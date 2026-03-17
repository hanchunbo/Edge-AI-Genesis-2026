// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：BGR→Gray 多版实现（无 main，编译为静态库供 bench 和 test 链接）
//   V1：双层循环 + .ptr<T>()      — 展示 step/stride 内存布局
//   V2：isContinuous() 单层展开   — 连续内存优化
//   V3：std::mdspan (C++23)       — 统一多维视图抽象
//   V4：AVX2 SIMD                 — 每批 8 像素向量化（P1 修复）
//   BgrToNormCHW：float32 CHW 输出 — 推理引擎标准输入（P1 修复）
// ============================================================================

// [Legacy C++11/17]: 直接用二维下标 mat.at<uchar>(r, c*3+ch) 访问像素，
//                   每次调用都有边界检查开销，且无法表达 HWC/CHW 内存语义。
// [Pain Point]: 行指针需要手动计算 step 偏移；代码与 AI 张量访问模式不统一；
//               返回值 cv::Mat 强制堆分配，生产管道无法复用帧缓冲区（P0
//               债务）。
// [Modern C++20/23]: std::mdspan 提供零开销多维视图；void 重载允许零额外分配；
//                    AVX2 intrinsics 可将吞吐量提升 3-5× 弥合与 cv::cvtColor
//                    差距。

#include "bgr2gray.hpp"

#include "mdspan_compat.hpp"

#ifdef __AVX2__
#include <immintrin.h>
#endif

#include <cstdint>
#include <cstring>
#include <opencv2/core.hpp>
#include <span>
#include <stdexcept>
#include <vector>

namespace w9 {

// BGR→Gray 权重（ITU-R BT.601 标准，已在头文件注释中声明）
// 整数定点化：B*29 + G*150 + R*77，再右移 8 位
// 精度与浮点版 (0.114, 0.587, 0.299) 差异 < 1 灰度级
static constexpr int kWeightB = 29;   // 0.114 * 256 ≈ 29
static constexpr int kWeightG = 150;  // 0.587 * 256 ≈ 150
static constexpr int kWeightR = 77;   // 0.299 * 256 ≈ 77

// 公共前置检查：空图或非 BGR 格式均为系统边界错误，直接抛出
// 边缘设备摄像头断流时 src.empty() 是常态，必须在此防御（P0 修复）
static void ValidateSrc(const cv::Mat& src) {
  if (src.empty()) {
    throw std::invalid_argument("输入帧为空（摄像头断流？）");
  }
  if (src.type() != CV_8UC3) {
    throw std::invalid_argument("输入必须是 CV_8UC3 格式");
  }
}

// ============================================================================
// 版本1：双层循环 + .ptr<T>()
// 目的：演示 cv::Mat 的 step/stride 内存布局。
// 关键：src.step[0] 是行字节宽，不等于 cols*3（内存对齐 padding 可能存在）
// ============================================================================
void BgrToGrayV1(const cv::Mat& src, cv::Mat& dst) {
  ValidateSrc(src);
  // create 在尺寸/类型已匹配时是 no-op，避免重复分配
  dst.create(src.rows, src.cols, CV_8UC1);
  for (int r = 0; r < src.rows; ++r) {
    // .ptr<uint8_t>(r) 内部用 data + r * step[0]，正确处理了行 padding
    const uint8_t* src_row = src.ptr<uint8_t>(r);
    uint8_t* dst_row = dst.ptr<uint8_t>(r);
    for (int c = 0; c < src.cols; ++c) {
      // BGR 三通道按 3 字节连续存储（B 在低地址）
      const int b = src_row[c * 3 + 0];
      const int g = src_row[c * 3 + 1];
      const int r_ch = src_row[c * 3 + 2];
      dst_row[c] = static_cast<uint8_t>(
          (b * kWeightB + g * kWeightG + r_ch * kWeightR) >> 8);
    }
  }
}

[[nodiscard]] cv::Mat BgrToGrayV1(const cv::Mat& src) {
  cv::Mat dst;
  BgrToGrayV1(src, dst);
  return dst;
}

// ============================================================================
// 版本2：isContinuous() 单层展开
// 目的：当图像内存连续时（无 padding），展平成 1D 操作。
// 收益：减少行指针解引用次数，编译器更容易向量化。
// 降级：非连续时退回版本1，保证正确性。
// ============================================================================
void BgrToGrayV2(const cv::Mat& src, cv::Mat& dst) {
  ValidateSrc(src);
  dst.create(src.rows, src.cols, CV_8UC1);

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
    BgrToGrayV1(src, dst);
  }
}

[[nodiscard]] cv::Mat BgrToGrayV2(const cv::Mat& src) {
  cv::Mat dst;
  BgrToGrayV2(src, dst);
  return dst;
}

// ============================================================================
// 版本3：std::mdspan (C++23) 封装
// 目的：用多维视图统一表达 HWC 内存布局，对标 AI 张量处理标准。
// mdspan 不拥有数据，是零开销视图（类比 std::span 的多维版本）。
// 布局：HWC (Height × Width × Channels=3)，默认行优先（row-major）
// 注意：cv::Mat 内存连续时 step[0] == cols*3，此处仅处理连续 Mat
// ============================================================================
void BgrToGrayV3(const cv::Mat& src, cv::Mat& dst) {
  ValidateSrc(src);
  if (!src.isContinuous()) {
    // mdspan 要求内存连续（无 padding），非连续退回版本1
    BgrToGrayV1(src, dst);
    return;
  }

  dst.create(src.rows, src.cols, CV_8UC1);

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
}

[[nodiscard]] cv::Mat BgrToGrayV3(const cv::Mat& src) {
  cv::Mat dst;
  BgrToGrayV3(src, dst);
  return dst;
}

// ============================================================================
// 版本4：AVX2 SIMD（P1 修复）
//
// 核心思路：每次处理 8 个像素（24 字节输入，8 字节输出）
//   1. 加载两段 16 字节（各覆盖 4 个 BGR 像素）
//   2. 用 _mm_shuffle_epi8 分别提取 B、G、R 通道字节
//   3. _mm_cvtepu8_epi16 扩展为 uint16
//   4. _mm256_mullo_epi16 × 权重 + 累加 + 右移 8 位
//   5. _mm256_packus_epi16 饱和回 uint8，提取 8 个灰度值
//
// 安全边界：hi 段从 sp+12 读 16 字节（尾端多读 4 字节），
//   循环条件 (i + 10 <= total) 保证最后一次读不越界。
// 无 AVX2 时（#ifndef __AVX2__）自动回退到 V2。
// ============================================================================
void BgrToGrayV4(const cv::Mat& src, cv::Mat& dst) {
  ValidateSrc(src);

#ifndef __AVX2__
  // 编译期回退：无 AVX2 指令集时等价于 V2
  BgrToGrayV2(src, dst);
#else
  if (!src.isContinuous()) {
    BgrToGrayV2(src, dst);
    return;
  }

  dst.create(src.rows, src.cols, CV_8UC1);
  const int total = src.rows * src.cols;
  const uint8_t* sp = src.data;
  uint8_t* dp = dst.data;

  // 权重向量（int16，所有分量均 < 32768，无溢出风险）
  const __m256i wb = _mm256_set1_epi16(static_cast<short>(kWeightB));
  const __m256i wg = _mm256_set1_epi16(static_cast<short>(kWeightG));
  const __m256i wr = _mm256_set1_epi16(static_cast<short>(kWeightR));

  // shuffle mask：从 BGR 字节流中直接提取 4 个像素的指定通道
  // 例如 shuf_b：从 [B0,G0,R0, B1,G1,R1, B2,G2,R2, B3,G3,R3, ...]
  //              提取 [B0, B1, B2, B3, 0, 0, ...] 置于低 4 字节
  // _mm_shuffle_epi8 规则：mask[i] & 0x80 非零 → output[i]=0
  //                        否则 output[i] = src[mask[i]]
  const __m128i shuf_b = _mm_set_epi8(-1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                                      -1, -1, 9, 6, 3, 0);  // B 在每像素偏移 0
  const __m128i shuf_g = _mm_set_epi8(-1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                                      -1, -1, 10, 7, 4, 1);  // G 在每像素偏移 1
  const __m128i shuf_r = _mm_set_epi8(-1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                                      -1, -1, 11, 8, 5, 2);  // R 在每像素偏移 2

  int i = 0;
  // 循环条件 i+10<=total：确保 hi 段 _mm_loadu_si128(sp+12) 不越过缓冲区尾
  // （hi 段最远读至 sp+27，8 像素占 sp[0..23]，多读 4 字节需 total*3 >=
  // i*3+28）
  for (; i + 10 <= total; i += 8, sp += 24, dp += 8) {
    // 低 4 像素（字节 0..11）和高 4 像素（字节 12..23）分别加载 16 字节
    __m128i lo = _mm_loadu_si128(reinterpret_cast<const __m128i*>(sp));
    __m128i hi = _mm_loadu_si128(reinterpret_cast<const __m128i*>(sp + 12));

    // 提取低/高各 4 像素的 B、G、R 通道字节
    __m128i lo_b = _mm_shuffle_epi8(lo, shuf_b);  // [B0..B3, 0×12]
    __m128i lo_g = _mm_shuffle_epi8(lo, shuf_g);
    __m128i lo_r = _mm_shuffle_epi8(lo, shuf_r);
    __m128i hi_b = _mm_shuffle_epi8(hi, shuf_b);  // [B4..B7, 0×12]
    __m128i hi_g = _mm_shuffle_epi8(hi, shuf_g);
    __m128i hi_r = _mm_shuffle_epi8(hi, shuf_r);

    // uint8→uint16 扩展（cvtepu8_epi16 取低 8 字节，恰好是我们的 4 个有效值）
    // 合并为 256-bit：low lane = 低 4 像素，high lane = 高 4 像素
    __m256i b16 =
        _mm256_set_m128i(_mm_cvtepu8_epi16(hi_b), _mm_cvtepu8_epi16(lo_b));
    __m256i g16 =
        _mm256_set_m128i(_mm_cvtepu8_epi16(hi_g), _mm_cvtepu8_epi16(lo_g));
    __m256i r16 =
        _mm256_set_m128i(_mm_cvtepu8_epi16(hi_r), _mm_cvtepu8_epi16(lo_r));

    // BT.601 定点乘加：(B*29 + G*150 + R*77) >> 8
    // 最大值：255*(29+150+77)=255*256=65280 < 65535，uint16 无溢出
    __m256i sum =
        _mm256_add_epi16(_mm256_add_epi16(_mm256_mullo_epi16(b16, wb),
                                          _mm256_mullo_epi16(g16, wg)),
                         _mm256_mullo_epi16(r16, wr));
    __m256i gray16 = _mm256_srli_epi16(sum, 8);

    // uint16→uint8 饱和打包（值在 0-255，不会饱和截断）
    // packus_epi16 对每 128-bit lane 独立操作：
    //   low lane 结果:  [g0,g1,g2,g3, 0×4, 0×8]
    //   high lane 结果: [g4,g5,g6,g7, 0×4, 0×8]
    __m256i packed = _mm256_packus_epi16(gray16, _mm256_setzero_si256());

    // 取每 lane 前 4 字节（uint32）并写出
    uint32_t lo4 = static_cast<uint32_t>(
        _mm_cvtsi128_si32(_mm256_castsi256_si128(packed)));
    uint32_t hi4 = static_cast<uint32_t>(
        _mm_cvtsi128_si32(_mm256_extracti128_si256(packed, 1)));
    std::memcpy(dp, &lo4, 4);
    std::memcpy(dp + 4, &hi4, 4);
  }

  // 尾部处理（剩余 0-9 个像素，标量路径）
  for (; i < total; ++i, sp += 3, ++dp) {
    *dp = static_cast<uint8_t>(
        (sp[0] * kWeightB + sp[1] * kWeightG + sp[2] * kWeightR) >> 8);
  }
#endif  // __AVX2__
}

[[nodiscard]] cv::Mat BgrToGrayV4(const cv::Mat& src) {
  cv::Mat dst;
  BgrToGrayV4(src, dst);
  return dst;
}

// ============================================================================
// BgrToNormCHW：float32 归一化 + CHW 布局（P1 修复）
//
// 推理引擎（ONNX Runtime / TensorRT）标准输入格式：
//   uint8 BGR HWC → ÷255 → float32 [0,1] → CHW → {1,3,H,W} tensor
//
// 输出 layout（channel-first）：
//   [0 .. H*W-1]     → B 平面（所有行列的蓝色通道）
//   [H*W .. 2*H*W-1] → G 平面
//   [2*H*W .. 3*H*W-1] → R 平面
//
// 注意：此实现保持 BGR 通道顺序（即 B 平面在前）。
//       若模型期望 RGB 顺序，调用方需交换 B/R 平面或在模型预处理层处理。
// ============================================================================
[[nodiscard]] std::vector<float> BgrToNormCHW(const cv::Mat& src) {
  ValidateSrc(src);

  const int h = src.rows;
  const int w = src.cols;
  const size_t plane = static_cast<size_t>(h) * w;
  std::vector<float> out(plane * 3);

  float* b_plane = out.data();
  float* g_plane = out.data() + plane;
  float* r_plane = out.data() + plane * 2;

  for (int row = 0; row < h; ++row) {
    const uint8_t* src_row = src.ptr<uint8_t>(row);
    const size_t row_offset = static_cast<size_t>(row) * w;
    for (int col = 0; col < w; ++col) {
      const size_t idx = row_offset + col;
      // 归一化到 [0, 1]，÷255 比 /255.0f 略快（编译器等价处理）
      b_plane[idx] = src_row[col * 3 + 0] * (1.0f / 255.0f);
      g_plane[idx] = src_row[col * 3 + 1] * (1.0f / 255.0f);
      r_plane[idx] = src_row[col * 3 + 2] * (1.0f / 255.0f);
    }
  }
  return out;
}

}  // namespace w9
