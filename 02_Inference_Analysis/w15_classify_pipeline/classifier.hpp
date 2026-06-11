// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：W15 Classifier 编排类 —— 串联预处理、W14 推理、后处理，
//           对外提供「图片 -> Top-K 分类结果」一站式接口。
// ============================================================================

#ifndef EDGE_AI_GENESIS_2026_W15_CLASSIFIER_HPP_
#define EDGE_AI_GENESIS_2026_W15_CLASSIFIER_HPP_

#include "inference_engine.hpp"  // w14::InferenceEngine（经 lib PUBLIC include 可达）
#include "postprocess.hpp"
#include "preprocess.hpp"

#include <string>
#include <vector>

namespace w15 {

class Classifier {
 public:
  Classifier(const std::string& model_path, const std::string& labels_path,
             PreprocConfig cfg = {});

  // 从已解码 BGR 图分类。
  [[nodiscard]] std::vector<TopK> Classify(const cv::Mat& bgr, int top_k = 5);

  // 从文件路径分类（内部 cv::imread；读取失败抛 std::runtime_error）。
  [[nodiscard]] std::vector<TopK> Classify(const std::string& image_path,
                                           int top_k = 5);

 private:
  w14::InferenceEngine engine_;
  std::vector<std::string> labels_;
  PreprocConfig cfg_;
};

}  // namespace w15

#endif  // EDGE_AI_GENESIS_2026_W15_CLASSIFIER_HPP_
