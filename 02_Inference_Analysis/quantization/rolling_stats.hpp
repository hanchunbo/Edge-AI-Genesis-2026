// SPDX-License-Identifier: MIT
//
// 文件功能：quant 滚动延迟统计接口 —— 固定窗口保存 pre/infer/post/total 延迟。

#ifndef EDGE_AI_GENESIS_2026_QUANTIZATION_ROLLING_STATS_HPP_
#define EDGE_AI_GENESIS_2026_QUANTIZATION_ROLLING_STATS_HPP_

#include <cstddef>
#include <deque>

namespace quant {

/// 单次检测的分段延迟（毫秒）：预处理 / 推理 / 后处理 / 端到端。
struct StageLatencyMs {
  double pre = 0.0;
  double infer = 0.0;
  double post = 0.0;
  double total = 0.0;
};

/// 单个阶段的分位数统计（毫秒）。
struct PercentileStats {
  double p50 = 0.0;
  double p99 = 0.0;
};

/// 四个阶段各自的 P50/P99 + 当前窗口样本数。
struct StageLatencyStats {
  PercentileStats pre;
  PercentileStats infer;
  PercentileStats post;
  PercentileStats total;
  std::size_t count = 0;
};

/// 固定窗口的滚动延迟统计：只保留最近 window_size 个样本，超出淘汰最旧的。
/// 窗口固定使长跑 benchmark 内存有界，且分位数反映近期状态而非全历史。
class RollingStats {
 public:
  /// @throws std::invalid_argument window_size 为 0
  explicit RollingStats(std::size_t window_size = 128);

  /// 追加一个样本；窗口已满时先淘汰最旧的。
  void Add(const StageLatencyMs& sample);

  /// 汇总当前窗口内各阶段的 P50/P99。
  /// @note 空窗口返回全 0（count=0），不是错误——调用方靠 count 区分
  [[nodiscard]] StageLatencyStats Summary() const;
  [[nodiscard]] std::size_t Count() const { return samples_.size(); }
  [[nodiscard]] std::size_t WindowSize() const { return window_size_; }

 private:
  std::size_t window_size_;
  std::deque<StageLatencyMs> samples_;
};

}  // namespace quant

#endif  // EDGE_AI_GENESIS_2026_QUANTIZATION_ROLLING_STATS_HPP_
