// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：W9 BGR→Gray 三版实现的正确性验证（GTest）
//   1. 单像素精确值验证（与手算定点值对比）
//   2. 三版互相一致性验证（最大差 = 0，相同算法）
//   3. 与 cv::cvtColor 的一致性验证（最大差 ≤ 2，定点精度损失）
//   4. 非连续 Mat（ROI）的降级处理验证
//   5. 输出尺寸与类型验证
// ============================================================================

#include "bgr2gray.hpp"

#include <gtest/gtest.h>
#include <opencv2/imgproc.hpp>

namespace {

// 构造指定颜色的 BGR 图（4×4 以规避极小尺寸的边界情况）
cv::Mat MakeSolidBGR(uint8_t b, uint8_t g, uint8_t r) {
  return cv::Mat(4, 4, CV_8UC3, cv::Scalar(b, g, r));
}

// 验证三版与 cvtColor 在整幅图上的最大差异
void ExpectConsistentWithCvtColor(const cv::Mat& src, int max_diff = 2) {
  cv::Mat ref;
  cv::cvtColor(src, ref, cv::COLOR_BGR2GRAY);

  auto r1 = w9::BgrToGrayV1(src);
  auto r2 = w9::BgrToGrayV2(src);
  auto r3 = w9::BgrToGrayV3(src);

  // 三版使用相同定点算法，互相误差应为 0
  EXPECT_EQ(cv::norm(r1, r2, cv::NORM_INF), 0);
  EXPECT_EQ(cv::norm(r1, r3, cv::NORM_INF), 0);
  // 与 cvtColor（浮点实现）的误差来自定点化舍入，允许 ≤ max_diff
  EXPECT_LE(cv::norm(r1, ref, cv::NORM_INF), max_diff);
}

}  // namespace

// ============================================================================
// 测试1：纯黑（B=G=R=0）→ 所有实现输出 Gray=0
// ============================================================================
TEST(W9BGR2Gray, AllBlack) {
  cv::Mat black = MakeSolidBGR(0, 0, 0);
  EXPECT_EQ(w9::BgrToGrayV1(black).at<uint8_t>(0, 0), 0);
  ExpectConsistentWithCvtColor(black);
}

// ============================================================================
// 测试2：纯白（B=G=R=255）→ Gray=255
// 定点验证：(255*29 + 255*150 + 255*77) >> 8 = (255*256) >> 8 = 255
// ============================================================================
TEST(W9BGR2Gray, AllWhite) {
  cv::Mat white = MakeSolidBGR(255, 255, 255);
  EXPECT_EQ(w9::BgrToGrayV1(white).at<uint8_t>(0, 0), 255);
  ExpectConsistentWithCvtColor(white);
}

// ============================================================================
// 测试3：纯蓝（B=255, G=0, R=0）
// 定点：(255*29) >> 8 = 7395 >> 8 = 28（蓝色感知权重最低）
// ============================================================================
TEST(W9BGR2Gray, PureBlue) {
  cv::Mat blue = MakeSolidBGR(255, 0, 0);
  EXPECT_NEAR(static_cast<int>(w9::BgrToGrayV1(blue).at<uint8_t>(0, 0)), 28, 2);
  ExpectConsistentWithCvtColor(blue);
}

// ============================================================================
// 测试4：纯绿（B=0, G=255, R=0）
// 定点：(255*150) >> 8 = 38250 >> 8 = 149（绿色感知权重最高）
// ============================================================================
TEST(W9BGR2Gray, PureGreen) {
  cv::Mat green = MakeSolidBGR(0, 255, 0);
  EXPECT_NEAR(static_cast<int>(w9::BgrToGrayV1(green).at<uint8_t>(0, 0)), 149,
              2);
  ExpectConsistentWithCvtColor(green);
}

// ============================================================================
// 测试5：纯红（B=0, G=0, R=255）
// 定点：(255*77) >> 8 = 19635 >> 8 = 76
// ============================================================================
TEST(W9BGR2Gray, PureRed) {
  cv::Mat red = MakeSolidBGR(0, 0, 255);
  EXPECT_NEAR(static_cast<int>(w9::BgrToGrayV1(red).at<uint8_t>(0, 0)), 76, 2);
  ExpectConsistentWithCvtColor(red);
}

// ============================================================================
// 测试6：三版在 1080P 梯度图上互相一致
// ============================================================================
TEST(W9BGR2Gray, AllVersionsConsistent1080P) {
  cv::Mat img(1080, 1920, CV_8UC3);
  for (int r = 0; r < img.rows; ++r) {
    for (int c = 0; c < img.cols; ++c) {
      img.at<cv::Vec3b>(r, c) = {static_cast<uint8_t>((r + c) % 256),
                                 static_cast<uint8_t>((r * 2 + c) % 256),
                                 static_cast<uint8_t>((r + c * 2) % 256)};
    }
  }
  ExpectConsistentWithCvtColor(img);
}

// ============================================================================
// 测试7：非连续 Mat（ROI 子图）的降级处理
// ROI 的 step[0] > cols*3，isContinuous() = false
// V2/V3 应降级到 V1，结果与 V1 完全一致
// ============================================================================
TEST(W9BGR2Gray, NonContinuousROI) {
  cv::Mat full(100, 200, CV_8UC3, cv::Scalar(100, 150, 200));
  cv::Mat roi = full(cv::Rect(50, 25, 100, 50));
  ASSERT_FALSE(roi.isContinuous());

  auto r1 = w9::BgrToGrayV1(roi);
  auto r2 = w9::BgrToGrayV2(roi);
  auto r3 = w9::BgrToGrayV3(roi);

  EXPECT_EQ(cv::norm(r1, r2, cv::NORM_INF), 0);
  EXPECT_EQ(cv::norm(r1, r3, cv::NORM_INF), 0);
}

// ============================================================================
// 测试8：输出尺寸与类型验证
// ============================================================================
TEST(W9BGR2Gray, OutputShape) {
  cv::Mat src(480, 640, CV_8UC3, cv::Scalar(10, 20, 30));
  auto dst = w9::BgrToGrayV1(src);
  EXPECT_EQ(dst.rows, 480);
  EXPECT_EQ(dst.cols, 640);
  EXPECT_EQ(dst.type(), CV_8UC1);
  EXPECT_EQ(dst.channels(), 1);
}
