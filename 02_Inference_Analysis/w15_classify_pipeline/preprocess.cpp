// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：W15 分类预处理实现 —— 详见 preprocess.hpp。
// ============================================================================

#include "preprocess.hpp"

namespace w15 {

std::vector<float> Preprocess(const cv::Mat& /*bgr*/,
                              const PreprocConfig& cfg) {
  // 骨架占位：Task 2 用 TDD 替换为真实实现。
  return std::vector<float>(static_cast<size_t>(3) * cfg.crop * cfg.crop, 0.0f);
}

}  // namespace w15
