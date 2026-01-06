// SPDX-License-Identifier: Apache 2.0
// Copyright 2025-Present Light Transport Entertainment Inc.
//
// Parallel pretty-printing for Prim and PrimSpec
//
#include "prim-pprint-parallel.hh"
#include "prim-pprint.hh"
#include <sstream>

namespace tinyusdz {
namespace prim {

#if defined(TINYUSDZ_ENABLE_THREAD)

// Worker function for printing Prims
static void print_prim_worker(void* user_data) {
  PrintPrimTask* task = static_cast<PrintPrimTask*>(user_data);
  if (task && task->prim && task->output) {
    *(task->output) = print_prim(*(task->prim), task->indent);
  }
}

// Worker function for printing PrimSpecs
static void print_primspec_worker(void* user_data) {
  PrintPrimSpecTask* task = static_cast<PrintPrimSpecTask*>(user_data);
  if (task && task->primspec && task->output) {
    *(task->output) = print_primspec(*(task->primspec), task->indent);
  }
}

std::string print_prims_parallel(
    const std::vector<const Prim*>& prims,
    uint32_t indent,
    const ParallelPrintConfig& config) {

  // Check if parallel printing is worth it
  if (!config.enabled || prims.size() < config.min_prims_for_parallel) {
    // Fall back to sequential printing
    std::stringstream ss;
    for (size_t i = 0; i < prims.size(); i++) {
      if (prims[i]) {
        ss << print_prim(*prims[i], indent);
        if (i != (prims.size() - 1)) {
          ss << "\n";
        }
      }
    }
    return ss.str();
  }

  // Prepare output buffers
  std::vector<std::string> outputs(prims.size());
  std::vector<PrintPrimTask> tasks(prims.size());

  // Initialize tasks
  for (size_t i = 0; i < prims.size(); i++) {
    tasks[i] = PrintPrimTask(prims[i], indent, i, &outputs[i]);
  }

  // Create task queue
  TaskQueue queue(config.task_queue_capacity);
  std::atomic<size_t> completed_tasks(0);
  std::atomic<bool> producer_done(false);

  // Launch worker threads
  std::vector<std::thread> workers;
  workers.reserve(config.num_threads);

  for (size_t t = 0; t < config.num_threads; t++) {
    workers.emplace_back([&queue, &completed_tasks, &producer_done]() {
      TaskItem task;
      while (!producer_done.load(std::memory_order_acquire) || !queue.Empty()) {
        if (queue.Pop(task)) {
          if (task.func) {
            task.func(task.user_data);
            completed_tasks.fetch_add(1, std::memory_order_relaxed);
          }
        } else {
          std::this_thread::yield();
        }
      }
    });
  }

  // Producer: push all tasks
  for (size_t i = 0; i < tasks.size(); i++) {
    while (!queue.Push(print_prim_worker, &tasks[i])) {
      std::this_thread::yield();
    }
  }

  producer_done.store(true, std::memory_order_release);

  // Wait for all workers to finish
  for (auto& worker : workers) {
    worker.join();
  }

  // Concatenate results in original order
  std::stringstream ss;
  for (size_t i = 0; i < outputs.size(); i++) {
    ss << outputs[i];
    if (i != (outputs.size() - 1)) {
      ss << "\n";
    }
  }

  return ss.str();
}

std::string print_primspecs_parallel(
    const std::vector<const PrimSpec*>& primspecs,
    uint32_t indent,
    const ParallelPrintConfig& config) {

  // Check if parallel printing is worth it
  if (!config.enabled || primspecs.size() < config.min_prims_for_parallel) {
    // Fall back to sequential printing
    std::stringstream ss;
    for (size_t i = 0; i < primspecs.size(); i++) {
      if (primspecs[i]) {
        ss << print_primspec(*primspecs[i], indent);
        if (i != (primspecs.size() - 1)) {
          ss << "\n";
        }
      }
    }
    return ss.str();
  }

  // Prepare output buffers
  std::vector<std::string> outputs(primspecs.size());
  std::vector<PrintPrimSpecTask> tasks(primspecs.size());

  // Initialize tasks
  for (size_t i = 0; i < primspecs.size(); i++) {
    tasks[i] = PrintPrimSpecTask(primspecs[i], indent, i, &outputs[i]);
  }

  // Create task queue
  TaskQueue queue(config.task_queue_capacity);
  std::atomic<size_t> completed_tasks(0);
  std::atomic<bool> producer_done(false);

  // Launch worker threads
  std::vector<std::thread> workers;
  workers.reserve(config.num_threads);

  for (size_t t = 0; t < config.num_threads; t++) {
    workers.emplace_back([&queue, &completed_tasks, &producer_done]() {
      TaskItem task;
      while (!producer_done.load(std::memory_order_acquire) || !queue.Empty()) {
        if (queue.Pop(task)) {
          if (task.func) {
            task.func(task.user_data);
            completed_tasks.fetch_add(1, std::memory_order_relaxed);
          }
        } else {
          std::this_thread::yield();
        }
      }
    });
  }

  // Producer: push all tasks
  for (size_t i = 0; i < tasks.size(); i++) {
    while (!queue.Push(print_primspec_worker, &tasks[i])) {
      std::this_thread::yield();
    }
  }

  producer_done.store(true, std::memory_order_release);

  // Wait for all workers to finish
  for (auto& worker : workers) {
    worker.join();
  }

  // Concatenate results in original order
  std::stringstream ss;
  for (size_t i = 0; i < outputs.size(); i++) {
    ss << outputs[i];
    if (i != (outputs.size() - 1)) {
      ss << "\n";
    }
  }

  return ss.str();
}

#endif  // TINYUSDZ_ENABLE_THREAD

}  // namespace prim
}  // namespace tinyusdz
