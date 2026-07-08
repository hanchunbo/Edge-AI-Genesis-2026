// SPDX-License-Identifier: MIT
//
// 文件功能：trt M1 基准 —— TRT FP16 纯 infer 延迟（P50/P99），
//           对比 quant 同机 CUDA EP FP32 基线（引用不重测）。

#include "custom_resize.hpp"  // w10::LetterboxToTensor
#include "engine_builder.hpp"
#include "rolling_stats.hpp"  // quant::RollingStats（统计口径对齐 quant）
#include "trt_engine.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <string>
#include <vector>

namespace {

constexpr int kInput = 640;
constexpr int kWarmup = 5;  // 对齐 quant_benchmark 口径
constexpr int kIters = 20;  // 对齐 quant_benchmark 口径
// quant_int8_report.md 同机基线（2026-07 实测），spec 明确引用不重测。
constexpr double kCudaEpFp32P50Ms = 5.64;

}  // namespace

int main(int argc, char** argv) {
  const std::string image_path = argc > 1 ? argv[1] : TRT_IMAGE_PATH;
  const cv::Mat bgr = cv::imread(image_path, cv::IMREAD_COLOR);
  if (bgr.empty()) {
    std::fprintf(stderr, "图片读取失败: %s\n", image_path.c_str());
    return 1;
  }

  cv::Mat rgb;
  cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
  w10::LetterboxInfo info{};
  const std::vector<float> input =
      w10::LetterboxToTensor(rgb, kInput, kInput, info);

  const std::string engine_path = trt::BuildOrLoadEngine(trt::BuildConfig{
      .onnx_path = TRT_MODEL_PATH,
      .cache_dir = TRT_ENGINE_CACHE_DIR,
      .precision = trt::Precision::kFp16,
  });
  trt::TrtEngine engine(engine_path);

  for (int i = 0; i < kWarmup; ++i) {
    static_cast<void>(engine.Infer(input));
  }

  quant::RollingStats stats(128);
  for (int i = 0; i < kIters; ++i) {
    const auto start = std::chrono::steady_clock::now();
    static_cast<void>(engine.Infer(input));
    const auto end = std::chrono::steady_clock::now();
    const double ms =
        std::chrono::duration<double, std::milli>(end - start).count();
    stats.Add(quant::StageLatencyMs{.infer = ms, .total = ms});
  }
  const quant::StageLatencyStats summary = stats.Summary();

  const double engine_mb =
      static_cast<double>(std::filesystem::file_size(engine_path)) /
      (1024.0 * 1024.0);
  std::printf("# trt M1 Benchmark（TRT FP16 纯 infer）\n\n");
  std::printf("- image: `%s`\n", image_path.c_str());
  std::printf("- engine: `%s`（%.1f MB）\n", engine_path.c_str(), engine_mb);
  std::printf(
      "- 口径: 含 H2D/D2H 与 stream 同步（对齐 ORT Run），warmup=%d "
      "iters=%d\n\n",
      kWarmup, kIters);
  std::printf("| 路线 | P50(ms) | P99(ms) | FPS |\n");
  std::printf("|---|---:|---:|---:|\n");
  std::printf("| CUDA EP FP32（quant 同机引用） | %.2f | — | %.1f |\n",
              kCudaEpFp32P50Ms, 1000.0 / kCudaEpFp32P50Ms);
  std::printf("| TRT FP16（实测） | %.2f | %.2f | %.1f |\n", summary.infer.p50,
              summary.infer.p99, 1000.0 / summary.infer.p50);
  std::printf(
      "\n> TRT FP16 vs CUDA EP FP32: %+.1f%%（负值 = TRT 更快）\n",
      (summary.infer.p50 - kCudaEpFp32P50Ms) * 100.0 / kCudaEpFp32P50Ms);
  return 0;
}
