// SPDX-License-Identifier: MIT
//
// 文件功能：INT8 与 FP32 检测一致性测试 —— 防止 PTQ
// 产出"能跑但检测坍缩"的模型。
//
// 背景：首版 INT8 模型对整图量化（含检测头），输出张量混合框坐标（0~640）与
// 类别分数（0~1），per-tensor scale≈2.5 令全部分数坍缩为 0（0 检测框）。
// 本测试把"INT8 检测结果必须与 FP32 框级对齐"固化为回归约束。

#include "yolo_detector.hpp"

#include <algorithm>
#include <filesystem>
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace {

// FP32 高置信框必须在 INT8 结果中找到同类别、高 IoU 的对应框。
constexpr float kStrongScore = 0.5f;    // 只强约束 FP32 的高置信框
constexpr float kMinMatchIou = 0.7f;    // 匹配框的最小 IoU
constexpr float kMaxScoreDiff = 0.15f;  // 匹配框允许的分数偏差

void SkipIfMissing(const std::string& path, const char* what) {
  if (!std::filesystem::exists(path)) {
    GTEST_SKIP() << "缺少" << what << "：" << path;
  }
}

[[nodiscard]] float Iou(const w16::Detection& a, const w16::Detection& b) {
  const float ix1 = std::max(a.x1, b.x1);
  const float iy1 = std::max(a.y1, b.y1);
  const float ix2 = std::min(a.x2, b.x2);
  const float iy2 = std::min(a.y2, b.y2);
  const float iw = std::max(0.0f, ix2 - ix1);
  const float ih = std::max(0.0f, iy2 - iy1);
  const float inter = iw * ih;
  const float area_a = (a.x2 - a.x1) * (a.y2 - a.y1);
  const float area_b = (b.x2 - b.x1) * (b.y2 - b.y1);
  const float uni = area_a + area_b - inter;
  return uni > 0.0f ? inter / uni : 0.0f;
}

[[nodiscard]] std::vector<w16::Detection> DetectCpu(
    const std::string& model_path) {
  w16::YOLODetector detector(model_path,
                             w16::DetectorConfig{.ep = w14::Ep::kCpu});
  return detector.Detect(std::string(QUANT_W16_IMAGE_PATH));
}

void ExpectInt8MatchesFp32(const std::string& int8_model_path) {
  SkipIfMissing(QUANT_W16_MODEL_PATH, "FP32 模型");
  SkipIfMissing(QUANT_W16_IMAGE_PATH, "测试图");
  SkipIfMissing(int8_model_path, "INT8 模型（先跑 quant_yolov8_static 生成）");

  const std::vector<w16::Detection> fp32 = DetectCpu(QUANT_W16_MODEL_PATH);
  const std::vector<w16::Detection> int8 = DetectCpu(int8_model_path);

  ASSERT_FALSE(fp32.empty()) << "FP32 基线在测试图上应有检测框";
  // 核心回归点：检测坍缩时这里直接失败（首版 INT8 模型输出 0 框）。
  ASSERT_FALSE(int8.empty()) << "INT8 模型输出 0 检测框（检测坍缩）";

  for (const w16::Detection& ref : fp32) {
    if (ref.score < kStrongScore) {
      continue;  // 低置信框允许 PTQ 边界抖动，不做强约束
    }
    const auto match = std::find_if(int8.begin(), int8.end(),
                                    [&ref](const w16::Detection& cand) {
                                      return cand.class_id == ref.class_id &&
                                             Iou(cand, ref) >= kMinMatchIou;
                                    });
    ASSERT_NE(match, int8.end())
        << "FP32 高置信框（class=" << ref.class_id << " score=" << ref.score
        << "）在 INT8 结果中无 IoU>=" << kMinMatchIou << " 的同类匹配";
    EXPECT_NEAR(match->score, ref.score, kMaxScoreDiff)
        << "匹配框分数偏差超过容差（class=" << ref.class_id << "）";
  }
}

TEST(QuantInt8Consistency, MinMaxModelMatchesFp32) {
  ExpectInt8MatchesFp32(QUANT_INT8_MINMAX_PATH);
}

TEST(QuantInt8Consistency, EntropyModelMatchesFp32) {
  ExpectInt8MatchesFp32(QUANT_INT8_ENTROPY_PATH);
}

}  // namespace
