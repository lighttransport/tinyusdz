// SPDX-License-Identifier: Apache 2.0
// Copyright 2025 Light Transport Entertainment Inc.
//
// PCP Threading Infrastructure Implementation
//

#include "pcp-threading.hh"
#include <algorithm>
#include <iostream>

namespace tinyusdz {
namespace tydra {
namespace pcp {

ThreadPool::ThreadPool(size_t num_threads)
    : task_queue_(1024),
      enabled_(true),
      shutdown_(false),
      pending_tasks_(),
      completed_tasks_() {
  // Use number of hardware threads if not specified
  if (num_threads == 0) {
    num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) {
      num_threads = 4;  // Fallback default
    }
  }

  // Create worker threads
  for (size_t i = 0; i < num_threads; ++i) {
    threads_.emplace_back(&ThreadPool::WorkerThread, this);
  }
}

ThreadPool::~ThreadPool() {
  Shutdown();
}

bool ThreadPool::SubmitTask(std::shared_ptr<CompositionTask> task) {
  if (!enabled_.load(std::memory_order_acquire) || !task) {
    return false;
  }

  pending_tasks_.Increment();

  // Create lambda that wraps the task execution
  auto task_wrapper = [this, task]() {
    try {
      task->state.store(CompositionTask::State::RUNNING,
                        std::memory_order_release);

      // TODO: Call actual composition evaluation here
      // For now, simulate with callback
      if (task->callback) {
        task->callback(task->prim_path, task->error);
      }

      task->state.store(CompositionTask::State::COMPLETED,
                        std::memory_order_release);
    } catch (const std::exception& e) {
      task->error.message = std::string(e.what());
      task->error_code = -1;
      task->state.store(CompositionTask::State::FAILED,
                        std::memory_order_release);
    }
    pending_tasks_.Decrement();
    completed_tasks_.Increment();
  };

  return task_queue_.Push(task_wrapper);
}

size_t ThreadPool::SubmitTasks(
    const std::vector<std::shared_ptr<CompositionTask>>& tasks) {
  size_t submitted = 0;
  for (const auto& task : tasks) {
    if (SubmitTask(task)) {
      ++submitted;
    }
  }
  return submitted;
}

void ThreadPool::WaitAll() {
  pending_tasks_.Wait(0);
}

void ThreadPool::WaitUntil(size_t count) {
  // Wait until at least count tasks are completed
  while (completed_tasks_.Count() < static_cast<int>(count)) {
    std::this_thread::yield();
  }
}

size_t ThreadPool::PendingTaskCount() const {
  return std::max(0, pending_tasks_.Count());
}

size_t ThreadPool::CompletedTaskCount() const {
  return std::max(0, completed_tasks_.Count());
}

void ThreadPool::Shutdown() {
  enabled_.store(false, std::memory_order_release);
  WaitAll();  // Wait for all pending tasks

  {
    std::lock_guard<std::mutex> lock(shutdown_mutex_);
    shutdown_.store(true, std::memory_order_release);
  }
  shutdown_cv_.notify_all();

  // Join all threads
  for (auto& thread : threads_) {
    if (thread.joinable()) {
      thread.join();
    }
  }
  threads_.clear();
}

void ThreadPool::WorkerThread() {
  while (!shutdown_.load(std::memory_order_acquire)) {
    TaskItemFunc task_item;

    if (task_queue_.Pop(task_item)) {
      if (task_item.func) {
        try {
          task_item.func();
        } catch (const std::exception& e) {
          // Log error but continue
          std::cerr << "Worker thread exception: " << e.what() << std::endl;
        }
      }
    } else {
      // No tasks available, yield to avoid busy waiting
      std::this_thread::yield();
    }
  }
}

/// Parallel Composition Evaluator Implementation
ParallelCompositionEvaluator::ParallelCompositionEvaluator(Cache* cache,
                                                           size_t num_threads)
    : cache_(cache) {
  thread_pool_ = std::make_unique<ThreadPool>(num_threads);
}

ParallelCompositionEvaluator::~ParallelCompositionEvaluator() {
  // Thread pool will be destroyed automatically
}

std::vector<ParallelCompositionEvaluator::EvaluationResult>
ParallelCompositionEvaluator::EvaluateParallel(
    const std::vector<Path>& prim_paths,
    const ComputePrimIndexOptions& options) {
  if (!cache_) {
    return {};
  }

  options_ = options;
  std::vector<EvaluationResult> results;
  std::vector<std::shared_ptr<CompositionTask>> tasks;

  // Create tasks for each prim path
  for (const auto& path : prim_paths) {
    auto task = std::make_shared<CompositionTask>(
        path, [](const Path&, const Error&) {
          // Callback - can be used for progress tracking
        });
    tasks.push_back(task);
  }

  // Submit all tasks
  thread_pool_->SubmitTasks(tasks);

  // Wait for all to complete
  thread_pool_->WaitAll();

  // Collect results
  for (const auto& task : tasks) {
    EvaluationResult result;
    result.path = task->prim_path;
    result.prim_index = task->result;
    if (task->error_code != 0) {
      result.errors.push_back(task->error);
    }
    results.push_back(result);
  }

  return results;
}

}  // namespace pcp
}  // namespace tydra
}  // namespace tinyusdz
