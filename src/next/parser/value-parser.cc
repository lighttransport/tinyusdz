// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Value Parser implementation

#include "value-parser.hh"
#include "ascii-parser.hh"
#include "value-parser-numeric.hh"
#include "../strfmt.hh"
#include "lexer.hh"
#include "../crate/crate-format.hh"
#include "../types/type-info.hh"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cerrno>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <vector>

#if defined(TINYUSDZ_ENABLE_THREAD)
#include <thread>
#endif

namespace tinyusdz {
namespace next {

namespace {

// ============================================================
// Value parsing functions
// ============================================================

using value_parser_detail::DecimalToI64;
using value_parser_detail::DecimalToU64;
using value_parser_detail::FastFloatParse;
using value_parser_detail::FastFloatParseToken;

#if defined(TINYUSDZ_ENABLE_THREAD)
class ValueWorkerPool {
 public:
  explicit ValueWorkerPool(size_t nthreads) {
    if (nthreads < 1) nthreads = 1;
    workers_.reserve(nthreads);
    for (size_t i = 0; i < nthreads; i++) {
      workers_.emplace_back([this]() { WorkerLoop(); });
    }
  }

  ~ValueWorkerPool() {
    {
      std::lock_guard<std::mutex> lock(mu_);
      stop_ = true;
    }
    cv_.notify_all();
    for (std::thread& worker : workers_) {
      if (worker.joinable()) worker.join();
    }
  }

  ValueWorkerPool(const ValueWorkerPool&) = delete;
  ValueWorkerPool& operator=(const ValueWorkerPool&) = delete;

  void Submit(std::function<void()> job) {
    {
      std::lock_guard<std::mutex> lock(mu_);
      jobs_.push_back(std::move(job));
    }
    cv_.notify_one();
  }

 private:
  void WorkerLoop() {
    while (true) {
      std::function<void()> job;
      {
        std::unique_lock<std::mutex> lock(mu_);
        cv_.wait(lock, [this]() { return stop_ || !jobs_.empty(); });
        if (stop_ && jobs_.empty()) return;
        job = std::move(jobs_.front());
        jobs_.pop_front();
      }
      job();
    }
  }

  std::mutex mu_;
  std::condition_variable cv_;
  std::deque<std::function<void()>> jobs_;
  std::vector<std::thread> workers_;
  bool stop_ = false;
};

ValueWorkerPool* GetValueWorkerPool(int requested_threads) {
  int nthreads = requested_threads;
  if (nthreads <= 0) {
    nthreads = static_cast<int>(std::thread::hardware_concurrency());
    if (nthreads < 1) nthreads = 1;
    nthreads = std::min(nthreads, 8);
  }
  if (nthreads <= 1) return nullptr;

  static std::mutex pool_mu;
  static std::unique_ptr<ValueWorkerPool> pool;
  static int pool_threads = 0;
  std::lock_guard<std::mutex> lock(pool_mu);
  if (!pool || pool_threads != nthreads) {
    pool.reset(new ValueWorkerPool(static_cast<size_t>(nthreads)));
    pool_threads = nthreads;
  }
  return pool.get();
}
#endif

#include "value-parser-scalars.inc"

}  // anonymous namespace

// ============================================================
// Public API
// ============================================================

ParseResult ParseValue(Lexer& lexer, TypeId expected_type) {
  // Handle None: an authored value block, not "no value". Preserve it as a block
  // so the writer re-emits `= None` (round-trips USDC ValueBlock + USDA `= None`).
  if (lexer.peek().type == TokenType::None) {
    lexer.next();
    return ParseResult::Ok(Value::MakeBlock());
  }

  // Dictionaries have a dedicated recursive parser (no flat ParseFn entry).
  if (expected_type == TypeId::Dictionary ||
      lexer.peek().type == TokenType::OpenBrace) {
    return ParseDict(lexer);
  }

  ParseFn fn = GetParseFunction(expected_type);
  if (!fn) {
    // GetTypeName returns nullptr for ids without TypeInfo (e.g. semantic ids
    // a malformed file maps onto) — std::string(nullptr) is UB/abort.
    const char* tn = GetTypeName(expected_type);
    return ParseResult::Error("No parser for type " +
                              (tn ? std::string(tn)
                                  : "#" + IntToStr(int(expected_type))));
  }

  ParseResult result = fn(lexer);

  // If parsing succeeded but type has a semantic distinction, fix the type ID
  if (result.success) {
    // For semantic types, the parse function returns the base type
    // We need to update to the actual requested type
    switch (expected_type) {
      case TypeId::Point3f:
      case TypeId::Point3h:
      case TypeId::Vector3f:
      case TypeId::Vector3h:
      case TypeId::Normal3f:
      case TypeId::Normal3h:
      case TypeId::Color3f:
      case TypeId::Color3h:
      case TypeId::Point3d:
      case TypeId::Vector3d:
      case TypeId::Normal3d:
      case TypeId::Color3d:
      case TypeId::Color4f:
      case TypeId::Color4h:
      case TypeId::Color4d:
      case TypeId::Texcoord2f:
      case TypeId::Texcoord2h:
      case TypeId::Texcoord2d:
      case TypeId::Texcoord3f:
      case TypeId::Texcoord3h:
      case TypeId::Texcoord3d:
        result.value = Value::MakeFromRaw(expected_type, result.value.raw_data());
        break;
      default:
        break;
    }
  }

  return result;
}

#include "value-parser-arrays.inc"

#if defined(TINYUSDZ_ENABLE_THREAD)

// Batch shaping. Target enough text per task that the pool's mutex/condvar and
// std::function overhead vanish against the parse work (~0.5 MB of array text
// is roughly 1.5 ms of numeric conversion), while staying small enough that
// batches spread evenly across workers.
constexpr size_t kDeferredBatchTargetBytes = size_t(512) << 10;  // 512 KiB
constexpr size_t kDeferredBatchMaxItems = 1024;

struct DeferredArrayScheduler::Shared {
  std::mutex mu;
  std::condition_variable cv;
  size_t inflight = 0;
  std::atomic<bool> failed{false};
  std::string first_error;

  // Parse every item of one batch. Continues past prior failures only within
  // the already-submitted batch (cheap) but records only the first error.
  void RunBatch(std::vector<DeferredArrayItem>& items) {
    for (DeferredArrayItem& item : items) {
      if (failed.load(std::memory_order_relaxed)) return;
      if (!FillDeferredArrayValue(item)) {
        std::lock_guard<std::mutex> lock(mu);
        if (!failed.exchange(true, std::memory_order_relaxed)) {
          first_error = "Failed to parse deferred " +
                        std::string(GetTypeName(item.type_id)
                                        ? GetTypeName(item.type_id)
                                        : "numeric") +
                        " array value";
        }
        return;
      }
    }
  }
};

DeferredArrayScheduler::DeferredArrayScheduler(int num_threads)
    : shared_(std::make_shared<Shared>()), num_threads_(num_threads) {}

DeferredArrayScheduler::~DeferredArrayScheduler() {
  std::string err;
  Drain(&err);  // never leave workers writing into freed payloads
}

std::unique_ptr<DeferredArrayScheduler> DeferredArrayScheduler::Create(
    int num_threads) {
  ValueWorkerPool* pool = GetValueWorkerPool(num_threads);
  if (!pool) return nullptr;
  std::unique_ptr<DeferredArrayScheduler> s(
      new DeferredArrayScheduler(num_threads));
  // Bound how far the producer may run ahead: enough batches to keep every
  // worker busy plus a queue, without unbounded pending result memory.
  int nt = num_threads;
  if (nt <= 0) {
    nt = static_cast<int>(std::thread::hardware_concurrency());
    if (nt < 1) nt = 1;
    nt = std::min(nt, 8);
  }
  s->max_inflight_ = static_cast<size_t>(4 * nt);
  return s;
}

bool DeferredArrayScheduler::failed() const {
  return shared_->failed.load(std::memory_order_relaxed);
}

void DeferredArrayScheduler::SubmitBatch() {
  if (current_.empty()) return;
  std::vector<DeferredArrayItem> batch;
  batch.swap(current_);
  current_bytes_ = 0;

  {
    std::unique_lock<std::mutex> lock(shared_->mu);
    if (shared_->inflight >= max_inflight_) {
      // Backpressure: workers are saturated — parse this batch inline on the
      // producer thread instead of blocking on the queue (self-balancing).
      lock.unlock();
      shared_->RunBatch(batch);
      return;
    }
    shared_->inflight++;
  }

  ValueWorkerPool* pool = GetValueWorkerPool(num_threads_);
  if (!pool) {  // pool was reconfigured away: run inline, undo the count
    shared_->RunBatch(batch);
    std::lock_guard<std::mutex> lock(shared_->mu);
    shared_->inflight--;
    if (shared_->inflight == 0) shared_->cv.notify_all();
    return;
  }

  std::shared_ptr<Shared> st = shared_;
  auto batch_ptr =
      std::make_shared<std::vector<DeferredArrayItem>>(std::move(batch));
  pool->Submit([st, batch_ptr]() {
    st->RunBatch(*batch_ptr);
    std::lock_guard<std::mutex> lock(st->mu);
    st->inflight--;
    if (st->inflight == 0) st->cv.notify_all();
  });
}

void DeferredArrayScheduler::Enqueue(DeferredArrayItem item) {
  current_bytes_ += item.len;
  current_.push_back(std::move(item));
  if (current_bytes_ >= kDeferredBatchTargetBytes ||
      current_.size() >= kDeferredBatchMaxItems) {
    SubmitBatch();
  }
}

bool DeferredArrayScheduler::Drain(std::string* error) {
  SubmitBatch();
  {
    std::unique_lock<std::mutex> lock(shared_->mu);
    shared_->cv.wait(lock, [this]() { return shared_->inflight == 0; });
    if (shared_->failed.load(std::memory_order_relaxed)) {
      if (error) *error = shared_->first_error;
      return false;
    }
  }
  return true;
}

#endif  // TINYUSDZ_ENABLE_THREAD

ParseResult ParseGenericValue(Lexer& lexer, TypeId& out_type) {
  const Token& tok = lexer.peek();

  if (tok.type == TokenType::True || tok.type == TokenType::False) {
    out_type = TypeId::Bool;
    return ParseBool(lexer);
  }

  if (tok.type == TokenType::Number) {
    // Try to determine if it's int or float
    if (tok.value.find('.') != std::string::npos ||
        tok.value.find('e') != std::string::npos ||
        tok.value.find('E') != std::string::npos) {
      out_type = TypeId::Double;
      return ParseDouble(lexer);
    } else {
      out_type = TypeId::Int;
      return ParseInt(lexer);
    }
  }

  if (tok.type == TokenType::String) {
    out_type = TypeId::String;
    return ParseString(lexer);
  }

  if (tok.type == TokenType::OpenParen) {
    // Could be a tuple - peek ahead to determine size
    // For now, assume float3 as default
    out_type = TypeId::Float3;
    return ParseFloat3(lexer);
  }

  if (tok.type == TokenType::None) {
    out_type = TypeId::Invalid;
    lexer.next();
    return ParseResult::Ok(Value());
  }

  out_type = TypeId::Invalid;
  return ParseResult::Error("Cannot infer type from token");
}

#include "value-parser-dict.inc"


}  // namespace next
}  // namespace tinyusdz
