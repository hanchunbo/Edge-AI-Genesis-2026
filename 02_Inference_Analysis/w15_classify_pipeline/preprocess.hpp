// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：W15 分类预处理 —— cv::Mat(BGR) 转 ImageNet 归一化 CHW float 张量。
// ============================================================================

#ifndef EDGE_AI_GENESIS_2026_W15_PREPROCESS_HPP_
#define EDGE_AI_GENESIS_2026_W15_PREPROCESS_HPP_

#include <array>
#include <opencv2/core.hpp>
#include <vector>

namespace w15 {

// 分类预处理配置。归一化参数显式定义（消 tech-debt「归一化参数未定义」）。
struct PreprocConfig {
  int resize_short = 256;  // 短边缩放到该值，保持长宽比
  int crop = 224;          // 中心裁剪到 crop×crop
  std::array<float, 3> mean{0.485f, 0.456f, 0.406f};  // ImageNet RGB 均值
  std::array<float, 3> std{0.229f, 0.224f, 0.225f};   // ImageNet RGB 标准差
  bool to_rgb = true;  // OpenCV 读入为 BGR，分类模型按 RGB 训练，需转换
};

// 把 BGR 图像预处理成连续 CHW float32 张量（大小 3*crop*crop）。
// 步骤：resize 短边 -> center-crop -> (可选 BGR2RGB) -> /255 -> 归一化 -> CHW。
[[nodiscard]] std::vector<float> Preprocess(const cv::Mat& bgr,
                                            const PreprocConfig& cfg);

}  // namespace w15

#endif  // EDGE_AI_GENESIS_2026_W15_PREPROCESS_HPP_
