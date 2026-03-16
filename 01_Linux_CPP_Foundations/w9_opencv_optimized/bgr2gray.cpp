// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：BGR→Gray 三版实现（无 main，编译为静态库供 bench 和 test 链接）
//   版本1：双层循环 + .ptr<T>()  — 展示 step/stride 内存布局
//   版本2：isContinuous() 单层展开 — 连续内存优化
//   版本3：std::mdspan (C++23)    — 统一多维视图抽象
// ============================================================================

// [Legacy C++11/17]: 直接用二维下标 mat.at<uchar>(r, c*3+ch) 访问像素，
//                   每次调用都有边界检查开销，且无法表达 HWC/CHW 内存语义。
// [Pain Point]: 行指针需要手动计算 step 偏移；代码与 AI 张量访问模式不统一。
// [Modern C++23]: std::mdspan 提供零开销多维视图，统一图像/张量布局描述。

#include "bgr2gray.hpp"

#include "mdspan_compat.hpp"

#include <cstdint>
#include <opencv2/core.hpp>
#include <span>
#include <stdexcept>

namespace w9 {

// BGR→Gray 权重（ITU-R BT.601 标准）
// 用整数定点化避免浮点乘法：B*29 + G*150 + R*77，再右移 8 位
// 精度与浮点版 (0.114, 0.587, 0.299) 差异 < 1 灰度级
static constexpr int kWeightB = 29;   // 0.114 * 256 ≈ 29
static constexpr int kWeightG = 150;  // 0.587 * 256 ≈ 150
static constexpr int kWeightR = 77;   // 0.299 * 256 ≈ 77

// ============================================================================
// 版本1：双层循环 + .ptr<T>()
// 目的：演示 cv::Mat 的 step/stride 内存布局。
// 关键：src.step[0] 是行字节宽，不等于 cols*3（内存对齐 padding 可能存在）
// ============================================================================
[[nodiscard]] cv::Mat BgrToGrayV1(const cv::Mat& src) {
  if (src.type() != CV_8UC3) {
    throw std::invalid_argument("输入必须是 CV_8UC3 格式");
  }
  cv::Mat dst(src.rows, src.cols, CV_8UC1);
  for (int r = 0; r < src.rows; ++r) {
    // .ptr<uint8_t>(r) 内部用 data + r * step[0]，正确处理了行 padding
    const uint8_t* src_row = src.ptr<uint8_t>(r);
    uint8_t* dst_row = dst.ptr<uint8_t>(r);
    for (int c = 0; c < src.cols; ++c) {
      // BGR 三通道按 3 字节连续存储（B在低地址）
      const int b = src_row[c * 3 + 0];
      const int g = src_row[c * 3 + 1];
      const int r_ch = src_row[c * 3 + 2];
      dst_row[c] = static_cast<uint8_t>(
          (b * kWeightB + g * kWeightG + r_ch * kWeightR) >> 8);
    }
  }
  return dst;
}

// ============================================================================
// 版本2：isContinuous() 单层展开
// 目的：当图像内存连续时（无 padding），展平成 1D 操作。
// 收益：减少行指针解引用次数，编译器更容易向量化。
// 降级：非连续时退回版本1，保证正确性。
// ============================================================================
[[nodiscard]] cv::Mat BgrToGrayV2(const cv::Mat& src) {
  if (src.type() != CV_8UC3) {
    throw std::invalid_argument("输入必须是 CV_8UC3 格式");
  }
  cv::Mat dst(src.rows, src.cols, CV_8UC1);

  if (src.isContinuous() && dst.isContinuous()) {
    // 连续内存：将整个图像展平为 1D span，一次性遍历
    const int total = src.rows * src.cols;
    std::span<const uint8_t> src_flat(src.data, static_cast<size_t>(total) * 3);
    std::span<uint8_t> dst_flat(dst.data, static_cast<size_t>(total));
    for (int i = 0; i < total; ++i) {
      const int b = src_flat[static_cast<size_t>(i) * 3 + 0];
      const int g = src_flat[static_cast<size_t>(i) * 3 + 1];
      const int r_ch = src_flat[static_cast<size_t>(i) * 3 + 2];
      dst_flat[static_cast<size_t>(i)] = static_cast<uint8_t>(
          (b * kWeightB + g * kWeightG + r_ch * kWeightR) >> 8);
    }
  } else {
    // 非连续（如 ROI 子图），降级到版本1
    dst = BgrToGrayV1(src);
  }
  return dst;
}

// ============================================================================
// 版本3：std::mdspan (C++23) 封装
// 目的：用多维视图统一表达 HWC 内存布局，对标 AI 张量处理标准。
// mdspan 不拥有数据，是零开销视图（类比 std::span 的多维版本）。
// 布局：HWC (Height × Width × Channels=3)，默认行优先（row-major）
// 注意：cv::Mat 内存连续时 step[0] == cols*3，此处仅处理连续 Mat
// ============================================================================
[[nodiscard]] cv::Mat BgrToGrayV3(const cv::Mat& src) {
  if (src.type() != CV_8UC3) {
    throw std::invalid_argument("输入必须是 CV_8UC3 格式");
  }
  if (!src.isContinuous()) {
    // mdspan 要求内存连续（无 padding），非连续退回版本1
    return BgrToGrayV1(src);
  }

  cv::Mat dst(src.rows, src.cols, CV_8UC1);

  // 将 cv::Mat 原始内存包装为 HWC 三维视图
  // extents: (H, W, C=3) 全为动态维度
  using Extents3D = std::extents<size_t, std::dynamic_extent,
                                 std::dynamic_extent, std::dynamic_extent>;
  std::mdspan<const uint8_t, Extents3D> src_view(
      src.data, static_cast<size_t>(src.rows), static_cast<size_t>(src.cols),
      3UZ);

  std::mdspan<uint8_t,
              std::extents<size_t, std::dynamic_extent, std::dynamic_extent>>
      dst_view(dst.data, static_cast<size_t>(dst.rows),
               static_cast<size_t>(dst.cols));

  for (size_t r = 0; r < src_view.extent(0); ++r) {
    for (size_t c = 0; c < src_view.extent(1); ++c) {
      // 通过多维下标访问像素，语义比 ptr[c*3+ch] 更清晰
      const int b = src_view[r, c, 0];
      const int g = src_view[r, c, 1];
      const int r_ch = src_view[r, c, 2];
      dst_view[r, c] = static_cast<uint8_t>(
          (b * kWeightB + g * kWeightG + r_ch * kWeightR) >> 8);
    }
  }
  return dst;
}

// [SIMD 思路：选做，此处仅注释说明]
// AVX2 版本可同时处理 8 个像素（8×3=24 字节/次加载）：
//   1. _mm256_loadu_si256 加载 BGR 数据（需要处理末尾对齐问题）
//   2. 通道分离：shuffle 提取 B/G/R 分量
//   3. 整数乘加：_mm256_maddubs_epi16 + 水平求和
//   4. 打包回 uint8：_mm256_packus_epi16
// 预期吞吐量：标量版 ~3 cycles/pixel → AVX2 版 ~0.4 cycles/pixel（理论值）
// 实际受限于内存带宽，1080P 图像加速比约 3-5x

}  // namespace w9
