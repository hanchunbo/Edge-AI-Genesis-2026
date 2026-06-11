// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：W15 预处理单元测试（纯函数，无需模型）。
// ============================================================================

#include "preprocess.hpp"

#include <gtest/gtest.h>
#include <opencv2/core.hpp>

namespace {

// 纯色图经 resize/crop 后颜色不变，便于手算归一化期望值校验。
TEST(W15Preprocess, ShapeAndNormalizationOnSolidImage) {
  // BGR 纯色 (B=50, G=100, R=200)，尺寸大于 crop，确保有效区域全是该色。
  cv::Mat bgr(300, 300, CV_8UC3, cv::Scalar(50, 100, 200));
  w15::PreprocConfig cfg;  // 默认 224 crop + ImageNet 参数 + to_rgb

  std::vector<float> out = w15::Preprocess(bgr, cfg);

  // 尺寸：3 * 224 * 224
  ASSERT_EQ(out.size(), static_cast<size_t>(3) * 224 * 224);

  const size_t hw = static_cast<size_t>(224) * 224;
  // 通道顺序 RGB：c0=R(200), c1=G(100), c2=B(50)
  const float r = (200.0f / 255.0f - 0.485f) / 0.229f;  // ≈ 1.307
  const float g = (100.0f / 255.0f - 0.456f) / 0.224f;  // ≈ -0.285
  const float b = (50.0f / 255.0f - 0.406f) / 0.225f;   // ≈ -0.933

  EXPECT_NEAR(out[0 * hw + 0], r, 1e-3);
  EXPECT_NEAR(out[1 * hw + 0], g, 1e-3);
  EXPECT_NEAR(out[2 * hw + 0], b, 1e-3);
  // 纯色图任意像素都应相同
  EXPECT_NEAR(out[0 * hw + 12345], r, 1e-3);
}

// 非正方形输入也应输出正确尺寸（center-crop 生效）。
TEST(W15Preprocess, NonSquareInputProducesCorrectSize) {
  cv::Mat bgr(480, 640, CV_8UC3, cv::Scalar(0, 0, 0));
  w15::PreprocConfig cfg;
  std::vector<float> out = w15::Preprocess(bgr, cfg);
  EXPECT_EQ(out.size(), static_cast<size_t>(3) * 224 * 224);
}

}  // namespace
