// SPDX-License-Identifier: MIT
//
// 文件功能：quant 评估 harness 实现 —— warmup、正式迭代与延迟汇总。

#include "eval_harness.hpp"

#include <opencv2/imgcodecs.hpp>
#include <stdexcept>

namespace quant {

namespace {

/// W16 分段计时 → 滚动统计样本的字段映射。
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
  // 只解码一次：分段计时在 DetectWithProfile(Mat) 内部才开始、本就不含 imread，
  // 逐轮重读主要是白耗解码时间。
  // 口径影响见 notes.md §Profiling 入口与采集边界。
  const cv::Mat bgr = cv::imread(image_path, cv::IMREAD_COLOR);
  if (bgr.empty()) {
    throw std::runtime_error("EvalHarness: 图片读取失败: " + image_path);
  }

  std::vector<ModelResult> results;
  results.reserve(cases.size());

  for (const ModelCase& model : cases) {
    w16::YOLODetector detector(model.model_path, config_.detector);

    // warmup 不计入统计，避免冷启动、CPU cache 或 CUDA autotuning 污染分位数。
    // static_cast<void>：DetectWithProfile 标了 [[nodiscard]]，这里只要副作用、
    // 不要返回值，显式转 void 表明「有意丢弃」，否则 -Wunused-result 会报警告。
    for (int i = 0; i < config_.warmup; ++i) {
      static_cast<void>(detector.DetectWithProfile(bgr));
    }

    RollingStats rolling(static_cast<std::size_t>(config_.stats_window));
    // 只留最后一次结果做 sanity check：同模型 + 同图 + 同配置的迭代是确定性的，
    // 每次输出相同，取哪一次都等价——last 恰好是还在作用域里的那个，零额外成本。
    // 注意：CUDA 归约顺序抖动可能让卡在 conf 阈值边缘的框 ±1，但它防的是
    // 「全部坍缩成 0 框」这种全有或全无的故障，不受该抖动影响
    w16::DetectionResult last;
    for (int i = 0; i < config_.iters; ++i) {
      last = detector.DetectWithProfile(bgr);
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
