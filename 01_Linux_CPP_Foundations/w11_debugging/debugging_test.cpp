// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：W11 调试实验室 GTest 单元测试
//           验证三个 Bug 的修复版本行为正确
// ============================================================================

#include "fixed_lab.hpp"

#include <algorithm>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

// ============================================================================
// Fix 1：内存管理 —— 接口调用不崩溃，Valgrind 另行验证无泄漏
// ============================================================================
TEST(FixedLab, MemoryNoLeak) {
  // 调用 5 次不抛异常即为通过；Valgrind 在 CI 外单独验证无泄漏
  EXPECT_NO_THROW(fixed::SimulateFrameProcessing(5));
}

// ============================================================================
// Fix 2：并发无死锁 —— 两线程应在合理时间内完成
// ============================================================================
TEST(FixedLab, NoDeadlock) {
  auto t0 = std::chrono::steady_clock::now();
  EXPECT_NO_THROW(fixed::TransferData());
  auto t1 = std::chrono::steady_clock::now();

  // 两线程各 sleep 5ms，总耗时应在 500ms 内（留足裕量）
  auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
  EXPECT_LT(elapsed_ms, 500) << "TransferData 耗时过长，疑似死锁";
}

// ============================================================================
// Fix 3：搜索正确性 —— 与 O(n²) 暴力结果完全一致
// ============================================================================
TEST(FixedLab, SearchCorrect) {
  // 构造数据：偶数集合 data，目标为 4 的倍数（data 的子集）
  const int n = 1000;
  std::vector<int> data(n);
  std::vector<int> targets(n / 4);
  for (int i = 0; i < n; ++i)
    data[i] = i * 2;
  for (int i = 0; i < n / 4; ++i)
    targets[i] = i * 8;

  auto result = fixed::FindTarget(data, targets);

  // 验证每个 target 都被找到（均为 data 的子集）
  EXPECT_EQ(static_cast<int>(result.size()), n / 4);
  for (int t : targets) {
    EXPECT_NE(std::find(result.begin(), result.end(), t), result.end())
        << "未找到目标值: " << t;
  }
}

TEST(FixedLab, SearchMissingTarget) {
  // 目标不在数据集中，结果应为空
  std::vector<int> data = {2, 4, 6, 8, 10};
  std::vector<int> targets = {1, 3, 5};  // 全是奇数，不在 data 里

  auto result = fixed::FindTarget(data, targets);
  EXPECT_TRUE(result.empty());
}

TEST(FixedLab, SearchPerformance) {
  // O(n log n) 大数据量应在 50ms 内完成
  const int n = 100000;
  std::vector<int> data(n);
  std::vector<int> targets(n / 2);
  for (int i = 0; i < n; ++i)
    data[i] = i * 2;
  for (int i = 0; i < n / 2; ++i)
    targets[i] = i * 4;

  auto t0 = std::chrono::steady_clock::now();
  auto result = fixed::FindTarget(data, targets);
  auto t1 = std::chrono::steady_clock::now();

  auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
  EXPECT_LT(elapsed_ms, 50) << "搜索耗时 " << elapsed_ms << " ms，性能不达标";
  EXPECT_EQ(static_cast<int>(result.size()), n / 2);
}
