// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：W11 调试实验室 —— 修复后的版本
//           对应 buggy_lab.cpp 中三个 Bug 的修复实现，供 GTest 验证和
//           Valgrind 验证使用。
// ============================================================================
//
// 修复对照：
//   Bug 1 修复：SimulateFrameProcessing → 改用 std::vector<uint8_t> RAII 管理
//   Bug 2 修复：TransferData → 改用 std::scoped_lock 同时锁两个
//   mutex，消除顺序依赖 Bug 3 修复：FindTarget → std::sort +
//   std::lower_bound，O(n log n)

#include "fixed_lab.hpp"

#include <algorithm>
#include <memory>
#include <chrono>
#include <cstring>
#include <format>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

namespace fixed {

// ============================================================================
// Fix 1：RAII 内存管理（std::vector 自动释放）
// ============================================================================
void SimulateFrameProcessing(int frame_count) {
  for (int i = 0; i < frame_count; ++i) {
    // [FIXED] RAII 自动释放，使用 make_unique_for_overwrite 消除了 std::vector 
    //         默认的大面积清零开销，在大张量预分配时节省大量内存带宽。
    auto buf = std::make_unique_for_overwrite<uint8_t[]>(1024 * 1024);
    std::memset(buf.get(), static_cast<int>(i % 256), 1024 * 1024);
    // buf 在此处析构，内存立即归还
  }
}

// ============================================================================
// Fix 2：std::scoped_lock 同时锁两个 mutex，消除加锁顺序依赖
// ============================================================================
std::mutex mutex_a;
std::mutex mutex_b;

void ThreadA() {
  // [FIXED] scoped_lock 内部使用死锁避免算法（std::lock），
  //         无论两锁以何种顺序初始化，都能安全获取
  std::scoped_lock lock(mutex_a, mutex_b);
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
}

void ThreadB() {
  // [FIXED] 与 ThreadA 参数顺序相同，但 scoped_lock 会自动处理
  std::scoped_lock lock(mutex_a, mutex_b);
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
}

void TransferData() {
  std::thread ta(ThreadA);
  std::thread tb(ThreadB);
  ta.join();
  tb.join();
}

// ============================================================================
// Fix 3：O(n log n) 搜索（sort + lower_bound）
// ============================================================================
std::vector<int> FindTarget(const std::vector<int>& data,
                            const std::vector<int>& targets) {
  // [FIXED] 先排序（O(n log n)），再二分搜索（O(m log n)）
  std::vector<int> sorted_data = data;
  std::sort(sorted_data.begin(), sorted_data.end());

  std::vector<int> result;
  result.reserve(targets.size());
  for (int t : targets) {
    // [FIXED] 语义更直白的二分查找，提升工程可读性
    if (std::binary_search(sorted_data.begin(), sorted_data.end(), t)) {
      result.push_back(t);
    }
  }
  return result;
}

}  // namespace fixed

// main 仅在非测试模式下编译（测试时 debugging_test.cpp 提供 main）
#ifndef W11_TEST_MODE
int main() {
  std::cout << std::format("[Fix 1] RAII 内存管理验证（Valgrind 无泄漏）\n");
  fixed::SimulateFrameProcessing(5);
  std::cout << std::format("  完成，无内存泄漏\n");

  std::cout << std::format("[Fix 2] scoped_lock 并发无死锁验证\n");
  fixed::TransferData();
  std::cout << std::format("  完成，两线程正常退出\n");

  std::cout << std::format("[Fix 3] O(n log n) 搜索验证\n");
  const int n = 50000;
  std::vector<int> data(n);
  std::vector<int> targets(n / 2);
  for (int i = 0; i < n; ++i)
    data[i] = i * 2;
  for (int i = 0; i < n / 2; ++i)
    targets[i] = i * 4;

  auto t0 = std::chrono::steady_clock::now();
  auto found = fixed::FindTarget(data, targets);
  auto t1 = std::chrono::steady_clock::now();
  double ms = static_cast<double>(
                  std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0)
                      .count()) /
              1000.0;
  std::cout << std::format("  找到 {} 个目标，耗时 {:.2f} ms\n",
                           static_cast<int>(found.size()), ms);
  return 0;
}
#endif
