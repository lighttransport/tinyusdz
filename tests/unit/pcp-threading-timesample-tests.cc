// SPDX-License-Identifier: Apache 2.0
// Copyright 2025 Light Transport Entertainment Inc.
//
// PCP Threading and Time-Based Composition Tests
//

#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <cmath>
#include "../../src/tydra/pcp-threading.hh"
#include "../../src/tydra/pcp-timesample.hh"

namespace tinyusdz {
namespace tydra {
namespace pcp {

// ============================================================================
// Thread Pool Tests
// ============================================================================

class ThreadPoolTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create a basic thread pool with 4 threads
    thread_pool = std::make_unique<ThreadPool>(4);
  }

  void TearDown() override {
    if (thread_pool) {
      thread_pool->Shutdown();
    }
  }

  std::unique_ptr<ThreadPool> thread_pool;
};

TEST_F(ThreadPoolTest, BasicTaskSubmission) {
  int counter = 0;
  std::mutex counter_mutex;

  auto task = std::make_shared<CompositionTask>(
      Path("/Test/Prim"), [&](const Path&, const Error&) {});

  task->callback = [&](const Path&, const Error&) {
    std::lock_guard<std::mutex> lock(counter_mutex);
    ++counter;
  };

  EXPECT_TRUE(thread_pool->SubmitTask(task));

  // Wait a bit for task to complete
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  EXPECT_GT(counter, 0);
}

TEST_F(ThreadPoolTest, MultipleTaskSubmission) {
  std::atomic<int> completed(0);
  const int task_count = 10;

  for (int i = 0; i < task_count; ++i) {
    auto task = std::make_shared<CompositionTask>(
        Path("/Test/Prim_" + std::to_string(i)),
        [&](const Path&, const Error&) {
          completed.fetch_add(1, std::memory_order_relaxed);
        });

    EXPECT_TRUE(thread_pool->SubmitTask(task));
  }

  // Wait for all tasks
  thread_pool->WaitAll();

  EXPECT_EQ(completed.load(), task_count);
}

TEST_F(ThreadPoolTest, ThreadCount) {
  EXPECT_EQ(thread_pool->ThreadCount(), 4);
}

TEST_F(ThreadPoolTest, PendingTaskCount) {
  auto task = std::make_shared<CompositionTask>(
      Path("/Test/Prim"), [](const Path&, const Error&) {});

  thread_pool->SubmitTask(task);
  // Note: Due to timing, pending might be 0 by the time we check

  // Just verify the method exists and returns a number >= 0
  EXPECT_GE(thread_pool->PendingTaskCount(), 0);
}

TEST_F(ThreadPoolTest, Enable_DisableThreadPool) {
  thread_pool->SetEnabled(false);
  EXPECT_FALSE(thread_pool->IsEnabled());

  thread_pool->SetEnabled(true);
  EXPECT_TRUE(thread_pool->IsEnabled());
}

// ============================================================================
// Work Counter Tests
// ============================================================================

class WorkCounterTest : public ::testing::Test {
 protected:
  WorkCounter counter;
};

TEST_F(WorkCounterTest, IncrementDecrement) {
  counter.Reset(0);
  EXPECT_EQ(counter.Count(), 0);

  counter.Increment();
  EXPECT_EQ(counter.Count(), 1);

  counter.Increment();
  counter.Increment();
  EXPECT_EQ(counter.Count(), 3);

  counter.Decrement();
  EXPECT_EQ(counter.Count(), 2);
}

TEST_F(WorkCounterTest, WaitUntilZero) {
  counter.Reset(0);
  counter.Increment();
  counter.Increment();

  // Decrement in background thread
  std::thread t([this]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    counter.Decrement();
    counter.Decrement();
  });

  // Wait for counter to reach 0
  counter.Wait(0);
  EXPECT_EQ(counter.Count(), 0);

  t.join();
}

// ============================================================================
// Animation Timeline Tests
// ============================================================================

class AnimationTimelineTest : public ::testing::Test {
 protected:
  AnimationTimeline timeline{Path("/Test/AnimatedPrim")};

  std::shared_ptr<PrimIndex> CreateMockComposition(int id) {
    auto comp = std::make_shared<PrimIndex>();
    comp->SetPath(Path("/Comp_" + std::to_string(id)));
    return comp;
  }
};

TEST_F(AnimationTimelineTest, AddKeyframe) {
  auto comp1 = CreateMockComposition(1);
  auto comp2 = CreateMockComposition(2);

  timeline.AddKeyframe(0.0, comp1, InterpolationMode::LINEAR);
  timeline.AddKeyframe(1.0, comp2, InterpolationMode::LINEAR);

  EXPECT_EQ(timeline.GetKeyframeCount(), 2);
}

TEST_F(AnimationTimelineTest, KeyframeTimeRange) {
  auto comp1 = CreateMockComposition(1);
  auto comp2 = CreateMockComposition(2);

  timeline.AddKeyframe(0.5, comp1);
  timeline.AddKeyframe(2.5, comp2);

  double start, end;
  EXPECT_TRUE(timeline.GetTimeRange(start, end));
  EXPECT_DOUBLE_EQ(start, 0.5);
  EXPECT_DOUBLE_EQ(end, 2.5);
}

TEST_F(AnimationTimelineTest, EvaluateAtKeyframe) {
  auto comp1 = CreateMockComposition(1);
  auto comp2 = CreateMockComposition(2);

  timeline.AddKeyframe(0.0, comp1);
  timeline.AddKeyframe(1.0, comp2);

  std::shared_ptr<PrimIndex> result;
  EXPECT_TRUE(timeline.EvaluateAt(0.0, result));
  EXPECT_NE(result, nullptr);
  EXPECT_EQ(result->GetPath().ToString(), "/Comp_1");
}

TEST_F(AnimationTimelineTest, EvaluateInRange) {
  auto comp1 = CreateMockComposition(1);
  auto comp2 = CreateMockComposition(2);

  timeline.AddKeyframe(0.0, comp1);
  timeline.AddKeyframe(1.0, comp2);

  // Evaluate at time 0.5 (between keyframes)
  std::shared_ptr<PrimIndex> result;
  EXPECT_TRUE(timeline.EvaluateAt(0.5, result));
  EXPECT_NE(result, nullptr);
}

TEST_F(AnimationTimelineTest, GetKeyframeAt) {
  auto comp = CreateMockComposition(1);
  timeline.AddKeyframe(0.5, comp);

  auto kf = timeline.GetKeyframeAt(0);
  EXPECT_NE(kf, nullptr);
  EXPECT_DOUBLE_EQ(kf->time, 0.5);
}

TEST_F(AnimationTimelineTest, IsTimeInRange) {
  auto comp1 = CreateMockComposition(1);
  auto comp2 = CreateMockComposition(2);

  timeline.AddKeyframe(1.0, comp1);
  timeline.AddKeyframe(3.0, comp2);

  EXPECT_FALSE(timeline.IsTimeInRange(0.5));
  EXPECT_TRUE(timeline.IsTimeInRange(1.0));
  EXPECT_TRUE(timeline.IsTimeInRange(2.0));
  EXPECT_TRUE(timeline.IsTimeInRange(3.0));
  EXPECT_FALSE(timeline.IsTimeInRange(4.0));
}

TEST_F(AnimationTimelineTest, ClearKeyframes) {
  auto comp = CreateMockComposition(1);
  timeline.AddKeyframe(0.0, comp);
  timeline.AddKeyframe(1.0, comp);

  EXPECT_EQ(timeline.GetKeyframeCount(), 2);

  timeline.Clear();
  EXPECT_EQ(timeline.GetKeyframeCount(), 0);
}

// ============================================================================
// Composition Timeline Tests
// ============================================================================

class CompositionTimelineTest : public ::testing::Test {
 protected:
  CompositionTimeline comp_timeline;

  std::shared_ptr<PrimIndex> CreateMockComposition(int id) {
    auto comp = std::make_shared<PrimIndex>();
    comp->SetPath(Path("/Comp_" + std::to_string(id)));
    return comp;
  }
};

TEST_F(CompositionTimelineTest, AddTimeline) {
  auto anim_timeline =
      std::make_shared<AnimationTimeline>(Path("/Animated/Prim"));
  auto comp = CreateMockComposition(1);
  anim_timeline->AddKeyframe(0.0, comp);

  comp_timeline.AddTimeline(Path("/Animated/Prim"), anim_timeline);

  EXPECT_EQ(comp_timeline.GetTimelineCount(), 1);
}

TEST_F(CompositionTimelineTest, GetTimeline) {
  auto anim_timeline =
      std::make_shared<AnimationTimeline>(Path("/Animated/Prim"));
  auto comp = CreateMockComposition(1);
  anim_timeline->AddKeyframe(0.0, comp);

  comp_timeline.AddTimeline(Path("/Animated/Prim"), anim_timeline);

  auto retrieved = comp_timeline.GetTimeline(Path("/Animated/Prim"));
  EXPECT_NE(retrieved, nullptr);
  EXPECT_EQ(retrieved->GetPrimPath().ToString(), "/Animated/Prim");
}

TEST_F(CompositionTimelineTest, HasAnimation) {
  auto anim_timeline =
      std::make_shared<AnimationTimeline>(Path("/Animated/Prim"));
  auto comp = CreateMockComposition(1);
  anim_timeline->AddKeyframe(0.0, comp);

  comp_timeline.AddTimeline(Path("/Animated/Prim"), anim_timeline);

  EXPECT_TRUE(comp_timeline.HasAnimation(Path("/Animated/Prim")));
  EXPECT_FALSE(comp_timeline.HasAnimation(Path("/NonAnimated/Prim")));
}

TEST_F(CompositionTimelineTest, GetAnimatedPrims) {
  auto anim1 =
      std::make_shared<AnimationTimeline>(Path("/Animated/Prim1"));
  auto anim2 =
      std::make_shared<AnimationTimeline>(Path("/Animated/Prim2"));
  auto comp = CreateMockComposition(1);

  anim1->AddKeyframe(0.0, comp);
  anim2->AddKeyframe(0.0, comp);

  comp_timeline.AddTimeline(Path("/Animated/Prim1"), anim1);
  comp_timeline.AddTimeline(Path("/Animated/Prim2"), anim2);

  auto prims = comp_timeline.GetAnimatedPrims();
  EXPECT_EQ(prims.size(), 2);
}

TEST_F(CompositionTimelineTest, SetFramesPerSecond) {
  comp_timeline.SetFramesPerSecond(24.0);

  // Frame 0 = 0.0 seconds
  EXPECT_DOUBLE_EQ(comp_timeline.FrameToTime(0), 0.0);

  // Frame 24 = 1.0 seconds
  EXPECT_DOUBLE_EQ(comp_timeline.FrameToTime(24), 1.0);
}

TEST_F(CompositionTimelineTest, FrameTimeConversion) {
  comp_timeline.SetFramesPerSecond(30.0);  // 30 FPS

  // 30 frames = 1 second at 30 FPS
  TimeCode time = comp_timeline.FrameToTime(30);
  EXPECT_DOUBLE_EQ(time, 1.0);

  // Convert back
  int frame = comp_timeline.TimeToFrame(1.0);
  EXPECT_EQ(frame, 30);
}

// ============================================================================
// Time-Based Composition Evaluator Tests
// ============================================================================

class TimeBasedEvaluatorTest : public ::testing::Test {
 protected:
  std::unique_ptr<Cache> cache;
  std::unique_ptr<TimeBasedCompositionEvaluator> evaluator;

  void SetUp() override {
    CacheConfig config;
    cache = std::make_unique<Cache>(config);
    evaluator = std::make_unique<TimeBasedCompositionEvaluator>(cache.get());
  }

  std::shared_ptr<PrimIndex> CreateMockComposition(int id) {
    auto comp = std::make_shared<PrimIndex>();
    comp->SetPath(Path("/Comp_" + std::to_string(id)));
    return comp;
  }
};

TEST_F(TimeBasedEvaluatorTest, BasicEvaluation) {
  auto timeline = evaluator->GetTimeline();
  auto anim = std::make_shared<AnimationTimeline>(Path("/Animated/Cube"));
  auto comp = CreateMockComposition(1);

  anim->AddKeyframe(0.0, comp);
  timeline->AddTimeline(Path("/Animated/Cube"), anim);

  std::vector<Error> errors;
  auto result = evaluator->EvaluateAtTime(Path("/Animated/Cube"), 0.0, &errors);

  EXPECT_NE(result, nullptr);
  EXPECT_EQ(errors.size(), 0);
}

TEST_F(TimeBasedEvaluatorTest, EvaluateSequence) {
  auto timeline = evaluator->GetTimeline();
  auto anim = std::make_shared<AnimationTimeline>(Path("/Animated/Cube"));

  // Add keyframes at 0, 1, 2 seconds
  for (int i = 0; i <= 2; ++i) {
    auto comp = CreateMockComposition(i);
    anim->AddKeyframe(static_cast<double>(i), comp);
  }

  timeline->AddTimeline(Path("/Animated/Cube"), anim);

  // Evaluate sequence from 0 to 2 seconds at 0.5 second intervals
  auto sequence = evaluator->EvaluateSequence(Path("/Animated/Cube"), 0.0, 2.0,
                                               0.5);

  EXPECT_GT(sequence.size(), 0);
}

// ============================================================================
// Integration Tests
// ============================================================================

class ThreadingIntegrationTest : public ::testing::Test {
 protected:
  std::unique_ptr<ThreadPool> thread_pool;
  std::unique_ptr<Cache> cache;

  void SetUp() override {
    thread_pool = std::make_unique<ThreadPool>(4);
    CacheConfig config;
    cache = std::make_unique<Cache>(config);
  }

  void TearDown() override {
    if (thread_pool) {
      thread_pool->Shutdown();
    }
  }
};

TEST_F(ThreadingIntegrationTest, ParallelTaskExecution) {
  std::vector<int> execution_order;
  std::mutex order_mutex;

  const int task_count = 20;

  for (int i = 0; i < task_count; ++i) {
    auto task = std::make_shared<CompositionTask>(
        Path("/Prim_" + std::to_string(i)),
        [&, i](const Path&, const Error&) {
          std::lock_guard<std::mutex> lock(order_mutex);
          execution_order.push_back(i);
        });

    thread_pool->SubmitTask(task);
  }

  thread_pool->WaitAll();

  EXPECT_EQ(execution_order.size(), task_count);
  EXPECT_EQ(thread_pool->PendingTaskCount(), 0);
}

TEST_F(ThreadingIntegrationTest, ConcurrentCacheAccess) {
  auto timeline = std::make_shared<AnimationTimeline>(Path("/Animated/Prim"));

  // Create mock compositions
  std::vector<std::shared_ptr<PrimIndex>> compositions;
  for (int i = 0; i < 5; ++i) {
    auto comp = std::make_shared<PrimIndex>();
    comp->SetPath(Path("/Comp_" + std::to_string(i)));
    compositions.push_back(comp);
  }

  // Add keyframes
  for (int i = 0; i < 5; ++i) {
    timeline->AddKeyframe(static_cast<double>(i), compositions[i]);
  }

  // Access timeline from multiple threads
  std::vector<std::shared_ptr<PrimIndex>> results;
  std::mutex results_mutex;

  for (int t = 0; t < 4; ++t) {
    auto task = std::make_shared<CompositionTask>(
        Path("/Reader_" + std::to_string(t)),
        [&](const Path&, const Error&) {});

    task->callback = [&](const Path&, const Error&) {
      for (double time = 0.0; time <= 4.0; time += 0.5) {
        std::shared_ptr<PrimIndex> comp;
        if (timeline->EvaluateAt(time, comp)) {
          std::lock_guard<std::mutex> lock(results_mutex);
          results.push_back(comp);
        }
      }
    };

    thread_pool->SubmitTask(task);
  }

  thread_pool->WaitAll();

  EXPECT_GT(results.size(), 0);
}

}  // namespace pcp
}  // namespace tydra
}  // namespace tinyusdz
