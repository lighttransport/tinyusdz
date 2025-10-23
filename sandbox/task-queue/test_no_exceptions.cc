#include "task-queue.hh"
#include <atomic>

using namespace tinyusdz::sandbox;

void dummy_task(void* data) {
  (void)data;
}

int main() {
  TaskQueue queue(16);
  std::atomic<int> counter(0);

  // Test basic operations
  queue.Push(dummy_task, &counter);

  TaskItem task;
  if (queue.Pop(task)) {
    if (task.func) {
      task.func(task.user_data);
    }
  }

  // Test function version
  TaskQueueFunc func_queue(16);
  func_queue.Push([]() { /* do nothing */ });

  TaskItemFunc func_task;
  if (func_queue.Pop(func_task)) {
    if (func_task.func) {
      func_task.func();
    }
  }

  return 0;
}
