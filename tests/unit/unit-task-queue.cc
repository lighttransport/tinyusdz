#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include "task-queue.hh"
#include "unit-common.hh"

#include <atomic>
#include <thread>
#include <vector>

using namespace tinyusdz;
using namespace tinyusdz_test;

// Test data structure
struct TestData {
  int value;
  std::atomic<int>* counter;
};

// Test task function
static void increment_task(void* user_data) {
  TestData* data = static_cast<TestData*>(user_data);
  if (data && data->counter) {
    data->counter->fetch_add(data->value, std::memory_order_relaxed);
  }
}

// Simple increment task
static void simple_increment(void* user_data) {
  std::atomic<int>* counter = static_cast<std::atomic<int>*>(user_data);
  if (counter) {
    counter->fetch_add(1, std::memory_order_relaxed);
  }
}

void task_queue_basic_test(void) {
  TaskQueue queue(16);
  std::atomic<int> counter(0);

  // Test push and pop
  TestData data1 = {10, &counter};
  TestData data2 = {20, &counter};
  TestData data3 = {30, &counter};

  TEST_CHECK(queue.Push(increment_task, &data1) == true);
  TEST_CHECK(queue.Push(increment_task, &data2) == true);
  TEST_CHECK(queue.Push(increment_task, &data3) == true);

  TEST_CHECK(queue.Size() == 3);
  TEST_CHECK(queue.Empty() == false);

  // Pop and execute tasks
  TaskItem task;
  int executed = 0;
  while (queue.Pop(task)) {
    if (task.func) {
      task.func(task.user_data);
      executed++;
    }
  }

  TEST_CHECK(executed == 3);
  TEST_CHECK(queue.Empty() == true);
  TEST_CHECK(counter.load() == 60);
}

void task_queue_func_test(void) {
  TaskQueueFunc queue(16);
  std::atomic<int> counter(0);

  // Push lambda tasks
  TEST_CHECK(queue.Push([&counter]() {
    counter.fetch_add(10, std::memory_order_relaxed);
  }) == true);

  TEST_CHECK(queue.Push([&counter]() {
    counter.fetch_add(20, std::memory_order_relaxed);
  }) == true);

  TEST_CHECK(queue.Push([&counter]() {
    counter.fetch_add(30, std::memory_order_relaxed);
  }) == true);

  // Capture by value
  int value = 40;
  TEST_CHECK(queue.Push([&counter, value]() {
    counter.fetch_add(value, std::memory_order_relaxed);
  }) == true);

  TEST_CHECK(queue.Size() == 4);

  // Pop and execute tasks
  TaskItemFunc task;
  int executed = 0;
  while (queue.Pop(task)) {
    if (task.func) {
      task.func();
      executed++;
    }
  }

  TEST_CHECK(executed == 4);
  TEST_CHECK(queue.Empty() == true);
  TEST_CHECK(counter.load() == 100);
}

void task_queue_full_test(void) {
  const size_t capacity = 8;
  TaskQueue queue(capacity);
  std::atomic<int> counter(0);

  // Use stack allocation
  std::vector<TestData> test_data(capacity + 10);
  for (auto& td : test_data) {
    td.value = 1;
    td.counter = &counter;
  }

  // Fill the queue
  size_t pushed = 0;
  for (size_t i = 0; i < capacity + 10; i++) {
    if (queue.Push(increment_task, &test_data[i])) {
      pushed++;
    }
  }

  TEST_CHECK(pushed <= capacity);
  TEST_CHECK(queue.Size() == pushed);

  // Pop all tasks to verify they work
  TaskItem task;
  size_t popped = 0;
  while (queue.Pop(task)) {
    if (task.func) {
      task.func(task.user_data);
      popped++;
    }
  }

  TEST_CHECK(popped == pushed);
  TEST_CHECK(queue.Empty() == true);
  TEST_CHECK(counter.load() == static_cast<int>(pushed));
}

void task_queue_multithreaded_test(void) {
  const int NUM_PRODUCERS = 2;
  const int NUM_CONSUMERS = 2;
  const int TASKS_PER_PRODUCER = 500;

  TaskQueue queue(256);
  std::atomic<int> counter(0);
  std::atomic<bool> done(false);

  // Producer threads
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
  TEST_CHECK(counter.load() == expected);
  TEST_CHECK(queue.Empty() == true);
}

void task_queue_clear_test(void) {
  TaskQueue queue(16);
  std::atomic<int> counter(0);

  TestData data = {1, &counter};

  // Add some tasks
  for (int i = 0; i < 5; i++) {
    TEST_CHECK(queue.Push(increment_task, &data) == true);
  }

  TEST_CHECK(queue.Size() == 5);

  // Clear the queue
  queue.Clear();

  TEST_CHECK(queue.Empty() == true);
  TEST_CHECK(queue.Size() == 0);

  // Should be able to push again
  TEST_CHECK(queue.Push(increment_task, &data) == true);
  TEST_CHECK(queue.Size() == 1);
}
