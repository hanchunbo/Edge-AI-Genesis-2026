// SPDX-License-Identifier: MIT
//
// 文件功能：BGR→Gray 多版实现的函数声明
#ifndef W9_BGR2GRAY_HPP_
#define W9_BGR2GRAY_HPP_

#include <opencv2/core.hpp>
#include <vector>

namespace w9 {

// ============================================================================
// 通用说明
//   src 必须满足：!src.empty() && src.type() == CV_8UC3
//   否则抛出 std::invalid_argument。
//
//   void 重载允许调用方传入预分配 dst（cv::Mat::create 在尺寸/类型匹配时是
//   no-op，避免重复堆分配，适合生产管道中复用帧缓冲区的场景）。
// ============================================================================

// ---- V1: 双层循环 + ptr<T>，演示 step/stride 内存布局 ----

[[nodiscard]] cv::Mat BgrToGrayV1(const cv::Mat& src);
void BgrToGrayV1(const cv::Mat& src, cv::Mat& dst);

// ---- V2: isContinuous() 单层展开，连续内存优化 ----

[[nodiscard]] cv::Mat BgrToGrayV2(const cv::Mat& src);
void BgrToGrayV2(const cv::Mat& src, cv::Mat& dst);

// ---- V3: std::mdspan (C++23) 多维视图封装 ----

[[nodiscard]] cv::Mat BgrToGrayV3(const cv::Mat& src);
void BgrToGrayV3(const cv::Mat& src, cv::Mat& dst);

// ---- V4: AVX2 SIMD，每批处理 8 像素（x86-64） ----
//   编译时无 __AVX2__ 则自动回退到 V2，行为等价但无加速。
//   需编译选项 -mavx2（CMakeLists 已添加）。

[[nodiscard]] cv::Mat BgrToGrayV4(const cv::Mat& src);
void BgrToGrayV4(const cv::Mat& src, cv::Mat& dst);

// ---- 归一化 + CHW 转换（推理引擎标准输入格式） ----
//   转换链：uint8 BGR HWC → ÷255 float32 → CHW (channel-first)
//   返回大小为 H×W×3 的 float32 向量：
//     [B_plane(H*W), G_plane(H*W), R_plane(H*W)]
//   对应 ONNX Runtime / TensorRT 的 {1, 3, H, W} 输入格式。

[[nodiscard]] std::vector<float> BgrToNormCHW(const cv::Mat& src);

}  // namespace w9

#endif  // W9_BGR2GRAY_HPP_
