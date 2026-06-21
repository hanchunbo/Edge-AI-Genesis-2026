// SPDX-License-Identifier: MIT
//
// 文件功能：W13 ImagePipeline 演示程序
//   场景 A：线程数扩展性 benchmark（100 帧，1/2/4/N 线程）
//   场景 B：Gray vs Tensor 延迟分布对比（P50/P95/P99）
//   场景 C：LetterboxInfo 数值验证（展示检测框反算公式）
//
//   输出可直接重定向到 docs/benchmarks/Q1_week_13.md 作为 Q2 基准数据。

#include "image_pipeline.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <future>
#include <iostream>
#include <numeric>
#include <opencv2/core.hpp>
#include <thread>
#include <vector>

namespace {

// 生成指定尺寸的合成 BGR 帧（用随机噪声模拟真实图像内容）
cv::Mat MakeSyntheticFrame(int rows, int cols) {
  cv::Mat frame(rows, cols, CV_8UC3);
  cv::randu(frame, cv::Scalar(0, 0, 0), cv::Scalar(255, 255, 255));
  return frame;
}

// 生成批量合成帧
std::vector<cv::Mat> MakeBatch(int count, int rows = 1080, int cols = 1920) {
  std::vector<cv::Mat> batch;
  batch.reserve(count);
  for (int i = 0; i < count; ++i) {
    batch.push_back(MakeSyntheticFrame(rows, cols));
  }
  return batch;
}

// 收集所有 future 结果，返回各帧 processing_time（微秒）
std::vector<double> CollectTimings(
    std::vector<std::future<w13::FrameResult>>& futures) {
  std::vector<double> timings;
  timings.reserve(futures.size());
  for (auto& f : futures) {
    auto result = f.get();
    timings.push_back(static_cast<double>(result.processing_time.count()));
  }
  return timings;
}

// 计算百分位数（原地排序，传入可修改副本）
double Percentile(std::vector<double> data, double p) {
  if (data.empty()) return 0.0;
  std::sort(data.begin(), data.end());
  size_t idx =
      static_cast<size_t>(p / 100.0 * static_cast<double>(data.size() - 1));
  return data[idx];
}

// ============================================================================
// 场景 A：线程数扩展性（展示并行加速比）
// ============================================================================
void RunThroughputBench() {
  std::cout << "\n========================================\n"
            << "  场景 A：线程数扩展性 Benchmark\n"
            << "  100 帧 1920×1080，kTensor 模式\n"
            << "========================================\n";

  constexpr int kFrameCount = 100;
  auto batch = MakeBatch(kFrameCount);
  std::span<const cv::Mat> frame_span{batch};

  size_t hw_concurrency = std::thread::hardware_concurrency();
  std::vector<size_t> thread_counts = {1, 2, 4, hw_concurrency};
  // 去重（避免 hw_concurrency==4 时重复测试）
  std::sort(thread_counts.begin(), thread_counts.end());
  thread_counts.erase(std::unique(thread_counts.begin(), thread_counts.end()),
                      thread_counts.end());

  double baseline_ms = 0.0;

  std::cout << std::format("{:<8} {:>12} {:>10}\n", "线程数", "墙钟时间(ms)",
                           "加速比");
  std::cout << std::string(32, '-') << "\n";

  for (size_t nt : thread_counts) {
    w13::PipelineConfig cfg{
        .mode = w13::ProcessMode::kTensor,
        .dst_w = 640,
        .dst_h = 640,
        .num_threads = nt,
    };
    w13::ImagePipeline pipeline(cfg);

    auto wall_t0 = std::chrono::steady_clock::now();
    auto futures = pipeline.SubmitBatch(frame_span);
    for (auto& f : futures)
      f.get();  // 等待所有帧完成
    double wall_ms = std::chrono::duration<double, std::milli>(
                         std::chrono::steady_clock::now() - wall_t0)
                         .count();

    if (nt == 1) baseline_ms = wall_ms;
    double speedup = (baseline_ms > 0.0) ? baseline_ms / wall_ms : 1.0;

    std::cout << std::format("{:<8} {:>12.1f} {:>10.2f}x\n", nt, wall_ms,
                             speedup);
    pipeline.Shutdown();
  }
}

// ============================================================================
// 场景 B：Gray vs Tensor 延迟分布（P50/P95/P99）
// ============================================================================
void RunLatencyComparison() {
  std::cout << "\n========================================\n"
            << "  场景 B：Gray vs Tensor 延迟分布\n"
            << "  50 帧 1920×1080，hardware_concurrency 线程\n"
            << "========================================\n";

  constexpr int kFrameCount = 50;
  auto batch = MakeBatch(kFrameCount);
  std::span<const cv::Mat> frame_span{batch};

  // Gray 模式
  {
    w13::PipelineConfig cfg{.mode = w13::ProcessMode::kGray};
    w13::ImagePipeline pipeline(cfg);
    auto futures = pipeline.SubmitBatch(frame_span);
    auto timings = CollectTimings(futures);
    pipeline.Shutdown();

    std::cout << std::format(
        "\n[kGray]   P50={:.0f}µs  P95={:.0f}µs  P99={:.0f}µs\n",
        Percentile(timings, 50.0), Percentile(timings, 95.0),
        Percentile(timings, 99.0));
    std::cout << pipeline.GetTimingReport().ToString();
  }

  // Tensor 模式（重建 pipeline，ResetStats 也可用）
  {
    w13::PipelineConfig cfg{
        .mode = w13::ProcessMode::kTensor,
        .dst_w = 640,
        .dst_h = 640,
    };
    w13::ImagePipeline pipeline(cfg);
    auto futures = pipeline.SubmitBatch(frame_span);
    auto timings = CollectTimings(futures);
    pipeline.Shutdown();

    std::cout << std::format(
        "\n[kTensor] P50={:.0f}µs  P95={:.0f}µs  P99={:.0f}µs\n",
        Percentile(timings, 50.0), Percentile(timings, 95.0),
        Percentile(timings, 99.0));
    std::cout << pipeline.GetTimingReport().ToString();
  }
}

// ============================================================================
// 场景 C：LetterboxInfo 数值验证（展示检测框反算公式）
// ============================================================================
void RunLetterboxVerify() {
  std::cout << "\n========================================\n"
            << "  场景 C：LetterboxInfo 验证\n"
            << "  输入 1280×720 → 目标 640×640\n"
            << "========================================\n";

  cv::Mat frame = MakeSyntheticFrame(720, 1280);

  w13::PipelineConfig cfg{
      .mode = w13::ProcessMode::kTensor,
      .dst_w = 640,
      .dst_h = 640,
  };
  w13::ImagePipeline pipeline(cfg);

  auto fut = pipeline.Submit(frame);
  auto result = fut.get();
  const auto& info = result.letterbox_info;

  // 理论计算：scale = min(640/1280, 640/720) = min(0.5, 0.889) = 0.5
  // resize 后尺寸：1280*0.5=640 (宽满), 720*0.5=360
  // pad_top = (640 - 360) / 2 = 140, pad_left = 0
  std::cout << std::format(
      "\n实际 LetterboxInfo:\n"
      "  scale    = {:.4f}  (期望 ≈ 0.5000)\n"
      "  pad_left = {:3d}    (期望 = 0)\n"
      "  pad_top  = {:3d}    (期望 = 140)\n",
      info.scale, info.pad_left, info.pad_top);

  // 演示检测框坐标反算（假设推理输出检测框中心在 320,320）
  float pred_cx = 320.0f;
  float pred_cy = 320.0f;
  float orig_cx = (pred_cx - static_cast<float>(info.pad_left)) / info.scale;
  float orig_cy = (pred_cy - static_cast<float>(info.pad_top)) / info.scale;
  std::cout << std::format(
      "\n示例反算（推理框中心 ({:.0f},{:.0f}) → 原始坐标 ({:.1f},{:.1f})）\n"
      "  原始坐标 = (pred - pad) / scale\n",
      pred_cx, pred_cy, orig_cx, orig_cy);

  pipeline.Shutdown();
}

}  // namespace

int main() {
  std::cout << "=== W13 高性能多线程图像预处理引擎演示 ===\n";
  std::cout << std::format("硬件并发数: {}\n",
                           std::thread::hardware_concurrency());

  RunThroughputBench();
  RunLatencyComparison();
  RunLetterboxVerify();

  std::cout
      << "\n=== 演示完成，输出可重定向至 docs/benchmarks/Q1_week_13.md ===\n";
  return 0;
}
