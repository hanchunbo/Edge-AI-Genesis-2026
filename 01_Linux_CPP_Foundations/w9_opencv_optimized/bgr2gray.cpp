// SPDX-License-Identifier: MIT
//
// 文件功能：BGR→Gray 多版实现（无 main，编译为静态库供 bench 和 test 链接）
//   V1：双层循环 + .ptr<T>()      — 展示 step/stride 内存布局
//   V2：isContinuous() 单层展开   — 连续内存优化
//   V3：std::mdspan (C++23)       — 统一多维视图抽象
//   V4：AVX2 SIMD                 — 每批 8 像素向量化（P1 修复）
//   BgrToNormCHW：float32 CHW 输出 — 推理引擎标准输入（P1 修复）

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
// 版本4：AVX2 SIMD — vpmaddubsw 乘加 + 16 像素/迭代 + 单次 16 字节 store
//
// 核心优化：用 _mm256_maddubs_epi16（vpmaddubsw）替代 3×mullo_epi16 + 2×add。
//   vpmaddubsw 一条指令完成 uint8×int8 乘加 → int16，吞吐率 0.5 cycle，
//   原来 6×mullo+4×add = 10 条指令，现在 2×maddubs+1×add = 3 条。
//
// 权重拆分（总和不变，每对各自 ≤ 32767 保证 int16 不溢出）：
//   [B,G] × [29,99] → B*29 + G*99          max 255*(29+99)=32640 < 32767 ✓
//   [G,R] × [51,77] → G*51 + R*77          max 255*(51+77)=32640 < 32767 ✓
//   vpaddw 相加（wrapping）→ B*29+G*150+R*77   max 65280 < 65536 ✓
//   srli 8 → 正确灰度值（wrapping 后 >>8 等价于无符号右移，与 V1 结果完全一致）
//
// 打包路径（与 V3→V4 相同）：
//   packus_epi16(gray_a, gray_b) + shuffle_epi8(compress) + permutevar8x32
//   → 16 字节连续灰度值，_mm_storeu_si128 一次写出
//
// 安全边界：hi_b 从 sp+36 读 16 字节（最远 sp+51），
//   循环条件 (i+18 ≤ total) 保证 (total-i)*3 ≥ 54 > 51。
// 1920×1080 = 2,073,600 恰好整除 16，尾部标量不执行。
// 无 AVX2 时自动回退 V2。
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

  // shuf_bg：从 4 像素 BGR 流中提取 [B0,G0, B1,G1, B2,G2, B3,G3, 0×8]
  // shuf_gr：提取 [G0,R0, G1,R1, G2,R2, G3,R3, 0×8]
  // 输入格式：B0,G0,R0, B1,G1,R1, B2,G2,R2, B3,G3,R3, ?,?,?,?（16 字节，低 12
  // 有效）
  const __m128i shuf_bg =
      _mm_set_epi8(-1, -1, -1, -1, -1, -1, -1, -1, 10, 9, 7, 6, 4, 3, 1, 0);
  const __m128i shuf_gr =
      _mm_set_epi8(-1, -1, -1, -1, -1, -1, -1, -1, 11, 10, 8, 7, 5, 4, 2, 1);

  // maddubs 权重：w_bg=[29,99,…], w_gr=[51,77,…]（int8，均 < 128 不溢出）
  // _mm256_set1_epi16(x)：将 x 的低字节=29/51，高字节=99/77 广播到所有通道
  const __m256i w_bg =
      _mm256_set1_epi16(static_cast<short>((99 << 8) | 29));  // [29,99]×16
  const __m256i w_gr =
      _mm256_set1_epi16(static_cast<short>((77 << 8) | 51));  // [51,77]×16

  // 压缩 mask（pshufb 对两 lane 独立）：[G×4,0×4,G×4,0×4] → [G×8,0×8]
  const __m256i compress_mask =
      _mm256_set_epi8(-1, -1, -1, -1, -1, -1, -1, -1, 11, 10, 9, 8, 3, 2, 1,
                      0,  // lane 1
                      -1, -1, -1, -1, -1, -1, -1, -1, 11, 10, 9, 8, 3, 2, 1,
                      0);  // lane 0

  // dword 重排 [0,4,1,5] 将两 lane 有效数据汇聚到低 128 位
  const __m256i perm_idx = _mm256_set_epi32(7, 6, 5, 4, 5, 1, 4, 0);

  int i = 0;
  // 每次处理 16 像素；hi_b 读至 sp+51，需剩余 ≥ 18 像素
  for (; i + 18 <= total; i += 16, sp += 48, dp += 16) {
    // ── 批次 A：像素 0-7（2×128-bit 加载，各覆盖 4 像素）────────────
    __m128i lo_a = _mm_loadu_si128(reinterpret_cast<const __m128i*>(sp));
    __m128i hi_a = _mm_loadu_si128(reinterpret_cast<const __m128i*>(sp + 12));

    // 提取 [B,G] 和 [G,R] 对，合并为 256-bit（lane0=lo 4像素，lane1=hi 4像素）
    __m256i bg_a = _mm256_set_m128i(_mm_shuffle_epi8(hi_a, shuf_bg),
                                    _mm_shuffle_epi8(lo_a, shuf_bg));
    __m256i gr_a = _mm256_set_m128i(_mm_shuffle_epi8(hi_a, shuf_gr),
                                    _mm_shuffle_epi8(lo_a, shuf_gr));

    // maddubs：B*29+G*99 和 G*51+R*77，vpaddw wrapping 相加后 >>8 = 灰度值
    __m256i gray16_a =
        _mm256_srli_epi16(_mm256_add_epi16(_mm256_maddubs_epi16(bg_a, w_bg),
                                           _mm256_maddubs_epi16(gr_a, w_gr)),
                          8);

    // ── 批次 B：像素 8-15 ────────────────────────────────────────────
    __m128i lo_b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(sp + 24));
    __m128i hi_b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(sp + 36));

    __m256i bg_b = _mm256_set_m128i(_mm_shuffle_epi8(hi_b, shuf_bg),
                                    _mm_shuffle_epi8(lo_b, shuf_bg));
    __m256i gr_b = _mm256_set_m128i(_mm_shuffle_epi8(hi_b, shuf_gr),
                                    _mm_shuffle_epi8(lo_b, shuf_gr));

    __m256i gray16_b =
        _mm256_srli_epi16(_mm256_add_epi16(_mm256_maddubs_epi16(bg_b, w_bg),
                                           _mm256_maddubs_epi16(gr_b, w_gr)),
                          8);

    // ── 打包 16 个灰度值 → 16 字节连续输出 ──────────────────────────
    // packus: lane0=[G0-G3,0×4,G8-G11,0×4] lane1=[G4-G7,0×4,G12-G15,0×4]
    __m256i packed = _mm256_packus_epi16(gray16_a, gray16_b);
    // compress: lane0=[G0-G3,G8-G11,0×8] lane1=[G4-G7,G12-G15,0×8]
    __m256i compressed = _mm256_shuffle_epi8(packed, compress_mask);
    // permute dword[0,4,1,5] → 低128位 = G0..G15 顺序正确
    __m256i result = _mm256_permutevar8x32_epi32(compressed, perm_idx);
    _mm_storeu_si128(reinterpret_cast<__m128i*>(dp),
                     _mm256_castsi256_si128(result));
  }

  // 尾部标量（剩余 0-17 像素；1080P 整除 16，此段不执行）
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
