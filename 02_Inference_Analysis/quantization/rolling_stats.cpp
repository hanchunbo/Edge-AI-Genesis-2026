// SPDX-License-Identifier: MIT
//
// 文件功能：quant 滚动延迟统计实现 —— 固定窗口 P50/P99 查询。

#include "rolling_stats.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <vector>

namespace quant {

namespace {

/// 用最近秩法（ceil(q*n)）取分位数。
///
/// 不做线性插值：插值会造出实际没观测到的延迟值，而基准报告要的是真实发生过的
/// 样本。代价是小样本下分辨率有限——窗口 128 时 P99 恒等于第 127 个样本。
/// @note 按值传入 values：内部要排序，不能改动调用方数据
PercentileStats Percentiles(std::vector<double> values) {
  if (values.empty()) {
    return PercentileStats{};
  }
  std::sort(values.begin(), values.end());
  const auto pick = [&values](double q) {
    const double rank = std::ceil(q * static_cast<double>(values.size()));
    // 注意：clamp 的下界防 q=0——rank 会是 0，减 1 后下标回绕成巨大值
    // （size_t 无符号）。当前只传 0.50/0.99 走不到，是给通用契约留的防御。
    const std::size_t idx = std::clamp(static_cast<std::size_t>(rank),
                                       std::size_t{1}, values.size()) -
                            1;
    return values[idx];
  };
  return PercentileStats{.p50 = pick(0.50), .p99 = pick(0.99)};
}

}  // namespace

RollingStats::RollingStats(std::size_t window_size)
    : window_size_(window_size) {
  if (window_size_ == 0) {
    throw std::invalid_argument("RollingStats: window_size 必须 > 0");
  }
}

void RollingStats::Add(const StageLatencyMs& sample) {
  if (samples_.size() == window_size_) {
    samples_.pop_front();
  }
  samples_.push_back(sample);
}

StageLatencyStats RollingStats::Summary() const {
  std::vector<double> pre;
  std::vector<double> infer;
  std::vector<double> post;
  std::vector<double> total;
  pre.reserve(samples_.size());
  infer.reserve(samples_.size());
  post.reserve(samples_.size());
  total.reserve(samples_.size());

  for (const StageLatencyMs& sample : samples_) {
    pre.push_back(sample.pre);
    infer.push_back(sample.infer);
    post.push_back(sample.post);
    total.push_back(sample.total);
  }

  return StageLatencyStats{
      .pre = Percentiles(std::move(pre)),
      .infer = Percentiles(std::move(infer)),
      .post = Percentiles(std::move(post)),
      .total = Percentiles(std::move(total)),
      .count = samples_.size(),
  };
}

}  // namespace quant
