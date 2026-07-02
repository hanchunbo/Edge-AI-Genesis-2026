// SPDX-License-Identifier: MIT
//
// 文件功能：quant 评估 harness 接口 —— 多模型共用 W16 检测流水线并收集延迟。

#ifndef EDGE_AI_GENESIS_2026_QUANTIZATION_EVAL_HARNESS_HPP_
#define EDGE_AI_GENESIS_2026_QUANTIZATION_EVAL_HARNESS_HPP_

#include "rolling_stats.hpp"
#include "yolo_detector.hpp"

#include <string>
#include <vector>

namespace quant {

struct ModelCase {
  std::string name;
  std::string model_path;
};

struct EvalConfig {
  w16::DetectorConfig detector;
  int warmup = 10;
  int iters = 50;
  int stats_window = 128;
};

struct ModelResult {
  std::string name;
  std::string model_path;
  w14::Ep active_ep = w14::Ep::kCpu;
  std::string ep_fallback_reason;
  std::size_t detection_count = 0;
  float top_score = 0.0f;
  StageLatencyStats latency;
};

class EvalHarness {
 public:
  explicit EvalHarness(EvalConfig config);

  [[nodiscard]] std::vector<ModelResult> Run(
      const std::vector<ModelCase>& cases, const std::string& image_path) const;

 private:
  EvalConfig config_;
};

}  // namespace quant

#endif  // EDGE_AI_GENESIS_2026_QUANTIZATION_EVAL_HARNESS_HPP_
