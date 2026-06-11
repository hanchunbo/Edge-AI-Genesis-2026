// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：W15 分类后处理 —— softmax、Top-K 选择、ImageNet 标签加载与映射。
// ============================================================================

#ifndef EDGE_AI_GENESIS_2026_W15_POSTPROCESS_HPP_
#define EDGE_AI_GENESIS_2026_W15_POSTPROCESS_HPP_

#include <span>
#include <string>
#include <vector>

namespace w15 {

// 单条 Top-K 结果。
struct TopK {
  int index;          // 类别索引
  float score;        // softmax 后概率（0~1）
  std::string label;  // ImageNet 标签文本
};

// 数值稳定 softmax（减最大值后取指数）。返回与输入等长的概率向量。
[[nodiscard]] std::vector<float> Softmax(std::span<const float> logits);

// 从文本文件按行读取标签（每行一个）。空文件抛 std::runtime_error。
[[nodiscard]] std::vector<std::string> LoadLabels(const std::string& path);

// 对 logits 做 softmax，取概率最高的 k 个，附带标签。
// 要求 logits.size() == labels.size()，否则抛 std::runtime_error。
[[nodiscard]] std::vector<TopK> TopKResults(
    std::span<const float> logits, const std::vector<std::string>& labels,
    int k);

}  // namespace w15

#endif  // EDGE_AI_GENESIS_2026_W15_POSTPROCESS_HPP_
