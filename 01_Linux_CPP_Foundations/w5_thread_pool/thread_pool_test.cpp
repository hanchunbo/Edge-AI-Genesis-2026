// Copyright 2026 Edge-AI-Genesis
// ThreadPool GTest 测试 - 验证并行执行、生命周期管理和 C++20 特性

#include "thread_pool.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <stop_token>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

// 模拟图像旋转任务（计算密集型）
double SimulateImageRotation(int /*image_id*/, int width, int height,
                             double angle) {
  double checksum = 0.0;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      double rad = angle * M_PI / 180.0;
      double new_x = x * std::cos(rad) - y * std::sin(rad);
      double new_y = x * std::sin(rad) + y * std::cos(rad);
      checksum += std::fmod(std::abs(new_x + new_y), 256.0);
    }
  }
  return checksum;
}

}  // namespace

// =============================================================================
// Test 1: 基础功能 - 提交任务并获取结果
// =============================================================================
TEST(ThreadPoolTest, BasicFunctionality) {
  w5::ThreadPool pool(4);
  EXPECT_EQ(pool.GetThreadCount(), 4);

  // 无参 lambda
  auto future1 = pool.Submit([]() { return 42; });
  EXPECT_EQ(future1.get(), 42);

  // 带参 lambda
  auto future2 = pool.Submit([](int a, int b) { return a + b; }, 10, 20);
  EXPECT_EQ(future2.get(), 30);

  // 字符串参数
  auto future3 = pool.Submit(
      [](const std::string& s) { return s + " World!"; },
      std::string("Hello"));
  EXPECT_EQ(future3.get(), "Hello World!");
}

// =============================================================================
// Test 2: 100 个图像旋转任务并行执行
// =============================================================================
TEST(ThreadPoolTest, ImageRotationParallel) {
  const int kNumTasks = 100;
  const int kImageWidth = 100;
  const int kImageHeight = 100;

  // 串行基准
  double serial_sum = 0.0;
  for (int i = 0; i < kNumTasks; ++i) {
    serial_sum +=
        SimulateImageRotation(i, kImageWidth, kImageHeight, i * 3.6);
  }

  // 并行执行
  size_t num_threads = std::thread::hardware_concurrency();
  if (num_threads == 0) num_threads = 4;

  w5::ThreadPool pool(num_threads);
  std::vector<std::future<double>> futures;
  futures.reserve(kNumTasks);

  for (int i = 0; i < kNumTasks; ++i) {
    futures.push_back(pool.Submit(SimulateImageRotation, i, kImageWidth,
                                  kImageHeight, i * 3.6));
  }

  double parallel_sum = 0.0;
  for (auto& future : futures) {
    parallel_sum += future.get();
  }

  // 结果一致性验证
  EXPECT_NEAR(serial_sum, parallel_sum, 1e-6);
}

// =============================================================================
// Test 3: 优雅关闭 - 析构前完成所有已提交任务
// =============================================================================
TEST(ThreadPoolTest, GracefulShutdown) {
  std::atomic<int> completed_count(0);
  const int kNumTasks = 20;

  {
    w5::ThreadPool pool(4);

    for (int i = 0; i < kNumTasks; ++i) {
      pool.Submit([&completed_count]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        ++completed_count;
      });
    }
    // 析构函数调用 Shutdown()，等待所有任务完成
  }

  EXPECT_EQ(completed_count.load(), kNumTasks);
}

// =============================================================================
// Test 4: WaitForAll - 阻塞等待所有任务完成
// =============================================================================
TEST(ThreadPoolTest, WaitForAll) {
  w5::ThreadPool pool(4);
  std::atomic<int> counter(0);
  const int kNumTasks = 50;

  for (int i = 0; i < kNumTasks; ++i) {
    pool.Submit([&counter]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      ++counter;
    });
  }

  pool.WaitForAll();
  EXPECT_EQ(counter.load(), kNumTasks);
}

// =============================================================================
// Test 5: 异常传播 - 任务异常通过 future 传递
// =============================================================================
TEST(ThreadPoolTest, ExceptionPropagation) {
  w5::ThreadPool pool(2);

  auto future = pool.Submit([]() -> int {
    throw std::runtime_error("Test exception from task");
    return 0;
  });

  EXPECT_THROW(future.get(), std::runtime_error);
}

// =============================================================================
// Test 6: stop_token 优雅中断 (C++20)
// =============================================================================
TEST(ThreadPoolTest, StopTokenInterruption) {
  w5::ThreadPool pool(2);
  std::atomic<bool> task_started(false);
  std::atomic<bool> task_interrupted(false);
  std::atomic<int> iterations_completed(0);

  auto future =
      pool.SubmitWithToken([&](std::stop_token stop_token) -> int {
        task_started = true;

        for (int i = 0; i < 1000; ++i) {
          if (stop_token.stop_requested()) {
            task_interrupted = true;
            return -1;
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
          ++iterations_completed;
        }
        return 0;
      });

  // 等待任务启动
  while (!task_started) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // 让任务运行一段时间后触发停止
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  pool.Shutdown();

  future.get();
  EXPECT_TRUE(task_interrupted.load());
  EXPECT_LT(iterations_completed.load(), 1000);
}
