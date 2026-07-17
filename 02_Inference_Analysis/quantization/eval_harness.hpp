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

/// 一个待评估模型：报告里显示的名字 + ONNX 路径。
struct ModelCase {
  std::string name;
  std::string model_path;
};

/// 评估参数：检测器配置 + warmup/正式迭代次数 + 统计窗口。
struct EvalConfig {
  w16::DetectorConfig detector;
  int warmup = 10;
  int iters = 50;
  int stats_window = 128;
};

/// 单模型评估结果：EP 实况 + 最后一次检测概况 + 延迟分位数。
struct ModelResult {
  std::string name;
  std::string model_path;

  /// 实际生效的 EP 与回退原因。
  /// @warning 请求 CUDA 但环境无 GPU provider 时会静默回退 CPU——报告必须打印
  /// ep_fallback_reason，否则会把 CPU 数字当成 GPU 数字读
  w14::Ep active_ep = w14::Ep::kCpu;
  std::string ep_fallback_reason;

  /// 最后一次迭代的检测概况，仅用于 sanity check（确认模型没输出 0 框）。
  /// @warning 不是精度指标——INT8 精度看 mAP（tools/eval_map_coco128.py）与
  /// Quant_Int8ConsistencyTest 的框级比对
  std::size_t detection_count = 0;
  float top_score = 0.0f;
  StageLatencyStats latency;
};

/// 多模型评估 harness：把每个 ModelCase 放进同一套 W16 检测流水线，
/// 统一 warmup、迭代次数与统计口径，使 FP32/INT8 的延迟可直接对比。
class EvalHarness {
 public:
  /// @throws std::invalid_argument warmup<0 / iters<=0 / stats_window<=0
  explicit EvalHarness(EvalConfig config);

  /// 逐个模型跑 warmup + iters 次计时迭代。
  /// @return 与 cases 等长且同序的结果
  [[nodiscard]] std::vector<ModelResult> Run(
      const std::vector<ModelCase>& cases, const std::string& image_path) const;

 private:
  EvalConfig config_;
};

}  // namespace quant

#endif  // EDGE_AI_GENESIS_2026_QUANTIZATION_EVAL_HARNESS_HPP_
