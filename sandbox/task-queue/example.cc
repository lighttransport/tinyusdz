#include "task-queue.hh"

#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>
#include <cassert>

using namespace tinyusdz::sandbox;

// Test data structure
struct TestData {
  int value;
  std::atomic<int>* counter;
};

// Example C function pointer task
void increment_task(void* user_data) {
  TestData* data = static_cast<TestData*>(user_data);
  if (data && data->counter) {
    data->counter->fetch_add(data->value, std::memory_order_relaxed);
  }
}

// Example test: single-threaded basic operations
void test_basic_operations() {
  std::cout << "=== Test: Basic Operations ===" << std::endl;

  TaskQueue queue(16);
  std::atomic<int> counter(0);

  // Test push and pop
  TestData data1 = {10, &counter};
  TestData data2 = {20, &counter};
  TestData data3 = {30, &counter};

  assert(queue.Push(increment_task, &data1) == true);
  assert(queue.Push(increment_task, &data2) == true);
  assert(queue.Push(increment_task, &data3) == true);

  assert(queue.Size() == 3);
  assert(queue.Empty() == false);

  // Pop and execute tasks
  TaskItem task;
  int executed = 0;
  while (queue.Pop(task)) {
    if (task.func) {
      task.func(task.user_data);
      executed++;
    }
  }

  assert(executed == 3);
  assert(queue.Empty() == true);
  assert(counter.load() == 60);

  std::cout << "  Counter value: " << counter.load() << " (expected 60)" << std::endl;
  std::cout << "  PASSED" << std::endl << std::endl;
}

// Simple task that just increments a shared counter
void simple_increment(void* user_data) {
  std::atomic<int>* counter = static_cast<std::atomic<int>*>(user_data);
  if (counter) {
    counter->fetch_add(1, std::memory_order_relaxed);
  }
}

// Example test: multi-threaded producer-consumer
void test_multithreaded() {
  std::cout << "=== Test: Multi-threaded Producer-Consumer ===" << std::endl;

  const int NUM_PRODUCERS = 4;
  const int NUM_CONSUMERS = 4;
  const int TASKS_PER_PRODUCER = 1000;

  TaskQueue queue(512);
  std::atomic<int> counter(0);
  std::atomic<bool> done(false);

  // Producer threads - pass counter address directly
  std::vector<std::thread> producers;
  for (int i = 0; i < NUM_PRODUCERS; i++) {
    producers.emplace_back([&queue, &counter]() {
      for (int j = 0; j < TASKS_PER_PRODUCER; j++) {
        while (!queue.Push(simple_increment, &counter)) {
          std::this_thread::yield();
        }
      }
    });
  }

  // Consumer threads
  std::vector<std::thread> consumers;
  for (int i = 0; i < NUM_CONSUMERS; i++) {
    consumers.emplace_back([&queue, &done]() {
      TaskItem task;
      while (!done.load(std::memory_order_acquire) || !queue.Empty()) {
        if (queue.Pop(task)) {
          if (task.func) {
            task.func(task.user_data);
          }
        } else {
          std::this_thread::yield();
        }
      }
    });
  }

  // Wait for producers to finish
  for (auto& t : producers) {
    t.join();
  }

  done.store(true, std::memory_order_release);

  // Wait for consumers to finish
  for (auto& t : consumers) {
    t.join();
  }

  int expected = NUM_PRODUCERS * TASKS_PER_PRODUCER;
  std::cout << "  Counter value: " << counter.load() << " (expected " << expected << ")" << std::endl;
  assert(counter.load() == expected);
  std::cout << "  PASSED" << std::endl << std::endl;
}

// Example test: std::function version
void test_function_version() {
  std::cout << "=== Test: std::function Version ===" << std::endl;

  TaskQueueFunc queue(16);
  std::atomic<int> counter(0);

  // Push lambda tasks
  queue.Push([&counter]() { counter.fetch_add(10, std::memory_order_relaxed); });
  queue.Push([&counter]() { counter.fetch_add(20, std::memory_order_relaxed); });
  queue.Push([&counter]() { counter.fetch_add(30, std::memory_order_relaxed); });

  // Capture by value
  int value = 40;
  queue.Push([&counter, value]() { counter.fetch_add(value, std::memory_order_relaxed); });

  assert(queue.Size() == 4);

  // Pop and execute tasks
  TaskItemFunc task;
  int executed = 0;
  while (queue.Pop(task)) {
    if (task.func) {
      task.func();
      executed++;
    }
  }

  assert(executed == 4);
  assert(queue.Empty() == true);
  assert(counter.load() == 100);

  std::cout << "  Counter value: " << counter.load() << " (expected 100)" << std::endl;
  std::cout << "  PASSED" << std::endl << std::endl;
}

// Example test: queue full behavior
void test_queue_full() {
  std::cout << "=== Test: Queue Full Behavior ===" << std::endl;

  const size_t capacity = 8;
  TaskQueue queue(capacity);
  std::atomic<int> counter(0);

  // Use stack allocation instead of heap to avoid memory leaks in this test
  std::vector<TestData> test_data(capacity + 10);
  for (auto& td : test_data) {
    td.value = 1;
    td.counter = &counter;
  }

  // Fill the queue
  int pushed = 0;
  for (size_t i = 0; i < capacity + 10; i++) {
    if (queue.Push(increment_task, &test_data[i])) {
      pushed++;
    }
  }

  std::cout << "  Pushed " << pushed << " tasks (capacity: " << capacity << ")" << std::endl;
  assert(pushed <= static_cast<int>(capacity));

  // Pop all tasks to verify they work
  TaskItem task;
  int popped = 0;
  while (queue.Pop(task)) {
    if (task.func) {
      task.func(task.user_data);
      popped++;
    }
  }

  assert(popped == pushed);
  assert(queue.Empty() == true);

  std::cout << "  Popped " << popped << " tasks, counter: " << counter.load() << std::endl;
  std::cout << "  PASSED" << std::endl << std::endl;
}

// Print build configuration
void print_build_info() {
  std::cout << "=== Build Configuration ===" << std::endl;
#if TASKQUEUE_HAS_BUILTIN_ATOMICS
  std::cout << "  Lock-free atomics: ENABLED (using compiler builtins)" << std::endl;
#else
  std::cout << "  Lock-free atomics: DISABLED (using std::mutex fallback)" << std::endl;
#endif

#if defined(__GNUC__) && !defined(__clang__)
  std::cout << "  Compiler: GCC " << __GNUC__ << "." << __GNUC_MINOR__ << std::endl;
#elif defined(__clang__)
  std::cout << "  Compiler: Clang " << __clang_major__ << "." << __clang_minor__ << std::endl;
#elif defined(_MSC_VER)
  std::cout << "  Compiler: MSVC " << _MSC_VER << std::endl;
#else
  std::cout << "  Compiler: Unknown" << std::endl;
#endif
  std::cout << std::endl;
}

int main() {
  std::cout << "\n";
  std::cout << "========================================" << std::endl;
  std::cout << "  Task Queue Example and Tests" << std::endl;
  std::cout << "========================================" << std::endl;
  std::cout << std::endl;

  print_build_info();

  test_basic_operations();
  test_function_version();
  test_queue_full();
  test_multithreaded();

  std::cout << "========================================" << std::endl;
  std::cout << "  All tests PASSED!" << std::endl;
  std::cout << "========================================" << std::endl;
  std::cout << std::endl;

  return 0;
}
