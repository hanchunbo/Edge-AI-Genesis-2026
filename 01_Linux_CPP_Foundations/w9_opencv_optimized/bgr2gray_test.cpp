// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：W9 BGR→Gray 多版实现的正确性验证（GTest）
//   1. 单像素精确值验证（与手算定点值对比）
//   2. 三版互相一致性验证（最大差 = 0，相同算法）
//   3. 与 cv::cvtColor 的一致性验证（最大差 ≤ 2，定点精度损失）
//   4. 非连续 Mat（ROI）的降级处理验证
//   5. 输出尺寸与类型验证
//   6. [P0] 空输入抛出异常
//   7. [P0] void 重载复用预分配缓冲区
//   8. [P1] V4 AVX2 与 V1 结果一致
//   9. [P1] BgrToNormCHW 值域和 CHW 布局正确性
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

// ============================================================================
// 测试9：[P0] 空输入应抛出 std::invalid_argument
// 边缘设备摄像头断流时 src.empty()=true，必须在 API 边界被捕获
// ============================================================================
TEST(W9BGR2Gray, EmptyInputThrows) {
  cv::Mat empty_mat;
  ASSERT_TRUE(empty_mat.empty());

  // 使用 void 重载或 lambda 避免 [[nodiscard]] 警告
  cv::Mat discard;
  EXPECT_THROW(w9::BgrToGrayV1(empty_mat, discard), std::invalid_argument);
  EXPECT_THROW(w9::BgrToGrayV2(empty_mat, discard), std::invalid_argument);
  EXPECT_THROW(w9::BgrToGrayV3(empty_mat, discard), std::invalid_argument);
  EXPECT_THROW(w9::BgrToGrayV4(empty_mat, discard), std::invalid_argument);
  EXPECT_THROW((void)w9::BgrToNormCHW(empty_mat), std::invalid_argument);
}

// ============================================================================
// 测试10：[P0] 错误格式输入应抛出 std::invalid_argument
// ============================================================================
TEST(W9BGR2Gray, WrongTypeThrows) {
  cv::Mat gray_mat(4, 4, CV_8UC1, cv::Scalar(128));
  cv::Mat discard;
  EXPECT_THROW(w9::BgrToGrayV1(gray_mat, discard), std::invalid_argument);
  EXPECT_THROW(w9::BgrToGrayV2(gray_mat, discard), std::invalid_argument);
  EXPECT_THROW(w9::BgrToGrayV4(gray_mat, discard), std::invalid_argument);
}

// ============================================================================
// 测试11：[P0] void 重载在 dst 已正确分配时不重新分配内存
// 验证方式：记录 dst.data 指针，调用前后指针不变 → cv::Mat::create 是 no-op
// ============================================================================
TEST(W9BGR2Gray, VoidOverloadReuseBuffer) {
  cv::Mat src(480, 640, CV_8UC3, cv::Scalar(100, 150, 200));

  // 预分配正确尺寸的 dst
  cv::Mat dst(480, 640, CV_8UC1);
  const uchar* ptr_before = dst.data;

  w9::BgrToGrayV1(src, dst);
  EXPECT_EQ(dst.data, ptr_before) << "V1 void 重载不应重新分配已匹配的 dst";
  EXPECT_EQ(dst.rows, 480);
  EXPECT_EQ(dst.cols, 640);

  w9::BgrToGrayV2(src, dst);
  EXPECT_EQ(dst.data, ptr_before) << "V2 void 重载不应重新分配已匹配的 dst";

  w9::BgrToGrayV4(src, dst);
  EXPECT_EQ(dst.data, ptr_before) << "V4 void 重载不应重新分配已匹配的 dst";
}

// ============================================================================
// 测试12：[P1] V4（AVX2）在梯度图上与 V1 完全一致
// 同一定点算法，AVX2 仅改变计算顺序不应引入差异
// ============================================================================
TEST(W9BGR2Gray, V4ConsistentWithV1) {
  // 梯度图：全像素值各不相同，覆盖边界和中间值
  cv::Mat img(1080, 1920, CV_8UC3);
  for (int r = 0; r < img.rows; ++r) {
    for (int c = 0; c < img.cols; ++c) {
      img.at<cv::Vec3b>(r, c) = {static_cast<uint8_t>((r + c) % 256),
                                 static_cast<uint8_t>((r * 3 + c) % 256),
                                 static_cast<uint8_t>((r + c * 3) % 256)};
    }
  }
  auto r1 = w9::BgrToGrayV1(img);
  auto r4 = w9::BgrToGrayV4(img);
  EXPECT_EQ(r1.size(), r4.size());
  EXPECT_EQ(cv::norm(r1, r4, cv::NORM_INF), 0)
      << "V4 AVX2 与 V1 标量结果不一致";
}

// ============================================================================
// 测试13：[P1] V4 在像素数不是 8 的整倍数时尾部处理正确
// 奇数尺寸图像测试 AVX2 尾部标量路径
// ============================================================================
TEST(W9BGR2Gray, V4TailPixelsCorrect) {
  // 17 像素（不是 8 的整倍数，触发尾部标量路径）
  cv::Mat img(1, 17, CV_8UC3);
  for (int c = 0; c < 17; ++c) {
    img.at<cv::Vec3b>(0, c) = {static_cast<uint8_t>(c * 5),
                               static_cast<uint8_t>(c * 7 + 3),
                               static_cast<uint8_t>(c * 11 + 7)};
  }
  auto r1 = w9::BgrToGrayV1(img);
  auto r4 = w9::BgrToGrayV4(img);
  EXPECT_EQ(cv::norm(r1, r4, cv::NORM_INF), 0) << "V4 尾部像素计算错误";
}

// ============================================================================
// 测试14：[P1] BgrToNormCHW 输出值域在 [0, 1]
// ============================================================================
TEST(W9NormCHW, ValueRangeIsZeroToOne) {
  // 包含极值：纯黑、纯白、混合色
  cv::Mat src(2, 2, CV_8UC3);
  src.at<cv::Vec3b>(0, 0) = {0, 0, 0};        // 全黑
  src.at<cv::Vec3b>(0, 1) = {255, 255, 255};  // 全白
  src.at<cv::Vec3b>(1, 0) = {128, 64, 200};   // 混合
  src.at<cv::Vec3b>(1, 1) = {1, 254, 127};    // 边界值

  auto out = w9::BgrToNormCHW(src);
  ASSERT_EQ(out.size(), 4u * 3u);  // H*W*C = 4*3

  for (float v : out) {
    EXPECT_GE(v, 0.0f);
    EXPECT_LE(v, 1.0f);
  }
}

// ============================================================================
// 测试15：[P1] BgrToNormCHW CHW 布局正确性验证
// 给定像素颜色，验证 B/G/R 平面值与原始像素对应
// ============================================================================
TEST(W9NormCHW, CHWLayoutCorrect) {
  // 2×2 图像，4 个像素颜色各不相同
  cv::Mat src(2, 2, CV_8UC3);
  src.at<cv::Vec3b>(0, 0) = {100, 150, 200};  // pixel 0: B=100, G=150, R=200
  src.at<cv::Vec3b>(0, 1) = {10, 20, 30};     // pixel 1
  src.at<cv::Vec3b>(1, 0) = {50, 60, 70};     // pixel 2
  src.at<cv::Vec3b>(1, 1) = {255, 0, 128};    // pixel 3

  auto out = w9::BgrToNormCHW(src);
  // CHW layout: B[0..3], G[4..7], R[8..11]
  const float* b_plane = out.data();
  const float* g_plane = out.data() + 4;
  const float* r_plane = out.data() + 8;

  // pixel 0 验证
  EXPECT_NEAR(b_plane[0], 100.0f / 255.0f, 1e-5f);
  EXPECT_NEAR(g_plane[0], 150.0f / 255.0f, 1e-5f);
  EXPECT_NEAR(r_plane[0], 200.0f / 255.0f, 1e-5f);

  // pixel 3 边界值验证
  EXPECT_NEAR(b_plane[3], 255.0f / 255.0f, 1e-5f);
  EXPECT_NEAR(g_plane[3], 0.0f / 255.0f, 1e-5f);
  EXPECT_NEAR(r_plane[3], 128.0f / 255.0f, 1e-5f);
}

// ============================================================================
// 测试16：[P1] BgrToNormCHW 输出大小为 H*W*3
// ============================================================================
TEST(W9NormCHW, OutputSizeCorrect) {
  cv::Mat src(480, 640, CV_8UC3, cv::Scalar(10, 20, 30));
  auto out = w9::BgrToNormCHW(src);
  EXPECT_EQ(out.size(), static_cast<size_t>(480 * 640 * 3));
}
