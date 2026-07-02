// SPDX-License-Identifier: MIT
//
// 文件功能：quant 评估 harness 实现 —— warmup、正式迭代与延迟汇总。

#include "eval_harness.hpp"

#include <stdexcept>

namespace quant {

namespace {

StageLatencyMs ToStageLatency(const w16::DetectionTiming& timing) {
  return StageLatencyMs{.pre = timing.pre_ms,
                        .infer = timing.infer_ms,
                        .post = timing.post_ms,
                        .total = timing.total_ms};
}

}  // namespace

EvalHarness::EvalHarness(EvalConfig config) : config_(config) {
  if (config_.warmup < 0) {
    throw std::invalid_argument("EvalHarness: warmup 必须 >= 0");
  }
  if (config_.iters <= 0) {
    throw std::invalid_argument("EvalHarness: iters 必须 > 0");
  }
  if (config_.stats_window <= 0) {
    throw std::invalid_argument("EvalHarness: stats_window 必须 > 0");
  }
}

std::vector<ModelResult> EvalHarness::Run(const std::vector<ModelCase>& cases,
                                          const std::string& image_path) const {
  std::vector<ModelResult> results;
  results.reserve(cases.size());

  for (const ModelCase& model : cases) {
    w16::YOLODetector detector(model.model_path, config_.detector);

    // warmup 不计入统计，避免冷启动、CPU cache 或 CUDA autotuning 污染分位数。
    for (int i = 0; i < config_.warmup; ++i) {
      static_cast<void>(detector.DetectWithProfile(image_path));
    }

    RollingStats rolling(static_cast<std::size_t>(config_.stats_window));
    w16::DetectionResult last;
    for (int i = 0; i < config_.iters; ++i) {
      last = detector.DetectWithProfile(image_path);
      rolling.Add(ToStageLatency(last.timing));
    }

    const bool has_detection = !last.detections.empty();
    results.push_back(ModelResult{
        .name = model.name,
        .model_path = model.model_path,
        .active_ep = detector.ActiveEp(),
        .ep_fallback_reason = detector.EpFallbackReason(),
        .detection_count = last.detections.size(),
        .top_score = has_detection ? last.detections.front().score : 0.0f,
        .latency = rolling.Summary(),
    });
  }

  return results;
}

}  // namespace quant
