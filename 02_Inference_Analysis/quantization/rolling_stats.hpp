// SPDX-License-Identifier: MIT
//
// 文件功能：quant 滚动延迟统计接口 —— 固定窗口保存 pre/infer/post/total 延迟。

#ifndef EDGE_AI_GENESIS_2026_QUANTIZATION_ROLLING_STATS_HPP_
#define EDGE_AI_GENESIS_2026_QUANTIZATION_ROLLING_STATS_HPP_

#include <cstddef>
#include <deque>

namespace quant {

struct StageLatencyMs {
  double pre = 0.0;
  double infer = 0.0;
  double post = 0.0;
  double total = 0.0;
};

struct PercentileStats {
  double p50 = 0.0;
  double p99 = 0.0;
};

struct StageLatencyStats {
  PercentileStats pre;
  PercentileStats infer;
  PercentileStats post;
  PercentileStats total;
  std::size_t count = 0;
};

class RollingStats {
 public:
  explicit RollingStats(std::size_t window_size = 128);

  void Add(const StageLatencyMs& sample);

  [[nodiscard]] StageLatencyStats Summary() const;
  [[nodiscard]] std::size_t Count() const { return samples_.size(); }
  [[nodiscard]] std::size_t WindowSize() const { return window_size_; }

 private:
  std::size_t window_size_;
  std::deque<StageLatencyMs> samples_;
};

}  // namespace quant

#endif  // EDGE_AI_GENESIS_2026_QUANTIZATION_ROLLING_STATS_HPP_
