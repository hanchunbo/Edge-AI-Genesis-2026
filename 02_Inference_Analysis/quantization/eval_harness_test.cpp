// SPDX-License-Identifier: MIT
//
// 文件功能：quant EvalHarness 单元测试 —— 多模型评估封装与滚动统计。

#include "eval_harness.hpp"

#include <filesystem>
#include <gtest/gtest.h>
#include <vector>

namespace {

/// 缺模型或测试图时跳过用例——CI 与裸克隆环境没有这些产物。
void SkipIfAssetsMissing() {
  if (!std::filesystem::exists(QUANT_W16_MODEL_PATH) ||
      !std::filesystem::exists(QUANT_W16_IMAGE_PATH)) {
    GTEST_SKIP() << "缺少 YOLOv8n 模型或测试图";
  }
}

TEST(QuantEvalHarness, Fp32CaseRunsAndRecordsIters) {
  SkipIfAssetsMissing();

  quant::EvalConfig cfg{
      .detector = w16::DetectorConfig{.ep = w14::Ep::kCpu},
      .warmup = 1,
      .iters = 2,
      .stats_window = 8,
  };
  quant::EvalHarness harness(cfg);

  const std::vector<quant::ModelResult> results = harness.Run(
      {quant::ModelCase{.name = "fp32", .model_path = QUANT_W16_MODEL_PATH}},
      QUANT_W16_IMAGE_PATH);

  ASSERT_EQ(results.size(), 1u);
  EXPECT_EQ(results[0].name, "fp32");
  EXPECT_EQ(results[0].latency.count, 2u);
  EXPECT_EQ(results[0].active_ep, w14::Ep::kCpu);
  EXPECT_GT(results[0].detection_count, 0u);
  EXPECT_GT(results[0].top_score, 0.0f);
}

TEST(QuantEvalHarness, CudaFallbackReasonReadable) {
  SkipIfAssetsMissing();

  quant::EvalConfig cfg{
      .detector = w16::DetectorConfig{.ep = w14::Ep::kCuda},
      .warmup = 0,
      .iters = 1,
      .stats_window = 4,
  };
  quant::EvalHarness harness(cfg);

  const std::vector<quant::ModelResult> results = harness.Run(
      {quant::ModelCase{.name = "cuda", .model_path = QUANT_W16_MODEL_PATH}},
      QUANT_W16_IMAGE_PATH);

  ASSERT_EQ(results.size(), 1u);
  if (results[0].active_ep == w14::Ep::kCuda) {
    GTEST_SKIP() << "当前环境 CUDA EP 可用，不触发回退 reason";
  }
  EXPECT_FALSE(results[0].ep_fallback_reason.empty());
}

}  // namespace
