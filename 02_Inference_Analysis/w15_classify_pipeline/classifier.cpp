// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：W15 Classifier 实现 —— 详见 classifier.hpp。
// ============================================================================

#include "classifier.hpp"

#include <cstdint>
#include <opencv2/imgcodecs.hpp>
#include <stdexcept>

namespace w15 {

Classifier::Classifier(const std::string& model_path,
                       const std::string& labels_path, PreprocConfig cfg)
    : engine_(model_path), labels_(LoadLabels(labels_path)), cfg_(cfg) {}

std::vector<TopK> Classifier::Classify(const cv::Mat& bgr, int top_k) {
  // 预处理 -> CHW float 张量
  const std::vector<float> input = Preprocess(bgr, cfg_);
  const std::vector<int64_t> shape{1, 3, cfg_.crop, cfg_.crop};

  // 复用 W14 推理（零拷贝喂入）
  std::vector<Ort::Value> outputs = engine_.Run(input, shape);
  const float* logits = outputs[0].GetTensorData<float>();
  const size_t n = outputs[0].GetTensorTypeAndShapeInfo().GetElementCount();

  // 后处理 -> Top-K
  return TopKResults(std::span<const float>(logits, n), labels_, top_k);
}

std::vector<TopK> Classifier::Classify(const std::string& image_path,
                                       int top_k) {
  const cv::Mat bgr = cv::imread(image_path, cv::IMREAD_COLOR);
  if (bgr.empty()) {
    throw std::runtime_error("[W15] 图片读取失败: " + image_path);
  }
  return Classify(bgr, top_k);
}

}  // namespace w15
