// SPDX-License-Identifier: MIT
//
// 文件功能：ThreadPool GTest 测试 - 验证并行执行、生命周期管理和 C++20 特性

#include "thread_pool.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <gtest/gtest.h>
#include <stop_token>
#include <string>
#include <vector>

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
  auto future3 = pool.Submit([](const std::string& s) { return s + " World!"; },
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
    serial_sum += SimulateImageRotation(i, kImageWidth, kImageHeight, i * 3.6);
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

  auto future = pool.SubmitWithToken([&](std::stop_token stop_token) -> int {
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

// =============================================================================
// Test 7: 批量提交 100 个任务，验证每个 future 返回正确的 index
// =============================================================================
TEST(ThreadPoolTest, SubmitMultipleTasks_AllComplete) {
  using namespace std::chrono_literals;
  w5::ThreadPool pool(4);
  const int kNumTasks = 100;

  // 提交 100 个任务，每个任务返回自身的 index
  std::vector<std::future<int>> futures;
  futures.reserve(kNumTasks);
  for (int i = 0; i < kNumTasks; ++i) {
    futures.push_back(pool.Submit([](int idx) { return idx; }, i));
  }

  // 验证每个 future 返回值与提交时的 index 完全一致
  for (int i = 0; i < kNumTasks; ++i) {
    EXPECT_EQ(futures[i].get(), i);
  }
}

// =============================================================================
// Test 8: 并行加速验证 —— 4 个 sleep 任务并行耗时须远小于串行耗时
// =============================================================================
TEST(ThreadPoolTest, ParallelSpeedup_FasterThanSerial) {
  using namespace std::chrono_literals;
  // 确保线程池至少有 4 个线程以真正并行执行
  w5::ThreadPool pool(4);

  auto start = std::chrono::steady_clock::now();

  std::vector<std::future<void>> futures;
  futures.reserve(4);
  for (int i = 0; i < 4; ++i) {
    futures.push_back(pool.Submit(
        []() { std::this_thread::sleep_for(std::chrono::milliseconds(50)); }));
  }
  for (auto& f : futures) {
    f.get();
  }

  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);

  // 4 个 50ms 任务串行需 200ms，并行应在 150ms 内完成
  EXPECT_LT(elapsed.count(), 150);
}

// =============================================================================
// Test 9: GetPendingTaskCount 随任务消费而减少
// =============================================================================
TEST(ThreadPoolTest, GetPendingTaskCount_Decreases) {
  using namespace std::chrono_literals;
  // 用互斥锁阻塞工作线程，使任务积压在队列中
  std::mutex block_mutex;
  std::condition_variable block_cv;
  bool released = false;

  // 4 线程池，先让所有线程阻塞在同一个锁上
  w5::ThreadPool pool(4);
  const int kNumBlockers = 4;  // 占满所有工作线程
  const int kNumPending = 10;  // 额外积压的任务数

  std::atomic<int> blocker_started{0};

  // 提交 kNumBlockers 个阻塞任务，占满所有工作线程
  std::vector<std::future<void>> blocker_futures;
  for (int i = 0; i < kNumBlockers; ++i) {
    blocker_futures.push_back(
        pool.Submit([&block_mutex, &block_cv, &released, &blocker_started]() {
          ++blocker_started;
          std::unique_lock<std::mutex> lk(block_mutex);
          // 等待外部释放信号
          block_cv.wait(lk, [&released] { return released; });
        }));
  }

  // 等待所有阻塞任务均已被工作线程取走并开始执行
  while (blocker_started.load() < kNumBlockers) {
    std::this_thread::sleep_for(1ms);
  }

  // 此时所有线程均被占用，继续提交 kNumPending 个额外任务
  std::vector<std::future<void>> pending_futures;
  for (int i = 0; i < kNumPending; ++i) {
    pending_futures.push_back(pool.Submit([]() {}));
  }

  // 验证队列中确实有积压任务
  EXPECT_GT(pool.GetPendingTaskCount(), 0u);

  // 释放所有阻塞任务
  {
    std::lock_guard<std::mutex> lk(block_mutex);
    released = true;
  }
  block_cv.notify_all();

  // 等待全部任务完成后，队列应为空
  pool.WaitForAll();
  EXPECT_EQ(pool.GetPendingTaskCount(), 0u);
}

// =============================================================================
// Test 10: GetActiveTaskCount 在任务执行期间 >= 1，完成后回归 0
// =============================================================================
TEST(ThreadPoolTest, GetActiveTaskCount_NonZeroDuringExecution) {
  using namespace std::chrono_literals;
  w5::ThreadPool pool(2);

  // 提交一个耗时 200ms 的任务
  auto future = pool.Submit(
      []() { std::this_thread::sleep_for(std::chrono::milliseconds(200)); });

  // 稍等 50ms 后，任务应处于执行中
  std::this_thread::sleep_for(50ms);
  EXPECT_GE(pool.GetActiveTaskCount(), 1u);

  // future.get() 在 packaged_task 内部 set_value 后即返回，此时 worker
  // 线程可能尚未执行 --active_tasks_；用 WaitForAll() 确保计数已归零
  future.get();
  pool.WaitForAll();
  EXPECT_EQ(pool.GetActiveTaskCount(), 0u);
}

// =============================================================================
// Test 11: Shutdown 触发 stop_token，循环任务在 200ms 内返回
// =============================================================================
TEST(ThreadPoolTest, StopToken_InterruptsIn100ms) {
  using namespace std::chrono_literals;
  w5::ThreadPool pool(2);

  std::atomic<bool> task_started{false};

  // 提交一个持续检查 stop_token 的可中断任务
  auto future =
      pool.SubmitWithToken([&task_started](std::stop_token token) -> int {
        task_started = true;
        // 循环直至收到停止信号
        while (!token.stop_requested()) {
          std::this_thread::sleep_for(1ms);
        }
        return -1;
      });

  // 等待任务实际开始运行
  while (!task_started.load()) {
    std::this_thread::sleep_for(1ms);
  }

  // 触发停止信号
  pool.Shutdown();

  // 验证任务在 200ms 内已完成（stop_token 中断生效）
  auto status = future.wait_for(200ms);
  EXPECT_EQ(status, std::future_status::ready);
}

// =============================================================================
// Test 12: SubmitWithToken 在 pool 未关闭时，stop_requested() 应为 false
// =============================================================================
TEST(ThreadPoolTest, SubmitWithToken_ReceivesValidToken) {
  using namespace std::chrono_literals;
  w5::ThreadPool pool(2);

  // 用 shared_ptr 包装 promise，避免 lambda 需要 mutable（SubmitWithToken
  // 内部以 const 调用函数对象，mutable lambda 会导致编译错误）
  auto p = std::make_shared<std::promise<bool>>();
  auto result_future = p->get_future();

  // 任务内立即检查 stop_token 初始状态
  pool.SubmitWithToken([p](std::stop_token token) {
    // pool 未关闭，stop_requested() 应为 false
    p->set_value(token.stop_requested());
  });

  // 取得任务内部检查的结果
  bool stop_requested_on_entry = result_future.get();
  EXPECT_FALSE(stop_requested_on_entry);
}

// =============================================================================
// Test 13: 多次调用 Shutdown() 不崩溃、不死锁（幂等验证）
// =============================================================================
TEST(ThreadPoolTest, MultipleShutdownCalls_Safe) {
  w5::ThreadPool pool(2);

  // 连续调用两次 Shutdown()，均应安全完成
  pool.Shutdown();
  EXPECT_NO_FATAL_FAILURE(pool.Shutdown());
}

// =============================================================================
// Test 14: GetThreadCount 返回值与构造参数一致
// =============================================================================
TEST(ThreadPoolTest, GetThreadCount_MatchesConstructorArg) {
  w5::ThreadPool pool(3);
  // 构造时指定 3 个线程，查询结果应精确匹配
  EXPECT_EQ(pool.GetThreadCount(), 3u);
}
