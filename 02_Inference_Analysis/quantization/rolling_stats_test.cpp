// SPDX-License-Identifier: MIT
//
// 文件功能：quant RollingStats 单元测试 —— 固定窗口 P50/P99 延迟统计。

#include "rolling_stats.hpp"

#include <gtest/gtest.h>
#include <stdexcept>

namespace {

TEST(QuantRollingStats, EmptyWindowReturnsZero) {
  quant::RollingStats stats;

  const quant::StageLatencyStats summary = stats.Summary();

  EXPECT_EQ(summary.count, 0u);
  EXPECT_DOUBLE_EQ(summary.pre.p50, 0.0);
  EXPECT_DOUBLE_EQ(summary.pre.p99, 0.0);
  EXPECT_DOUBLE_EQ(summary.infer.p50, 0.0);
  EXPECT_DOUBLE_EQ(summary.post.p99, 0.0);
  EXPECT_DOUBLE_EQ(summary.total.p50, 0.0);
}

TEST(QuantRollingStats, ComputesP50AndP99PerStage) {
  quant::RollingStats stats(/*window_size=*/8);
  stats.Add(quant::StageLatencyMs{
      .pre = 1.0, .infer = 10.0, .post = 2.0, .total = 13.0});
  stats.Add(quant::StageLatencyMs{
      .pre = 2.0, .infer = 20.0, .post = 3.0, .total = 25.0});
  stats.Add(quant::StageLatencyMs{
      .pre = 3.0, .infer = 30.0, .post = 4.0, .total = 37.0});
  stats.Add(quant::StageLatencyMs{
      .pre = 4.0, .infer = 40.0, .post = 5.0, .total = 49.0});

  const quant::StageLatencyStats summary = stats.Summary();

  EXPECT_EQ(summary.count, 4u);
  EXPECT_DOUBLE_EQ(summary.pre.p50, 2.0);
  EXPECT_DOUBLE_EQ(summary.pre.p99, 4.0);
  EXPECT_DOUBLE_EQ(summary.infer.p50, 20.0);
  EXPECT_DOUBLE_EQ(summary.infer.p99, 40.0);
  EXPECT_DOUBLE_EQ(summary.total.p50, 25.0);
  EXPECT_DOUBLE_EQ(summary.total.p99, 49.0);
}

TEST(QuantRollingStats, FixedWindowEvictsOldestSamples) {
  quant::RollingStats stats(/*window_size=*/3);
  stats.Add(quant::StageLatencyMs{.total = 1.0});
  stats.Add(quant::StageLatencyMs{.total = 2.0});
  stats.Add(quant::StageLatencyMs{.total = 3.0});
  stats.Add(quant::StageLatencyMs{.total = 4.0});

  const quant::StageLatencyStats summary = stats.Summary();

  EXPECT_EQ(summary.count, 3u);
  EXPECT_DOUBLE_EQ(summary.total.p50, 3.0);
  EXPECT_DOUBLE_EQ(summary.total.p99, 4.0);
}

TEST(QuantRollingStats, ThrowsOnZeroWindowSize) {
  EXPECT_THROW(static_cast<void>(quant::RollingStats(/*window_size=*/0)),
               std::invalid_argument);
}

}  // namespace
