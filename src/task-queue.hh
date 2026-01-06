// SPDX-License-Identifier: Apache 2.0
// Copyright 2025-Present Light Transport Entertainment Inc.
//
// Lock-free task queue for multi-threaded task execution
//
#pragma once

#include <atomic>
#include <mutex>
#include <functional>
#include <vector>
#include <cstdint>
#include <cstddef>

// Detect compiler support for lock-free atomics
#if defined(__GNUC__) || defined(__clang__)
  #define TINYUSDZ_TASK_QUEUE_HAS_BUILTIN_ATOMICS 1
#elif defined(_MSC_VER) && (_MSC_VER >= 1900)
  #define TINYUSDZ_TASK_QUEUE_HAS_BUILTIN_ATOMICS 1
#else
  #define TINYUSDZ_TASK_QUEUE_HAS_BUILTIN_ATOMICS 0
#endif

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

///
/// Lock-free task queue for C function pointers
/// Uses lock-free atomics when available, falls back to mutex otherwise
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
  explicit TaskQueue(size_t capacity = 1024)
      : _capacity(capacity),
        _write_pos(0),
        _read_pos(0) {
    _tasks.resize(_capacity);
  }

  ~TaskQueue() = default;

  // Disable copy
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

#if TINYUSDZ_TASK_QUEUE_HAS_BUILTIN_ATOMICS
    // Lock-free implementation with CAS
    while (true) {
      uint64_t current_write = __atomic_load_n(&_write_pos, __ATOMIC_ACQUIRE);
      uint64_t current_read = __atomic_load_n(&_read_pos, __ATOMIC_ACQUIRE);

      // Check if queue is full
      if (current_write - current_read >= _capacity) {
        return false;
      }

      // Try to claim this slot with CAS
      uint64_t next_write = current_write + 1;
      if (__atomic_compare_exchange_n(&_write_pos, &current_write, next_write,
                                       false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        // Successfully claimed slot, now store the task
        size_t index = current_write % _capacity;
        _tasks[index] = TaskItem(func, user_data);
        return true;
      }
      // CAS failed, retry
    }
#else
    // Mutex fallback
    std::lock_guard<std::mutex> lock(_mutex);

    uint64_t current_write = _write_pos.load(std::memory_order_acquire);
    uint64_t next_write = current_write + 1;
    uint64_t current_read = _read_pos.load(std::memory_order_acquire);

    if (next_write - current_read > _capacity) {
      return false;
    }

    size_t index = current_write % _capacity;
    _tasks[index] = TaskItem(func, user_data);
    _write_pos.store(next_write, std::memory_order_release);
    return true;
#endif
  }

  ///
  /// Pop a task from the queue
  /// @param[out] task Retrieved task item
  /// @return true if a task was retrieved, false if queue is empty
  ///
  bool Pop(TaskItem& task) {
#if TINYUSDZ_TASK_QUEUE_HAS_BUILTIN_ATOMICS
    // Lock-free implementation with CAS
    while (true) {
      uint64_t current_read = __atomic_load_n(&_read_pos, __ATOMIC_ACQUIRE);
      uint64_t current_write = __atomic_load_n(&_write_pos, __ATOMIC_ACQUIRE);

      // Check if queue is empty
      if (current_read >= current_write) {
        return false;
      }

      // Try to claim this slot with CAS
      uint64_t next_read = current_read + 1;
      if (__atomic_compare_exchange_n(&_read_pos, &current_read, next_read,
                                       false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        // Successfully claimed slot, now load the task
        size_t index = current_read % _capacity;
        task = _tasks[index];
        return true;
      }
      // CAS failed, retry
    }
#else
    // Mutex fallback
    std::lock_guard<std::mutex> lock(_mutex);

    uint64_t current_read = _read_pos.load(std::memory_order_acquire);
    uint64_t current_write = _write_pos.load(std::memory_order_acquire);

    if (current_read >= current_write) {
      return false;
    }

    size_t index = current_read % _capacity;
    task = _tasks[index];
    _read_pos.store(current_read + 1, std::memory_order_release);
    return true;
#endif
  }

  ///
  /// Get current queue size (approximate in lock-free mode)
  ///
  size_t Size() const {
#if TINYUSDZ_TASK_QUEUE_HAS_BUILTIN_ATOMICS
    uint64_t w = __atomic_load_n(&_write_pos, __ATOMIC_ACQUIRE);
    uint64_t r = __atomic_load_n(&_read_pos, __ATOMIC_ACQUIRE);
#else
    uint64_t w = _write_pos.load(std::memory_order_acquire);
    uint64_t r = _read_pos.load(std::memory_order_acquire);
#endif
    return (w >= r) ? static_cast<size_t>(w - r) : 0;
  }

  ///
  /// Check if queue is empty
  ///
  bool Empty() const {
    return Size() == 0;
  }

  ///
  /// Get queue capacity
  ///
  size_t Capacity() const {
    return _capacity;
  }

  ///
  /// Clear all pending tasks
  ///
  void Clear() {
#if TINYUSDZ_TASK_QUEUE_HAS_BUILTIN_ATOMICS
    uint64_t w = __atomic_load_n(&_write_pos, __ATOMIC_ACQUIRE);
    __atomic_store_n(&_read_pos, w, __ATOMIC_RELEASE);
#else
    std::lock_guard<std::mutex> lock(_mutex);
    uint64_t w = _write_pos.load(std::memory_order_acquire);
    _read_pos.store(w, std::memory_order_release);
#endif
  }

 private:
  const size_t _capacity;
  std::vector<TaskItem> _tasks;

#if TINYUSDZ_TASK_QUEUE_HAS_BUILTIN_ATOMICS
  uint64_t _write_pos;
  uint64_t _read_pos;
#else
  std::atomic<uint64_t> _write_pos;
  std::atomic<uint64_t> _read_pos;
  std::mutex _mutex;
#endif
};

///
/// Task queue for std::function version
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
  explicit TaskQueueFunc(size_t capacity = 1024)
      : _capacity(capacity),
        _write_pos(0),
        _read_pos(0) {
    _tasks.resize(_capacity);
  }

  ~TaskQueueFunc() = default;

  // Disable copy
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

#if TINYUSDZ_TASK_QUEUE_HAS_BUILTIN_ATOMICS
    // Lock-free implementation with CAS
    while (true) {
      uint64_t current_write = __atomic_load_n(&_write_pos, __ATOMIC_ACQUIRE);
      uint64_t current_read = __atomic_load_n(&_read_pos, __ATOMIC_ACQUIRE);

      // Check if queue is full
      if (current_write - current_read >= _capacity) {
        return false;
      }

      // Try to claim this slot with CAS
      uint64_t next_write = current_write + 1;
      if (__atomic_compare_exchange_n(&_write_pos, &current_write, next_write,
                                       false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        // Successfully claimed slot, now store the task
        size_t index = current_write % _capacity;
        _tasks[index] = TaskItemFunc(std::move(func));
        return true;
      }
      // CAS failed, retry
    }
#else
    // Mutex fallback
    std::lock_guard<std::mutex> lock(_mutex);

    uint64_t current_write = _write_pos.load(std::memory_order_acquire);
    uint64_t next_write = current_write + 1;
    uint64_t current_read = _read_pos.load(std::memory_order_acquire);

    if (next_write - current_read > _capacity) {
      return false;
    }

    size_t index = current_write % _capacity;
    _tasks[index] = TaskItemFunc(std::move(func));
    _write_pos.store(next_write, std::memory_order_release);
    return true;
#endif
  }

  ///
  /// Pop a task from the queue
  /// @param[out] task Retrieved task item
  /// @return true if a task was retrieved, false if queue is empty
  ///
  bool Pop(TaskItemFunc& task) {
#if TINYUSDZ_TASK_QUEUE_HAS_BUILTIN_ATOMICS
    // Lock-free implementation with CAS
    while (true) {
      uint64_t current_read = __atomic_load_n(&_read_pos, __ATOMIC_ACQUIRE);
      uint64_t current_write = __atomic_load_n(&_write_pos, __ATOMIC_ACQUIRE);

      // Check if queue is empty
      if (current_read >= current_write) {
        return false;
      }

      // Try to claim this slot with CAS
      uint64_t next_read = current_read + 1;
      if (__atomic_compare_exchange_n(&_read_pos, &current_read, next_read,
                                       false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        // Successfully claimed slot, now load the task
        size_t index = current_read % _capacity;
        task = std::move(_tasks[index]);
        return true;
      }
      // CAS failed, retry
    }
#else
    // Mutex fallback
    std::lock_guard<std::mutex> lock(_mutex);

    uint64_t current_read = _read_pos.load(std::memory_order_acquire);
    uint64_t current_write = _write_pos.load(std::memory_order_acquire);

    if (current_read >= current_write) {
      return false;
    }

    size_t index = current_read % _capacity;
    task = std::move(_tasks[index]);
    _read_pos.store(current_read + 1, std::memory_order_release);
    return true;
#endif
  }

  ///
  /// Get current queue size (approximate in lock-free mode)
  ///
  size_t Size() const {
#if TINYUSDZ_TASK_QUEUE_HAS_BUILTIN_ATOMICS
    uint64_t w = __atomic_load_n(&_write_pos, __ATOMIC_ACQUIRE);
    uint64_t r = __atomic_load_n(&_read_pos, __ATOMIC_ACQUIRE);
#else
    uint64_t w = _write_pos.load(std::memory_order_acquire);
    uint64_t r = _read_pos.load(std::memory_order_acquire);
#endif
    return (w >= r) ? static_cast<size_t>(w - r) : 0;
  }

  ///
  /// Check if queue is empty
  ///
  bool Empty() const {
    return Size() == 0;
  }

  ///
  /// Get queue capacity
  ///
  size_t Capacity() const {
    return _capacity;
  }

  ///
  /// Clear all pending tasks
  ///
  void Clear() {
#if TINYUSDZ_TASK_QUEUE_HAS_BUILTIN_ATOMICS
    uint64_t w = __atomic_load_n(&_write_pos, __ATOMIC_ACQUIRE);
    __atomic_store_n(&_read_pos, w, __ATOMIC_RELEASE);
#else
    std::lock_guard<std::mutex> lock(_mutex);
    uint64_t w = _write_pos.load(std::memory_order_acquire);
    _read_pos.store(w, std::memory_order_release);
#endif
  }

 private:
  const size_t _capacity;
  std::vector<TaskItemFunc> _tasks;

#if TINYUSDZ_TASK_QUEUE_HAS_BUILTIN_ATOMICS
  uint64_t _write_pos;
  uint64_t _read_pos;
#else
  std::atomic<uint64_t> _write_pos;
  std::atomic<uint64_t> _read_pos;
  std::mutex _mutex;
#endif
};

}  // namespace tinyusdz
