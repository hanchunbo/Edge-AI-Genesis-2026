// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：线程池演示程序，展示 Submit / SubmitWithToken / WaitForAll
// 等核心接口
// ============================================================================

#include "thread_pool.hpp"

#include <format>
#include <iostream>
#include <vector>

int main() {
  std::cout << std::format("=== W5 ThreadPool 演示 ===\n\n");

  // ------------------------------------------------------------------
  // 场景 1：批量提交计算任务，通过 future 收集结果
  // ------------------------------------------------------------------
  std::cout << "【场景 1】批量提交 8 个计算任务\n";
  {
    w5::ThreadPool pool(4);

    std::vector<std::future<int>> futures;
    futures.reserve(8);

    for (int i = 0; i < 8; ++i) {
      futures.push_back(pool.Submit([i]() {
        // 模拟轻量计算
        return i * i;
      }));
    }

    for (int i = 0; i < 8; ++i) {
      std::cout << std::format("  task[{}] = {}\n", i, futures[i].get());
    }
  }  // pool 析构时自动 Shutdown + join

  // ------------------------------------------------------------------
  // 场景 2：SubmitWithToken —— 可中断的长时任务
  // ------------------------------------------------------------------
  std::cout << "\n【场景 2】可中断任务（SubmitWithToken）\n";
  {
    w5::ThreadPool pool(2);

    auto future = pool.SubmitWithToken([](std::stop_token token) -> int {
      int count = 0;
      while (!token.stop_requested()) {
        ++count;
        if (count >= 1000) break;  // 防止无限循环
      }
      return count;
    });

    // 稍后关闭线程池，触发 stop_requested
    pool.Shutdown();
    std::cout << std::format("  任务执行了 {} 次迭代后被中断\n", future.get());
  }

  // ------------------------------------------------------------------
  // 场景 3：WaitForAll —— 等待所有任务完成后再继续
  // ------------------------------------------------------------------
  std::cout << "\n【场景 3】WaitForAll 同步点\n";
  {
    w5::ThreadPool pool(4);
    std::atomic<int> counter{0};

    for (int i = 0; i < 20; ++i) {
      pool.Submit([&counter]() { ++counter; });
    }

    pool.WaitForAll();
    std::cout << std::format("  所有任务完成，counter = {}\n", counter.load());
  }

  std::cout << "\n=== 演示结束 ===\n";
  return 0;
}
