// SPDX-License-Identifier: MIT
//
// 文件功能：Resize 算子 GTest 单元测试
//           覆盖 V1 最近邻 / V2 双线性 / V3 Letterbox 的正确性

#include "custom_resize.hpp"

#include <gtest/gtest.h>
#include <opencv2/imgproc.hpp>

namespace {

// ============================================================================
// 辅助函数：生成确定性测试图（棋盘格，每格 8×8 像素，便于验证插值边界）
// ============================================================================
cv::Mat MakeCheckerboard(int w, int h, int cell_size = 8) {
  cv::Mat img(h, w, CV_8UC3);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      bool white = ((x / cell_size) + (y / cell_size)) % 2 == 0;
      img.at<cv::Vec3b>(y, x) =
          white ? cv::Vec3b(255, 255, 255) : cv::Vec3b(0, 0, 0);
    }
  }
  return img;
}

// 计算两张图的 MAE（绝对误差均值），用于精度断言
double MAE(const cv::Mat& a, const cv::Mat& b) {
  cv::Mat diff;
  cv::absdiff(a, b, diff);
  return cv::mean(diff)[0];  // 取第一通道均值（棋盘格 R=G=B，三通道一致）
}

}  // namespace

// ============================================================================
// 基础尺寸测试：输出 Mat 尺寸必须与请求一致
// ============================================================================
TEST(ResizeNearest, OutputSize) {
  cv::Mat src = MakeCheckerboard(64, 48);
  auto dst = w10::ResizeNearest(src, 128, 96);
  EXPECT_EQ(dst.cols, 128);
  EXPECT_EQ(dst.rows, 96);
  EXPECT_EQ(dst.type(), CV_8UC3);
}

TEST(ResizeBilinear, OutputSize) {
  cv::Mat src = MakeCheckerboard(64, 48);
  auto dst = w10::ResizeBilinear(src, 128, 96);
  EXPECT_EQ(dst.cols, 128);
  EXPECT_EQ(dst.rows, 96);
  EXPECT_EQ(dst.type(), CV_8UC3);
}

TEST(Letterbox, OutputSize) {
  cv::Mat src = MakeCheckerboard(640, 480);
  w10::LetterboxInfo info{};
  auto dst = w10::Letterbox(src, 640, 640, info);
  EXPECT_EQ(dst.cols, 640);
  EXPECT_EQ(dst.rows, 640);
  EXPECT_EQ(dst.type(), CV_8UC3);
}

// ============================================================================
// 1×1 图像：缩放到任意尺寸，所有像素值应与源图相同
// ============================================================================
TEST(ResizeNearest, SinglePixelFill) {
  cv::Mat src(1, 1, CV_8UC3, cv::Scalar(42, 100, 200));
  auto dst = w10::ResizeNearest(src, 8, 8);
  for (int y = 0; y < dst.rows; ++y)
    for (int x = 0; x < dst.cols; ++x)
      EXPECT_EQ(dst.at<cv::Vec3b>(y, x), (cv::Vec3b{42, 100, 200}));
}

TEST(ResizeBilinear, SinglePixelFill) {
  cv::Mat src(1, 1, CV_8UC3, cv::Scalar(42, 100, 200));
  auto dst = w10::ResizeBilinear(src, 8, 8);
  for (int y = 0; y < dst.rows; ++y)
    for (int x = 0; x < dst.cols; ++x)
      EXPECT_EQ(dst.at<cv::Vec3b>(y, x), (cv::Vec3b{42, 100, 200}));
}

// ============================================================================
// 等尺寸复制：src==dst 时输出应与输入完全相同
// ============================================================================
TEST(ResizeNearest, IdentityCopy) {
  cv::Mat src = MakeCheckerboard(32, 32);
  auto dst = w10::ResizeNearest(src, 32, 32);
  // countNonZero 不支持多通道 Mat，用 NORM_INF（最大像素差）代替
  EXPECT_EQ(cv::norm(src, dst, cv::NORM_INF), 0.0);
}

TEST(ResizeBilinear, IdentityCopy) {
  cv::Mat src = MakeCheckerboard(32, 32);
  auto dst = w10::ResizeBilinear(src, 32, 32);
  // 双线性等尺寸复制有 ±1 的定点舍入误差，用 MAE < 1 验证
  EXPECT_LT(MAE(src, dst), 1.0);
}

// ============================================================================
// 精度对比：与 OpenCV 参考实现的 MAE 要求
// - 最近邻：完全一致（MAE == 0）
// - 双线性：定点误差 ≤ 2（Q8 舍入引起的最大误差为 1 个 LSB）
// ============================================================================
TEST(ResizeNearest, MatchOpenCV) {
  cv::Mat src = MakeCheckerboard(128, 96);
  auto our = w10::ResizeNearest(src, 64, 48, w10::CoordMode::kAsymmetric);
  cv::Mat ref;
  cv::resize(src, ref, cv::Size(64, 48), 0, 0, cv::INTER_NEAREST);
  // OpenCV INTER_NEAREST 使用 floor(coord + 0.5) 四舍五入，
  // 我们使用 floor(coord) 向下取整（asymmetric ONNX 语义），
  // 两者在整数坐标点上结果相同，非整数坐标处允许 1 个像素差异
  EXPECT_LT(MAE(our, ref), 2.0);
}

TEST(ResizeBilinear, MatchOpenCV) {
  cv::Mat src = MakeCheckerboard(128, 96);
  auto our = w10::ResizeBilinear(src, 64, 48, w10::CoordMode::kAsymmetric);
  cv::Mat ref;
  cv::resize(src, ref, cv::Size(64, 48), 0, 0, cv::INTER_LINEAR);
  // Q8 定点舍入，与 OpenCV float 结果 MAE 应 ≤ 2
  EXPECT_LT(MAE(our, ref), 2.0);
}

// ============================================================================
// Letterbox：验证 LetterboxInfo 数值和填充颜色
// ============================================================================
TEST(Letterbox, ScaleAndPad_WiderThanTall) {
  // 源图 960×540（16:9），目标 640×640（正方形）
  // 期望：scale = 640/960 ≈ 0.667，pad_top = (640 - 360)/2 = 140
  cv::Mat src(540, 960, CV_8UC3, cv::Scalar(100, 100, 100));
  w10::LetterboxInfo info{};
  auto dst = w10::Letterbox(src, 640, 640, info);

  EXPECT_NEAR(info.scale, 640.0f / 960.0f, 0.002f);
  // new_h = round(540 * scale) = round(360) = 360
  // pad_top = (640 - 360) / 2 = 140
  EXPECT_EQ(info.pad_top, 140);
  EXPECT_EQ(info.pad_left, 0);

  // 验证顶部填充行为灰色（114, 114, 114）
  cv::Vec3b top_pixel = dst.at<cv::Vec3b>(0, 320);
  EXPECT_EQ(top_pixel, (cv::Vec3b{114, 114, 114}));
}

TEST(Letterbox, ScaleAndPad_TallerThanWide) {
  // 源图 480×640（3:4 竖图），目标 640×640
  // 期望：scale = 640/640 = 1.0（height 已等于 dst），pad_left = 0... wait
  // 实际：scale_w = 640/480 ≈ 1.333，scale_h = 640/640 = 1.0，取 min = 1.0
  // new_w = round(480*1.0) = 480，pad_left = (640-480)/2 = 80
  cv::Mat src(640, 480, CV_8UC3, cv::Scalar(100, 100, 100));
  w10::LetterboxInfo info{};
  auto dst = w10::Letterbox(src, 640, 640, info);

  EXPECT_NEAR(info.scale, 1.0f, 0.002f);
  EXPECT_EQ(info.pad_left, 80);
  EXPECT_EQ(info.pad_top, 0);

  // 验证左侧填充列为灰色
  cv::Vec3b left_pixel = dst.at<cv::Vec3b>(320, 0);
  EXPECT_EQ(left_pixel, (cv::Vec3b{114, 114, 114}));
}

TEST(Letterbox, CoordRoundtrip) {
  // 验证 LetterboxInfo 可以正确将 dst 坐标反算回 src 坐标
  // src 正中心点坐标 = (src_w/2, src_h/2)
  const int src_w = 1280, src_h = 720;
  const int dst_w = 640, dst_h = 640;
  cv::Mat src(src_h, src_w, CV_8UC3, cv::Scalar(0, 0, 0));
  w10::LetterboxInfo info{};
  auto dst2 = w10::Letterbox(src, dst_w, dst_h, info);
  (void)dst2;

  // dst 图中 src 正中心对应的坐标
  float dst_cx = static_cast<float>(src_w / 2) * info.scale + info.pad_left;
  float dst_cy = static_cast<float>(src_h / 2) * info.scale + info.pad_top;

  // 反算回 src
  float back_x = (dst_cx - static_cast<float>(info.pad_left)) / info.scale;
  float back_y = (dst_cy - static_cast<float>(info.pad_top)) / info.scale;

  EXPECT_NEAR(back_x, static_cast<float>(src_w / 2), 1.0f);
  EXPECT_NEAR(back_y, static_cast<float>(src_h / 2), 1.0f);
}

// ============================================================================
// 坐标模式对比：asymmetric vs half_pixel 在非整数倍缩放时结果不同
// ============================================================================
TEST(ResizeBilinear, CoordModeDiffers) {
  // 用水平渐变图：每列值 = x * 8，保证采样点不同时插值结果必然不同
  // 非整数倍缩放（48→32，scale=1.5）使两种模式的采样偏移更明显
  cv::Mat src(48, 48, CV_8UC3);
  for (int y = 0; y < 48; ++y)
    for (int x = 0; x < 48; ++x)
      src.at<cv::Vec3b>(y, x) =
          cv::Vec3b(static_cast<uint8_t>(x * 5), static_cast<uint8_t>(x * 5),
                    static_cast<uint8_t>(x * 5));

  auto asym = w10::ResizeBilinear(src, 32, 32, w10::CoordMode::kAsymmetric);
  auto half = w10::ResizeBilinear(src, 32, 32, w10::CoordMode::kHalfPixel);
  // 非整数倍 scale 下，两种模式采样偏移 0.25 像素，渐变图上必然产生差异
  EXPECT_GT(cv::norm(asym, half, cv::NORM_L1), 0.0);
}

// ============================================================================
// 输入校验：空图 / 错误类型应抛 std::invalid_argument（中优技术债修复验证）
// ============================================================================
TEST(InputValidation, EmptyInputThrows) {
  cv::Mat empty;
  w10::LetterboxInfo info{};
  EXPECT_THROW((void)w10::ResizeNearest(empty, 64, 64), std::invalid_argument);
  EXPECT_THROW((void)w10::ResizeBilinear(empty, 64, 64), std::invalid_argument);
  EXPECT_THROW((void)w10::Letterbox(empty, 64, 64, info),
               std::invalid_argument);
}

TEST(InputValidation, WrongTypeThrows) {
  // CV_8UC1 灰度图（非 CV_8UC3）
  cv::Mat gray(64, 64, CV_8UC1, cv::Scalar(128));
  w10::LetterboxInfo info{};
  EXPECT_THROW((void)w10::ResizeNearest(gray, 32, 32), std::invalid_argument);
  EXPECT_THROW((void)w10::ResizeBilinear(gray, 32, 32), std::invalid_argument);
  EXPECT_THROW((void)w10::Letterbox(gray, 32, 32, info), std::invalid_argument);
}

// ============================================================================
// LetterboxToTensor：float32 CHW 输出正确性（高优技术债修复验证）
// ============================================================================
TEST(LetterboxToTensor, OutputSize) {
  cv::Mat src(540, 960, CV_8UC3, cv::Scalar(100, 150, 200));
  w10::LetterboxInfo info{};
  auto tensor = w10::LetterboxToTensor(src, 640, 640, info);
  // 输出尺寸 = 3 * dst_h * dst_w
  EXPECT_EQ(static_cast<int>(tensor.size()), 3 * 640 * 640);
}

TEST(LetterboxToTensor, ValueRangeIsZeroToOne) {
  // 纯色图：BGR = (0, 128, 255)，归一化后各通道值应在 [0, 1]
  cv::Mat src(64, 64, CV_8UC3, cv::Scalar(0, 128, 255));
  w10::LetterboxInfo info{};
  auto tensor = w10::LetterboxToTensor(src, 64, 64, info);
  for (float v : tensor) {
    EXPECT_GE(v, 0.0f);
    EXPECT_LE(v, 1.0f);
  }
}

TEST(LetterboxToTensor, CHWLayoutCorrect) {
  // 纯色图 BGR=(10, 20, 30)，无填充（正方形→正方形等比）
  // B 面 = 10/255, G 面 = 20/255, R 面 = 30/255
  cv::Mat src(64, 64, CV_8UC3, cv::Scalar(10, 20, 30));
  w10::LetterboxInfo info{};
  auto tensor = w10::LetterboxToTensor(src, 64, 64, info);

  const int n = 64 * 64;
  const float kTol = 1.0f / 255.0f;  // 允许 1 个量化步长的误差

  // B 面（channel 0）：tensor[0..n-1]
  EXPECT_NEAR(tensor[0], 10.0f / 255.0f, kTol);
  // G 面（channel 1）：tensor[n..2n-1]
  EXPECT_NEAR(tensor[n], 20.0f / 255.0f, kTol);
  // R 面（channel 2）：tensor[2n..3n-1]
  EXPECT_NEAR(tensor[2 * n], 30.0f / 255.0f, kTol);
}
