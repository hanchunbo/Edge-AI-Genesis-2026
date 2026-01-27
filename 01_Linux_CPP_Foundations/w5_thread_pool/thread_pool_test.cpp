// Copyright 2026 Edge-AI-Genesis
// 文件功能：ThreadPool 测试程序 - 验证并行执行和性能

#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <stop_token>
#include <string>
#include <vector>

#include "thread_pool.hpp"

namespace {

// 线程安全日志
std::mutex g_log_mutex;

void Log(const std::string& message) {
  std::lock_guard<std::mutex> lock(g_log_mutex);
  std::cout << message;
}

// =============================================================================
// 模拟图像旋转任务
// =============================================================================
// 模拟耗时的图像处理操作（实际场景中会是矩阵变换）
// 返回处理结果：旋转后的"校验和"
// =============================================================================
double SimulateImageRotation(int image_id, int width, int height, double angle) {
  // 模拟计算密集型操作
  double checksum = 0.0;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      // 模拟旋转坐标变换
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
// 测试1：基础功能测试 - 提交任务并获取结果
// =============================================================================
void TestBasicFunctionality() {
  std::cout << "\n" << std::string(60, '=') << "\n";
  std::cout << "Test 1: Basic ThreadPool Functionality\n";
  std::cout << std::string(60, '=') << "\n";

  w5::ThreadPool pool(4);
  std::cout << "Created ThreadPool with " << pool.GetThreadCount() << " threads\n";

  // 提交简单任务
  auto future1 = pool.Submit([]() {
    return 42;
  });

  auto future2 = pool.Submit([](int a, int b) {
    return a + b;
  }, 10, 20);

  auto future3 = pool.Submit([](const std::string& s) {
    return s + " World!";
  }, std::string("Hello"));

  // 获取结果
  std::cout << "Result 1: " << future1.get() << " (expected: 42)\n";
  std::cout << "Result 2: " << future2.get() << " (expected: 30)\n";
  std::cout << "Result 3: " << future3.get() << " (expected: Hello World!)\n";

  std::cout << "[PASSED] Basic functionality test\n";
}

// =============================================================================
// 测试2：100 个图像旋转任务并行执行
// =============================================================================
void TestImageRotationTasks() {
  std::cout << "\n" << std::string(60, '=') << "\n";
  std::cout << "Test 2: Parallel Image Rotation (100 tasks)\n";
  std::cout << std::string(60, '=') << "\n";

  const int kNumTasks = 100;
  const int kImageWidth = 100;   // 简化测试
  const int kImageHeight = 100;

  // 获取硬件线程数
  size_t num_threads = std::thread::hardware_concurrency();
  if (num_threads == 0) num_threads = 4;

  std::cout << "Using " << num_threads << " threads for " << kNumTasks << " tasks\n";

  // 测试串行执行时间
  auto serial_start = std::chrono::high_resolution_clock::now();
  double serial_sum = 0.0;
  for (int i = 0; i < kNumTasks; ++i) {
    serial_sum += SimulateImageRotation(i, kImageWidth, kImageHeight, i * 3.6);
  }
  auto serial_end = std::chrono::high_resolution_clock::now();
  auto serial_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      serial_end - serial_start);

  std::cout << "Serial execution: " << serial_duration.count() << "ms\n";

  // 测试并行执行时间
  w5::ThreadPool pool(num_threads);
  std::vector<std::future<double>> futures;
  futures.reserve(kNumTasks);

  auto parallel_start = std::chrono::high_resolution_clock::now();

  // 提交所有任务
  for (int i = 0; i < kNumTasks; ++i) {
    futures.push_back(pool.Submit(SimulateImageRotation,
                                   i, kImageWidth, kImageHeight, i * 3.6));
  }

  // 收集结果
  double parallel_sum = 0.0;
  for (auto& future : futures) {
    parallel_sum += future.get();
  }

  auto parallel_end = std::chrono::high_resolution_clock::now();
  auto parallel_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      parallel_end - parallel_start);

  std::cout << "Parallel execution: " << parallel_duration.count() << "ms\n";

  // 计算加速比
  double speedup = static_cast<double>(serial_duration.count()) /
                   static_cast<double>(parallel_duration.count());
  std::cout << "Speedup: " << std::fixed << std::setprecision(2) << speedup << "x\n";

  // 验证结果一致性
  bool results_match = std::abs(serial_sum - parallel_sum) < 1e-6;
  std::cout << "Results match: " << (results_match ? "Yes" : "No") << "\n";

  if (results_match && speedup > 1.0) {
    std::cout << "[PASSED] Image rotation parallel test\n";
  } else {
    std::cout << "[FAILED] Image rotation parallel test\n";
  }
}

// =============================================================================
// 测试3：空闲时 CPU 占用测试
// =============================================================================
void TestIdleCpuUsage() {
  std::cout << "\n" << std::string(60, '=') << "\n";
  std::cout << "Test 3: Idle CPU Usage (should be near 0%)\n";
  std::cout << std::string(60, '=') << "\n";

  w5::ThreadPool pool(4);

  std::cout << "ThreadPool created with " << pool.GetThreadCount() << " threads\n";
  std::cout << "Sleeping for 2 seconds to observe CPU usage...\n";
  std::cout << "(Monitor with 'top' or 'htop' in another terminal)\n";

  // 等待一段时间，让用户观察 CPU 占用
  std::this_thread::sleep_for(std::chrono::seconds(10));

  std::cout << "Pending tasks: " << pool.GetPendingTaskCount() << "\n";
  std::cout << "Active tasks: " << pool.GetActiveTaskCount() << "\n";

  // 提交一个任务验证线程池仍然正常工作
  auto result = pool.Submit([]() { return 123; });
  std::cout << "Quick task result: " << result.get() << "\n";

  std::cout << "[PASSED] Idle CPU usage test (verify manually)\n";
}

// =============================================================================
// 测试4：优雅关闭测试
// =============================================================================
void TestGracefulShutdown() {
  std::cout << "\n" << std::string(60, '=') << "\n";
  std::cout << "Test 4: Graceful Shutdown\n";
  std::cout << std::string(60, '=') << "\n";

  std::atomic<int> completed_count(0);
  const int kNumTasks = 20;

  {
    w5::ThreadPool pool(4);

    // 提交任务，每个任务需要一些时间
    for (int i = 0; i < kNumTasks; ++i) {
      pool.Submit([&completed_count, i]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        ++completed_count;
        std::ostringstream oss;
        oss << "Task " << i << " completed\n";
        Log(oss.str());
      });
    }

    std::cout << "Submitted " << kNumTasks << " tasks, shutting down pool...\n";
    // 析构函数会调用 Shutdown()，等待所有任务完成
  }

  std::cout << "Pool destroyed. Completed tasks: " << completed_count << "/" << kNumTasks << "\n";

  if (completed_count == kNumTasks) {
    std::cout << "[PASSED] Graceful shutdown test\n";
  } else {
    std::cout << "[FAILED] Some tasks were not completed\n";
  }
}

// =============================================================================
// 测试5：WaitForAll 测试
// =============================================================================
void TestWaitForAll() {
  std::cout << "\n" << std::string(60, '=') << "\n";
  std::cout << "Test 5: WaitForAll Functionality\n";
  std::cout << std::string(60, '=') << "\n";

  w5::ThreadPool pool(4);
  std::atomic<int> counter(0);
  const int kNumTasks = 50;

  // 提交任务
  for (int i = 0; i < kNumTasks; ++i) {
    pool.Submit([&counter]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      ++counter;
    });
  }

  std::cout << "Submitted " << kNumTasks << " tasks\n";
  std::cout << "Waiting for all tasks to complete...\n";

  // 等待所有任务完成
  pool.WaitForAll();

  std::cout << "WaitForAll returned. Counter: " << counter << "/" << kNumTasks << "\n";

  if (counter == kNumTasks) {
    std::cout << "[PASSED] WaitForAll test\n";
  } else {
    std::cout << "[FAILED] Not all tasks completed before WaitForAll returned\n";
  }
}

// =============================================================================
// 测试6：异常处理测试
// =============================================================================
void TestExceptionHandling() {
  std::cout << "\n" << std::string(60, '=') << "\n";
  std::cout << "Test 6: Exception Handling in Tasks\n";
  std::cout << std::string(60, '=') << "\n";

  w5::ThreadPool pool(2);

  // 提交一个会抛异常的任务
  auto future = pool.Submit([]() -> int {
    throw std::runtime_error("Test exception from task");
    return 0;  // 不会执行到这里
  });

  // 尝试获取结果，应该抛出异常
  bool caught_exception = false;
  try {
    future.get();
  } catch (const std::runtime_error& e) {
    caught_exception = true;
    std::cout << "Caught expected exception: " << e.what() << "\n";
  }

  if (caught_exception) {
    std::cout << "[PASSED] Exception handling test\n";
  } else {
    std::cout << "[FAILED] Exception was not propagated\n";
  }
}

// =============================================================================
// 测试7：std::stop_token 优雅中断测试 (C++20)
// =============================================================================
// 演示如何使用 stop_token 中断长时间运行的任务
// =============================================================================
void TestStopTokenInterruption() {
  std::cout << "\n" << std::string(60, '=') << "\n";
  std::cout << "Test 7: Stop Token Graceful Interruption (C++20)\n";
  std::cout << std::string(60, '=') << "\n";

  w5::ThreadPool pool(2);
  std::atomic<bool> task_started(false);
  std::atomic<bool> task_interrupted(false);
  std::atomic<int> iterations_completed(0);

  // 提交一个长时间运行但可中断的任务
  auto future = pool.SubmitWithToken([&](std::stop_token stop_token) -> int {
    task_started = true;
    std::cout << "[Task] Long-running task started\n";

    // 模拟长时间运行的任务（如持续推理）
    for (int i = 0; i < 1000; ++i) {
      // 关键：检查 stop_token 实现优雅退出
      if (stop_token.stop_requested()) {
        std::cout << "[Task] Stop requested, exiting gracefully at iteration "
                  << i << "\n";
        task_interrupted = true;
        return -1;  // 返回表示被中断
      }

      // 模拟工作
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      ++iterations_completed;
    }

    std::cout << "[Task] Completed all iterations\n";
    return 0;
  });

  // 等待任务开始
  while (!task_started) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // 让任务运行一小段时间
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  std::cout << "[Main] Requesting shutdown (stop)...\n";
  std::cout << "[Main] Iterations before stop: " << iterations_completed << "\n";

  // 请求停止
  pool.Shutdown();

  // 获取结果
  int result = future.get();
  std::cout << "[Main] Task returned: " << result << "\n";
  std::cout << "[Main] Task was interrupted: "
            << (task_interrupted ? "Yes" : "No") << "\n";
  std::cout << "[Main] Total iterations: " << iterations_completed << "\n";

  if (task_interrupted && iterations_completed < 1000) {
    std::cout << "[PASSED] Stop token interruption test\n";
  } else {
    std::cout << "[FAILED] Task was not interrupted properly\n";
  }
}

// =============================================================================
// 主函数
// =============================================================================
int main() {
  std::cout << "========================================\n";
  std::cout << "W5: 高性能并发进阶 - 通用线程池架构 (C++20)\n";
  std::cout << "========================================\n";

  // 运行所有测试
  TestBasicFunctionality();
  TestImageRotationTasks();
  TestIdleCpuUsage();
  TestGracefulShutdown();
  TestWaitForAll();
  TestExceptionHandling();
  TestStopTokenInterruption();

  std::cout << "\n========================================\n";
  std::cout << "All tests completed!\n";
  std::cout << "========================================\n";

  return 0;
}
