// SPDX-License-Identifier: Apache 2.0
// Copyright 2025-Present Light Transport Entertainment Inc.
//
// Lock-free task queue for multi-threaded task execution.
//
// Implemented as a bounded multi-producer/multi-consumer (MPMC) queue using
// Dmitry Vyukov's algorithm: each ring slot carries an atomic sequence number
// so the payload store is release-ordered against the slot becoming visible to
// consumers, and the payload load is acquire-ordered against the producer's
// publish. This makes the payload access correctly synchronized w.r.t. the
// position counters (the earlier hand-rolled ring claimed the slot with a CAS
// and only *then* touched the payload, leaving the payload access unordered).
//
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <cstdint>
#include <cstddef>

namespace tinyusdz {

// C function pointer task type
typedef void (*TaskFuncPtr)(void* user_data);

// Task item for C function pointer version
struct TaskItem {
  TaskFuncPtr func;
  void* user_data;

  TaskItem() : func(nullptr), user_data(nullptr) {}
  TaskItem(TaskFuncPtr f, void* d) : func(f), user_data(d) {}
};

// Task item for std::function version
struct TaskItemFunc {
  std::function<void()> func;

  TaskItemFunc() : func(nullptr) {}
  explicit TaskItemFunc(std::function<void()> f) : func(std::move(f)) {}
};

namespace detail {

// One ring slot: an atomic sequence number plus the payload.
template <typename T>
struct TaskQueueCell {
  std::atomic<uint64_t> seq;
  T data;
  TaskQueueCell() : seq(0), data() {}
};

///
/// Bounded MPMC queue (Vyukov). Shared core for the TaskItem and TaskItemFunc
/// queues. Capacity is fixed at construction; works for any capacity (slot
/// index is `pos % capacity`).
///
template <typename T>
class TaskQueueBase {
 public:
  explicit TaskQueueBase(size_t capacity)
      : _capacity(capacity ? capacity : 1),
        _buffer(new detail::TaskQueueCell<T>[_capacity]),
        _enqueue_pos(0),
        _dequeue_pos(0) {
    // Seed each slot's sequence with its index (enqueue-ready state).
    for (size_t i = 0; i < _capacity; i++) {
      _buffer[i].seq.store(i, std::memory_order_relaxed);
    }
  }

  ~TaskQueueBase() = default;

  TaskQueueBase(const TaskQueueBase&) = delete;
  TaskQueueBase& operator=(const TaskQueueBase&) = delete;

  /// Enqueue. Returns false if the queue is full.
  bool enqueue(T&& item) {
    detail::TaskQueueCell<T>* cell;
    uint64_t pos = _enqueue_pos.load(std::memory_order_relaxed);
    for (;;) {
      cell = &_buffer[pos % _capacity];
      uint64_t seq = cell->seq.load(std::memory_order_acquire);
      int64_t dif = static_cast<int64_t>(seq) - static_cast<int64_t>(pos);
      if (dif == 0) {
        // Slot is free and ours to claim.
        if (_enqueue_pos.compare_exchange_weak(pos, pos + 1,
                                                std::memory_order_relaxed)) {
          break;
        }
      } else if (dif < 0) {
        return false;  // full
      } else {
        pos = _enqueue_pos.load(std::memory_order_relaxed);
      }
    }
    cell->data = std::move(item);
    // Publish: a consumer that acquire-loads this sees the payload above.
    cell->seq.store(pos + 1, std::memory_order_release);
    return true;
  }

  /// Dequeue into `out`. Returns false if the queue is empty.
  bool dequeue(T& out) {
    detail::TaskQueueCell<T>* cell;
    uint64_t pos = _dequeue_pos.load(std::memory_order_relaxed);
    for (;;) {
      cell = &_buffer[pos % _capacity];
      uint64_t seq = cell->seq.load(std::memory_order_acquire);
      int64_t dif = static_cast<int64_t>(seq) - static_cast<int64_t>(pos + 1);
      if (dif == 0) {
        if (_dequeue_pos.compare_exchange_weak(pos, pos + 1,
                                               std::memory_order_relaxed)) {
          break;
        }
      } else if (dif < 0) {
        return false;  // empty
      } else {
        pos = _dequeue_pos.load(std::memory_order_relaxed);
      }
    }
    out = std::move(cell->data);
    // Release the slot for reuse one ring-lap ahead.
    cell->seq.store(pos + _capacity, std::memory_order_release);
    return true;
  }

  /// Approximate count under concurrent use; exact when quiescent.
  size_t size() const {
    uint64_t e = _enqueue_pos.load(std::memory_order_acquire);
    uint64_t d = _dequeue_pos.load(std::memory_order_acquire);
    return (e >= d) ? static_cast<size_t>(e - d) : 0;
  }

  bool empty() const { return size() == 0; }

  size_t capacity() const { return _capacity; }

  /// Drain all pending items. Intended for single-threaded use (no concurrent
  /// enqueue/dequeue in flight).
  void clear() {
    T tmp;
    while (dequeue(tmp)) {
    }
  }

 private:
  const size_t _capacity;
  std::unique_ptr<detail::TaskQueueCell<T>[]> _buffer;
  // Padding-free: contention on these two counters is inherent to MPMC.
  std::atomic<uint64_t> _enqueue_pos;
  std::atomic<uint64_t> _dequeue_pos;
};

}  // namespace detail

///
/// Lock-free (bounded MPMC) task queue for C function pointers.
///
/// Example:
///   TaskQueue queue(1024);
///   queue.Push(my_task_func, my_data);
///   TaskItem task;
///   if (queue.Pop(task)) {
///     task.func(task.user_data);
///   }
///
class TaskQueue {
 public:
  explicit TaskQueue(size_t capacity = 1024) : _q(capacity) {}

  ~TaskQueue() = default;

  TaskQueue(const TaskQueue&) = delete;
  TaskQueue& operator=(const TaskQueue&) = delete;

  ///
  /// Push a task to the queue
  /// @param[in] func Task function pointer
  /// @param[in] user_data User data to pass to the task
  /// @return true on success, false if queue is full
  ///
  bool Push(TaskFuncPtr func, void* user_data) {
    if (!func) {
      return false;
    }
    return _q.enqueue(TaskItem(func, user_data));
  }

  ///
  /// Pop a task from the queue
  /// @param[out] task Retrieved task item
  /// @return true if a task was retrieved, false if queue is empty
  ///
  bool Pop(TaskItem& task) { return _q.dequeue(task); }

  /// Current queue size (approximate under concurrent use).
  size_t Size() const { return _q.size(); }

  bool Empty() const { return _q.empty(); }

  size_t Capacity() const { return _q.capacity(); }

  /// Clear all pending tasks (single-threaded use).
  void Clear() { _q.clear(); }

 private:
  detail::TaskQueueBase<TaskItem> _q;
};

///
/// Lock-free (bounded MPMC) task queue for the std::function version.
///
/// Example:
///   TaskQueueFunc queue(1024);
///   queue.Push([]() { std::cout << "Hello\n"; });
///   TaskItemFunc task;
///   if (queue.Pop(task)) {
///     task.func();
///   }
///
class TaskQueueFunc {
 public:
  explicit TaskQueueFunc(size_t capacity = 1024) : _q(capacity) {}

  ~TaskQueueFunc() = default;

  TaskQueueFunc(const TaskQueueFunc&) = delete;
  TaskQueueFunc& operator=(const TaskQueueFunc&) = delete;

  ///
  /// Push a task to the queue
  /// @param[in] func Task function (lambda, function object, etc.)
  /// @return true on success, false if queue is full
  ///
  bool Push(std::function<void()> func) {
    if (!func) {
      return false;
    }
    return _q.enqueue(TaskItemFunc(std::move(func)));
  }

  ///
  /// Pop a task from the queue
  /// @param[out] task Retrieved task item
  /// @return true if a task was retrieved, false if queue is empty
  ///
  bool Pop(TaskItemFunc& task) { return _q.dequeue(task); }

  /// Current queue size (approximate under concurrent use).
  size_t Size() const { return _q.size(); }

  bool Empty() const { return _q.empty(); }

  size_t Capacity() const { return _q.capacity(); }

  /// Clear all pending tasks (single-threaded use).
  void Clear() { _q.clear(); }

 private:
  detail::TaskQueueBase<TaskItemFunc> _q;
};

}  // namespace tinyusdz
