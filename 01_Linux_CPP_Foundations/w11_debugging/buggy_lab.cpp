// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：W11 调试实验室 —— 包含三种故意植入的 Bug
//           本文件仅供学习 Valgrind / GDB / Perf 时复现和定位问题使用，
//           不接入 CI 自动测试。
// ============================================================================
//
// Bug 一览：
//   Bug 1 [内存泄漏]  ：SimulateFrameProcessing() 每次调用泄漏 1 MB
//   Bug 2 [死锁]      ：TransferData() 两线程以相反顺序加锁 mutex_a / mutex_b
//   Bug 3 [冗余循环]  ：FindTarget() 使用 O(n²) 朴素双重循环，可优化为 O(n log
//   n)
//
// 诊断命令（详见 notes.md）：
//   valgrind --leak-check=full ./w11_buggy_lab 1
//   gdb ./w11_buggy_lab  → run 2  → bt
//   perf stat ./w11_buggy_lab 3

#include <algorithm>
#include <chrono>
#include <cstring>
#include <format>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace buggy {

// ============================================================================
// Bug 1：内存泄漏
// ----------------------------------------------------------------------------
// 问题根因：每次调用分配 1MB，但从不释放。
//           Valgrind 报告："definitely lost: 1,048,576 bytes"
// ============================================================================
void SimulateFrameProcessing(int frame_count) {
  std::cout << std::format("[ BUG 1 ] 模拟处理 {} 帧（每帧泄漏 1 MB）...\n",
                           frame_count);
  for (int i = 0; i < frame_count; ++i) {
    // [BUGGY] 分配后直接丢弃指针，Valgrind 可检测到此泄漏
    auto* buf = new uint8_t[1024 * 1024];
    // 简单写入防止编译器优化掉分配
    std::memset(buf, static_cast<int>(i % 256), 1024 * 1024);
    // 缺少：delete[] buf;
  }
  std::cout << std::format("  处理完成（泄漏 {} MB，请用 Valgrind 检测）\n",
                           frame_count);
}

// ============================================================================
// Bug 2：死锁
// ----------------------------------------------------------------------------
// 问题根因：线程 A 先锁 mutex_a 再锁 mutex_b，
//           线程 B 先锁 mutex_b 再锁 mutex_a，
//           两线程互相持有对方需要的锁，永久阻塞。
// ============================================================================
std::mutex mutex_a;
std::mutex mutex_b;

void ThreadA() {
  std::lock_guard<std::mutex> la(mutex_a);  // 1. 先抢 a
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  std::lock_guard<std::mutex> lb(mutex_b);  // 2. 再抢 b（此时 B 持有 b）
  std::cout << std::format("  Thread A 完成\n");
}

void ThreadB() {
  std::lock_guard<std::mutex> lb(mutex_b);  // 1. 先抢 b
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  std::lock_guard<std::mutex> la(mutex_a);  // 2. 再抢 a（此时 A 持有 a）
  std::cout << std::format("  Thread B 完成\n");
}

void TransferData() {
  std::cout << std::format(
      "[ BUG 2 ] 启动两个加锁顺序相反的线程（将死锁，Ctrl+C 中断）...\n");
  std::thread ta(ThreadA);
  std::thread tb(ThreadB);
  ta.join();
  tb.join();
}

// ============================================================================
// Bug 3：冗余 O(n²) 循环
// ----------------------------------------------------------------------------
// 问题根因：外层枚举所有元素，内层线性搜索 target，整体 O(n²)。
//           "Perf" / "time" 可直观看到耗时随 n 平方增长。
// ============================================================================
std::vector<int> FindTarget(const std::vector<int>& data,
                            const std::vector<int>& targets) {
  std::vector<int> result;
  // [BUGGY] 双重循环，O(n * m)
  for (int t : targets) {
    for (int d : data) {
      if (d == t) {
        result.push_back(t);
        break;
      }
    }
  }
  return result;
}

void BenchmarkSearch(int n) {
  std::cout << std::format("[ BUG 3 ] O(n²) 搜索，n={} ...\n", n);
  std::vector<int> data(n);
  std::vector<int> targets(n / 2);
  for (int i = 0; i < n; ++i)
    data[i] = i * 2;
  for (int i = 0; i < n / 2; ++i)
    targets[i] = i * 4;

  auto t0 = std::chrono::steady_clock::now();
  auto found = FindTarget(data, targets);
  auto t1 = std::chrono::steady_clock::now();
  double ms = static_cast<double>(
                  std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0)
                      .count()) /
              1000.0;
  std::cout << std::format("  找到 {} 个目标，耗时 {:.2f} ms\n",
                           static_cast<int>(found.size()), ms);
}

}  // namespace buggy

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::cout << std::format(
        "用法: {} <bug_id>\n"
        "  1 = 内存泄漏（Valgrind 检测）\n"
        "  2 = 死锁（GDB/timeout 复现）\n"
        "  3 = O(n²) 慢搜索（Perf/time 对比）\n",
        argv[0]);
    return 1;
  }

  int bug_id = std::stoi(argv[1]);
  switch (bug_id) {
    case 1:
      buggy::SimulateFrameProcessing(5);
      break;
    case 2:
      buggy::TransferData();
      break;
    case 3:
      buggy::BenchmarkSearch(50000);
      break;
    default:
      throw std::invalid_argument(std::format("未知 bug_id: {}", bug_id));
  }
  return 0;
}
