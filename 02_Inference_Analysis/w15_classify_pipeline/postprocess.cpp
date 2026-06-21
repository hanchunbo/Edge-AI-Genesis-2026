// SPDX-License-Identifier: MIT
//
// 文件功能：W15 分类后处理实现 —— 详见 postprocess.hpp。

#include "postprocess.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numeric>
#include <stdexcept>

namespace w15 {

std::vector<float> Softmax(std::span<const float> logits) {
  std::vector<float> out(logits.size());
  if (logits.empty()) {
    return out;
  }
  const float max_v = *std::max_element(logits.begin(), logits.end());
  float sum = 0.0f;
  for (size_t i = 0; i < logits.size(); ++i) {
    out[i] = std::exp(logits[i] - max_v);
    sum += out[i];
  }
  for (float& v : out) {
    v /= sum;
  }
  return out;
}

std::vector<std::string> LoadLabels(const std::string& path) {
  std::ifstream f(path);
  if (!f) {
    throw std::runtime_error("[W15] 标签文件打开失败: " + path);
  }
  std::vector<std::string> labels;
  std::string line;
  while (std::getline(f, line)) {
    // 去掉行尾可能的 \r（Windows 换行兼容）
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    labels.push_back(line);
  }
  if (labels.empty()) {
    throw std::runtime_error("[W15] 标签文件为空: " + path);
  }
  return labels;
}

std::vector<TopK> TopKResults(std::span<const float> logits,
                              const std::vector<std::string>& labels, int k) {
  if (logits.size() != labels.size()) {
    throw std::runtime_error(
        "[W15] logits 维度与标签数不一致: " + std::to_string(logits.size()) +
        " vs " + std::to_string(labels.size()));
  }
  const std::vector<float> probs = Softmax(logits);

  std::vector<int> idx(probs.size());
  std::iota(idx.begin(), idx.end(), 0);
  const int kk = std::min<int>(k, static_cast<int>(probs.size()));
  std::partial_sort(idx.begin(), idx.begin() + kk, idx.end(),
                    [&probs](int a, int b) { return probs[a] > probs[b]; });

  std::vector<TopK> out;
  out.reserve(kk);
  for (int i = 0; i < kk; ++i) {
    const int id = idx[i];
    out.push_back(TopK{id, probs[id], labels[id]});
  }
  return out;
}

}  // namespace w15
