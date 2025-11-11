// SPDX-License-Identifier: Apache 2.0
// Copyright 2025 Light Transport Entertainment Inc.
//
// PCP Threading Infrastructure - Thread Pool and Synchronization
//

#pragma once

#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <vector>
#include <memory>
#include <functional>
#include <queue>
#include "../task-queue.hh"
#include "pcp-types.hh"

namespace tinyusdz {
namespace tydra {
namespace pcp {

// Forward declarations
class CompositionTask;
class ThreadPool;

/// Type for composition task callback
using CompositionCallback = std::function<void(const Path&, const Error&)>;

/// Represents a single composition task that can be executed in parallel
class CompositionTask {
 public:
  enum class State {
    PENDING,      // Task hasn't started
    RUNNING,      // Task is currently executing
    COMPLETED,    // Task finished successfully
    FAILED,       // Task failed
  };

  CompositionTask(const Path& path, CompositionCallback callback)
      : prim_path(path),
        callback(callback),
        state(State::PENDING),
        error_code(0) {}

  ~CompositionTask() = default;

  Path prim_path;
  CompositionCallback callback;
  std::atomic<State> state;
  Error error;
  int error_code;
  std::shared_ptr<PrimIndex> result;
};

/// Thread-safe work counter for synchronization
class WorkCounter {
 public:
  WorkCounter() : count_(0) {}

  void Increment() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++count_;
  }

  void Decrement() {
    std::unique_lock<std::mutex> lock(mutex_);
    --count_;
    cv_.notify_all();
  }

  void Wait(int target = 0) {
    std::unique_lock<std::mutex> lock(mutex_);
    while (count_ > target) {
      cv_.wait(lock);
    }
  }

  int Count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return count_;
  }

  void Reset(int value = 0) {
    std::lock_guard<std::mutex> lock(mutex_);
    count_ = value;
  }

 private:
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  int count_;
};

/// Thread pool for parallel composition evaluation
class ThreadPool {
 public:
  explicit ThreadPool(size_t num_threads = 0);
  ~ThreadPool();

  // Disable copy
  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  /// Submit a composition task
  /// @param[in] task Task to execute
  /// @return true on success
  bool SubmitTask(std::shared_ptr<CompositionTask> task);

  /// Submit multiple tasks
  /// @param[in] tasks Vector of tasks to execute
  /// @return number of tasks successfully submitted
  size_t SubmitTasks(const std::vector<std::shared_ptr<CompositionTask>>& tasks);

  /// Wait for all pending tasks to complete
  void WaitAll();

  /// Wait for specific number of tasks to complete
  /// @param[in] count Target number of completed tasks
  void WaitUntil(size_t count);

  /// Get number of pending tasks
  size_t PendingTaskCount() const;

  /// Get number of completed tasks
  size_t CompletedTaskCount() const;

  /// Get number of worker threads
  size_t ThreadCount() const { return threads_.size(); }

  /// Enable/disable thread pool
  void SetEnabled(bool enabled) {
    enabled_.store(enabled, std::memory_order_release);
  }

  bool IsEnabled() const {
    return enabled_.load(std::memory_order_acquire);
  }

  /// Shutdown thread pool and wait for all tasks
  void Shutdown();

 private:
  void WorkerThread();

  std::vector<std::thread> threads_;
  TaskQueueFunc task_queue_;
  std::atomic<bool> enabled_;
  std::atomic<bool> shutdown_;
  WorkCounter pending_tasks_;
  WorkCounter completed_tasks_;
  std::mutex shutdown_mutex_;
  std::condition_variable shutdown_cv_;
};

/// Thread-safe cache wrapper for parallel access
template <typename KeyType, typename ValueType>
class ThreadSafeCache {
 public:
  ThreadSafeCache() = default;
  ~ThreadSafeCache() = default;

  /// Get value from cache
  /// @param[in] key Cache key
  /// @param[out] value Retrieved value
  /// @return true if key found, false otherwise
  bool Get(const KeyType& key, ValueType& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = cache_.find(key);
    if (it != cache_.end()) {
      value = it->second;
      return true;
    }
    return false;
  }

  /// Put value into cache
  /// @param[in] key Cache key
  /// @param[in] value Value to cache
  void Put(const KeyType& key, const ValueType& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_[key] = value;
  }

  /// Remove value from cache
  /// @param[in] key Cache key
  /// @return true if key was removed, false if not found
  bool Remove(const KeyType& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = cache_.find(key);
    if (it != cache_.end()) {
      cache_.erase(it);
      return true;
    }
    return false;
  }

  /// Clear entire cache
  void Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.clear();
  }

  /// Get cache size
  size_t Size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cache_.size();
  }

  /// Check if key exists
  bool Contains(const KeyType& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cache_.find(key) != cache_.end();
  }

 private:
  mutable std::mutex mutex_;
  std::unordered_map<KeyType, ValueType> cache_;
};

/// Parallel composition evaluator
class ParallelCompositionEvaluator {
 public:
  explicit ParallelCompositionEvaluator(Cache* cache, size_t num_threads = 0);
  ~ParallelCompositionEvaluator();

  /// Evaluate multiple prims in parallel
  /// @param[in] prim_paths Vector of prim paths to evaluate
  /// @param[in] options Composition options
  /// @return Vector of (path, result, error) tuples
  struct EvaluationResult {
    Path path;
    std::shared_ptr<PrimIndex> prim_index;
    std::vector<Error> errors;
  };

  std::vector<EvaluationResult> EvaluateParallel(
      const std::vector<Path>& prim_paths,
      const ComputePrimIndexOptions& options);

  /// Get underlying thread pool
  ThreadPool* GetThreadPool() { return thread_pool_.get(); }

 private:
  Cache* cache_;
  std::unique_ptr<ThreadPool> thread_pool_;
  ComputePrimIndexOptions options_;
};

}  // namespace pcp
}  // namespace tydra
}  // namespace tinyusdz
