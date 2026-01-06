#pragma once

#include <atomic>
#include <mutex>
#include <functional>
#include <vector>
#include <cstdint>
#include <cstddef>

// Detect compiler support for lock-free atomics
#if defined(__GNUC__) || defined(__clang__)
  #define TASKQUEUE_HAS_BUILTIN_ATOMICS 1
#elif defined(_MSC_VER) && (_MSC_VER >= 1900)
  #define TASKQUEUE_HAS_BUILTIN_ATOMICS 1
#else
  #define TASKQUEUE_HAS_BUILTIN_ATOMICS 0
#endif

namespace tinyusdz {
namespace sandbox {

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
class TaskQueue {
 public:
  explicit TaskQueue(size_t capacity = 1024)
      : capacity_(capacity),
        write_pos_(0),
        read_pos_(0) {
    tasks_.resize(capacity_);
  }

  ~TaskQueue() = default;

  // Disable copy
  TaskQueue(const TaskQueue&) = delete;
  TaskQueue& operator=(const TaskQueue&) = delete;

  ///
  /// Push a task to the queue
  /// Returns true on success, false if queue is full
  ///
  bool Push(TaskFuncPtr func, void* user_data) {
    if (!func) {
      return false;
    }

#if TASKQUEUE_HAS_BUILTIN_ATOMICS
    // Lock-free implementation with CAS
    while (true) {
      uint64_t current_write = __atomic_load_n(&write_pos_, __ATOMIC_ACQUIRE);
      uint64_t current_read = __atomic_load_n(&read_pos_, __ATOMIC_ACQUIRE);

      // Check if queue is full
      if (current_write - current_read >= capacity_) {
        return false;
      }

      // Try to claim this slot with CAS
      uint64_t next_write = current_write + 1;
      if (__atomic_compare_exchange_n(&write_pos_, &current_write, next_write,
                                       false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        // Successfully claimed slot, now store the task
        size_t index = current_write % capacity_;
        tasks_[index] = TaskItem(func, user_data);
        return true;
      }
      // CAS failed, retry
    }
#else
    // Mutex fallback
    std::lock_guard<std::mutex> lock(mutex_);

    uint64_t current_write = write_pos_.load(std::memory_order_acquire);
    uint64_t next_write = current_write + 1;
    uint64_t current_read = read_pos_.load(std::memory_order_acquire);

    if (next_write - current_read > capacity_) {
      return false;
    }

    size_t index = current_write % capacity_;
    tasks_[index] = TaskItem(func, user_data);
    write_pos_.store(next_write, std::memory_order_release);
    return true;
#endif
  }

  ///
  /// Pop a task from the queue
  /// Returns true if a task was retrieved, false if queue is empty
  ///
  bool Pop(TaskItem& task) {
#if TASKQUEUE_HAS_BUILTIN_ATOMICS
    // Lock-free implementation with CAS
    while (true) {
      uint64_t current_read = __atomic_load_n(&read_pos_, __ATOMIC_ACQUIRE);
      uint64_t current_write = __atomic_load_n(&write_pos_, __ATOMIC_ACQUIRE);

      // Check if queue is empty
      if (current_read >= current_write) {
        return false;
      }

      // Try to claim this slot with CAS
      uint64_t next_read = current_read + 1;
      if (__atomic_compare_exchange_n(&read_pos_, &current_read, next_read,
                                       false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        // Successfully claimed slot, now load the task
        size_t index = current_read % capacity_;
        task = tasks_[index];
        return true;
      }
      // CAS failed, retry
    }
#else
    // Mutex fallback
    std::lock_guard<std::mutex> lock(mutex_);

    uint64_t current_read = read_pos_.load(std::memory_order_acquire);
    uint64_t current_write = write_pos_.load(std::memory_order_acquire);

    if (current_read >= current_write) {
      return false;
    }

    size_t index = current_read % capacity_;
    task = tasks_[index];
    read_pos_.store(current_read + 1, std::memory_order_release);
    return true;
#endif
  }

  ///
  /// Get current queue size (approximate in lock-free mode)
  ///
  size_t Size() const {
#if TASKQUEUE_HAS_BUILTIN_ATOMICS
    uint64_t w = __atomic_load_n(&write_pos_, __ATOMIC_ACQUIRE);
    uint64_t r = __atomic_load_n(&read_pos_, __ATOMIC_ACQUIRE);
#else
    uint64_t w = write_pos_.load(std::memory_order_acquire);
    uint64_t r = read_pos_.load(std::memory_order_acquire);
#endif
    return (w >= r) ? (w - r) : 0;
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
    return capacity_;
  }

  ///
  /// Clear all pending tasks
  ///
  void Clear() {
#if TASKQUEUE_HAS_BUILTIN_ATOMICS
    uint64_t w = __atomic_load_n(&write_pos_, __ATOMIC_ACQUIRE);
    __atomic_store_n(&read_pos_, w, __ATOMIC_RELEASE);
#else
    std::lock_guard<std::mutex> lock(mutex_);
    uint64_t w = write_pos_.load(std::memory_order_acquire);
    read_pos_.store(w, std::memory_order_release);
#endif
  }

 private:
  const size_t capacity_;
  std::vector<TaskItem> tasks_;

#if TASKQUEUE_HAS_BUILTIN_ATOMICS
  uint64_t write_pos_;
  uint64_t read_pos_;
#else
  std::atomic<uint64_t> write_pos_;
  std::atomic<uint64_t> read_pos_;
  std::mutex mutex_;
#endif
};

///
/// Task queue for std::function version
///
class TaskQueueFunc {
 public:
  explicit TaskQueueFunc(size_t capacity = 1024)
      : capacity_(capacity),
        write_pos_(0),
        read_pos_(0) {
    tasks_.resize(capacity_);
  }

  ~TaskQueueFunc() = default;

  // Disable copy
  TaskQueueFunc(const TaskQueueFunc&) = delete;
  TaskQueueFunc& operator=(const TaskQueueFunc&) = delete;

  ///
  /// Push a task to the queue
  /// Returns true on success, false if queue is full
  ///
  bool Push(std::function<void()> func) {
    if (!func) {
      return false;
    }

#if TASKQUEUE_HAS_BUILTIN_ATOMICS
    // Lock-free implementation with CAS
    while (true) {
      uint64_t current_write = __atomic_load_n(&write_pos_, __ATOMIC_ACQUIRE);
      uint64_t current_read = __atomic_load_n(&read_pos_, __ATOMIC_ACQUIRE);

      // Check if queue is full
      if (current_write - current_read >= capacity_) {
        return false;
      }

      // Try to claim this slot with CAS
      uint64_t next_write = current_write + 1;
      if (__atomic_compare_exchange_n(&write_pos_, &current_write, next_write,
                                       false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        // Successfully claimed slot, now store the task
        size_t index = current_write % capacity_;
        tasks_[index] = TaskItemFunc(std::move(func));
        return true;
      }
      // CAS failed, retry
    }
#else
    // Mutex fallback
    std::lock_guard<std::mutex> lock(mutex_);

    uint64_t current_write = write_pos_.load(std::memory_order_acquire);
    uint64_t next_write = current_write + 1;
    uint64_t current_read = read_pos_.load(std::memory_order_acquire);

    if (next_write - current_read > capacity_) {
      return false;
    }

    size_t index = current_write % capacity_;
    tasks_[index] = TaskItemFunc(std::move(func));
    write_pos_.store(next_write, std::memory_order_release);
    return true;
#endif
  }

  ///
  /// Pop a task from the queue
  /// Returns true if a task was retrieved, false if queue is empty
  ///
  bool Pop(TaskItemFunc& task) {
#if TASKQUEUE_HAS_BUILTIN_ATOMICS
    // Lock-free implementation with CAS
    while (true) {
      uint64_t current_read = __atomic_load_n(&read_pos_, __ATOMIC_ACQUIRE);
      uint64_t current_write = __atomic_load_n(&write_pos_, __ATOMIC_ACQUIRE);

      // Check if queue is empty
      if (current_read >= current_write) {
        return false;
      }

      // Try to claim this slot with CAS
      uint64_t next_read = current_read + 1;
      if (__atomic_compare_exchange_n(&read_pos_, &current_read, next_read,
                                       false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        // Successfully claimed slot, now load the task
        size_t index = current_read % capacity_;
        task = std::move(tasks_[index]);
        return true;
      }
      // CAS failed, retry
    }
#else
    // Mutex fallback
    std::lock_guard<std::mutex> lock(mutex_);

    uint64_t current_read = read_pos_.load(std::memory_order_acquire);
    uint64_t current_write = write_pos_.load(std::memory_order_acquire);

    if (current_read >= current_write) {
      return false;
    }

    size_t index = current_read % capacity_;
    task = std::move(tasks_[index]);
    read_pos_.store(current_read + 1, std::memory_order_release);
    return true;
#endif
  }

  ///
  /// Get current queue size (approximate in lock-free mode)
  ///
  size_t Size() const {
#if TASKQUEUE_HAS_BUILTIN_ATOMICS
    uint64_t w = __atomic_load_n(&write_pos_, __ATOMIC_ACQUIRE);
    uint64_t r = __atomic_load_n(&read_pos_, __ATOMIC_ACQUIRE);
#else
    uint64_t w = write_pos_.load(std::memory_order_acquire);
    uint64_t r = read_pos_.load(std::memory_order_acquire);
#endif
    return (w >= r) ? (w - r) : 0;
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
    return capacity_;
  }

  ///
  /// Clear all pending tasks
  ///
  void Clear() {
#if TASKQUEUE_HAS_BUILTIN_ATOMICS
    uint64_t w = __atomic_load_n(&write_pos_, __ATOMIC_ACQUIRE);
    __atomic_store_n(&read_pos_, w, __ATOMIC_RELEASE);
#else
    std::lock_guard<std::mutex> lock(mutex_);
    uint64_t w = write_pos_.load(std::memory_order_acquire);
    read_pos_.store(w, std::memory_order_release);
#endif
  }

 private:
  const size_t capacity_;
  std::vector<TaskItemFunc> tasks_;

#if TASKQUEUE_HAS_BUILTIN_ATOMICS
  uint64_t write_pos_;
  uint64_t read_pos_;
#else
  std::atomic<uint64_t> write_pos_;
  std::atomic<uint64_t> read_pos_;
  std::mutex mutex_;
#endif
};

}  // namespace sandbox
}  // namespace tinyusdz
