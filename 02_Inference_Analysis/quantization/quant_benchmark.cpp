// SPDX-License-Identifier: MIT
//
// 文件功能：quant YOLO 部署硬化 benchmark —— 纯 ORT 与端到端 Markdown 报告。

#include "custom_resize.hpp"
#include "eval_harness.hpp"
#include "inference_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr int kInput = 640;
constexpr int kWarmup = 5;
constexpr int kIters = 20;
constexpr double kNoisePct = 3.0;

struct InferStat {
  double p50_ms = 0.0;
  double p99_ms = 0.0;
  double fps = 0.0;
};

struct InferRow {
  std::string model;
  std::string requested_ep;
  std::string active_ep;
  std::string mode;
  double p50_ms = 0.0;
  double p99_ms = 0.0;
  double fps = 0.0;
  std::string fallback_reason;
};

struct PipelineConfig {
  const char* name;
  bool use_iobinding;
  bool skip_non_finite;
  int reserve_hint;
  int max_det;
};

[[nodiscard]] const char* EpName(w14::Ep ep) {
  return ep == w14::Ep::kCuda ? "CUDA" : "CPU";
}

[[nodiscard]] std::string ModelName(const std::string& path,
                                    std::size_t index) {
  if (index == 0) {
    return "fp32";
  }
  const std::string stem = std::filesystem::path(path).stem().string();
  return stem.empty() ? "model_" + std::to_string(index) : stem;
}

[[nodiscard]] std::vector<quant::ModelCase> ParseCases(int argc, char** argv) {
  std::vector<quant::ModelCase> cases;
  if (argc <= 2) {
    cases.push_back(
        quant::ModelCase{.name = "fp32", .model_path = QUANT_W16_MODEL_PATH});
    return cases;
  }

  for (int i = 2; i < argc; ++i) {
    cases.push_back(quant::ModelCase{
        .name = ModelName(argv[i], static_cast<std::size_t>(i - 2)),
        .model_path = argv[i],
    });
  }
  return cases;
}

[[nodiscard]] double Percentile(std::vector<double> values, double q) {
  if (values.empty()) {
    return 0.0;
  }
  std::sort(values.begin(), values.end());
  const double rank = std::ceil(q * static_cast<double>(values.size()));
  const std::size_t idx = std::clamp(static_cast<std::size_t>(rank),
                                     std::size_t{1}, values.size()) -
                          1;
  return values[idx];
}

void RunOnce(w14::InferenceEngine& engine, const std::vector<float>& input,
             const std::vector<int64_t>& shape, bool use_iobinding) {
  std::vector<Ort::Value> outputs = use_iobinding
                                        ? engine.RunIoBinding(input, shape)
                                        : engine.Run(input, shape);
  if (outputs.empty()) {
    throw std::runtime_error("ORT 推理没有返回输出");
  }
}

[[nodiscard]] InferStat BenchInfer(w14::InferenceEngine& engine,
                                   const std::vector<float>& input,
                                   const std::vector<int64_t>& shape,
                                   bool use_iobinding) {
  for (int i = 0; i < kWarmup; ++i) {
    RunOnce(engine, input, shape, use_iobinding);
  }

  std::vector<double> latencies;
  latencies.reserve(kIters);
  for (int i = 0; i < kIters; ++i) {
    const auto start = std::chrono::steady_clock::now();
    RunOnce(engine, input, shape, use_iobinding);
    const auto end = std::chrono::steady_clock::now();
    latencies.push_back(
        std::chrono::duration<double, std::milli>(end - start).count());
  }

  const double p50 = Percentile(latencies, 0.50);
  return InferStat{.p50_ms = p50,
                   .p99_ms = Percentile(latencies, 0.99),
                   .fps = 1000.0 / p50};
}

[[nodiscard]] std::vector<float> PrepareInput(const cv::Mat& bgr) {
  cv::Mat rgb;
  cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
  w10::LetterboxInfo info{};
  return w10::LetterboxToTensor(rgb, kInput, kInput, info);
}

void PrintFallbackNotice(const std::vector<InferRow>& rows) {
  for (const InferRow& row : rows) {
    if (row.requested_ep == "CUDA" && row.active_ep == "CPU" &&
        !row.fallback_reason.empty()) {
      std::printf("> CUDA fallback: model=%s mode=%s reason=%s\n",
                  row.model.c_str(), row.mode.c_str(),
                  row.fallback_reason.c_str());
    }
  }
}

void PrintIoBindingDelta(const std::vector<InferRow>& rows) {
  std::printf("\n## Run vs IOBinding 对比\n");
  std::printf(
      "| 模型 | EP | Run P50(ms) | IOBinding P50(ms) | 变化 | 结论 |\n");
  std::printf("|---|---|---:|---:|---:|---|\n");
  for (std::size_t i = 0; i + 1 < rows.size(); i += 2) {
    const InferRow& run = rows[i];
    const InferRow& binding = rows[i + 1];
    if (run.mode != "Run" || binding.mode != "IOBinding" ||
        run.model != binding.model ||
        run.requested_ep != binding.requested_ep) {
      continue;
    }
    const double delta = (run.p50_ms - binding.p50_ms) * 100.0 / run.p50_ms;
    const char* verdict =
        std::fabs(delta) < kNoisePct
            ? "噪声区间"
            : (delta > 0.0 ? "IOBinding 更快" : "IOBinding 更慢");
    std::printf("| %s | %s/%s | %.2f | %.2f | %.1f%% | %s |\n",
                run.model.c_str(), run.requested_ep.c_str(),
                run.active_ep.c_str(), run.p50_ms, binding.p50_ms, delta,
                verdict);
  }
}

}  // namespace

int main(int argc, char** argv) {
  const std::string image_path = argc > 1 ? argv[1] : QUANT_W16_IMAGE_PATH;
  const cv::Mat bgr = cv::imread(image_path, cv::IMREAD_COLOR);
  if (bgr.empty()) {
    std::fprintf(stderr, "图片读取失败: %s\n", image_path.c_str());
    return 1;
  }

  std::vector<quant::ModelCase> cases = ParseCases(argc, argv);
  cases.erase(std::remove_if(cases.begin(), cases.end(),
                             [](const quant::ModelCase& model) {
                               if (std::filesystem::exists(model.model_path)) {
                                 return false;
                               }
                               std::printf("> 跳过缺失模型: %s\n",
                                           model.model_path.c_str());
                               return true;
                             }),
              cases.end());
  if (cases.empty()) {
    std::fprintf(stderr, "没有可用模型\n");
    return 1;
  }

  const std::vector<float> input = PrepareInput(bgr);
  const std::vector<int64_t> shape{1, 3, kInput, kInput};

  std::printf("# Quant YOLO 部署硬化 Benchmark\n\n");
  std::printf("- image: `%s`\n", image_path.c_str());
  std::printf("- warmup: %d\n", kWarmup);
  std::printf("- iters: %d\n", kIters);

  std::printf("\n## 配置对照\n");
  std::printf(
      "| 配置 | use_iobinding | skip_non_finite | reserve_hint | max_det |\n");
  std::printf("|---|---:|---:|---:|---:|\n");
  std::printf("| W16 default | false | false | 0 | 0 |\n");
  std::printf("| quant hardened | true | true | 512 | 300 |\n\n");

  std::printf("## 纯 ORT infer\n");
  std::printf(
      "| 模型 | 请求 EP | 实际 EP | 模式 | P50(ms) | P99(ms) | FPS |\n");
  std::printf("|---|---|---|---|---:|---:|---:|\n");

  std::vector<InferRow> infer_rows;
  for (const quant::ModelCase& model : cases) {
    for (w14::Ep requested_ep : {w14::Ep::kCpu, w14::Ep::kCuda}) {
      w14::InferenceEngine engine(model.model_path,
                                  w14::SessionConfig{.ep = requested_ep});
      for (bool use_iobinding : {false, true}) {
        const InferStat stat = BenchInfer(engine, input, shape, use_iobinding);
        InferRow row{
            .model = model.name,
            .requested_ep = EpName(requested_ep),
            .active_ep = EpName(engine.ActiveEp()),
            .mode = use_iobinding ? "IOBinding" : "Run",
            .p50_ms = stat.p50_ms,
            .p99_ms = stat.p99_ms,
            .fps = stat.fps,
            .fallback_reason = engine.EpFallbackReason(),
        };
        std::printf("| %s | %s | %s | %s | %.2f | %.2f | %.1f |\n",
                    row.model.c_str(), row.requested_ep.c_str(),
                    row.active_ep.c_str(), row.mode.c_str(), row.p50_ms,
                    row.p99_ms, row.fps);
        infer_rows.push_back(std::move(row));
      }
    }
  }
  PrintFallbackNotice(infer_rows);
  PrintIoBindingDelta(infer_rows);

  std::printf("\n## 端到端 baseline vs hardened pipeline\n");
  std::printf(
      "| 配置 | 模型 | 请求 EP | 实际 EP | pre P50 | infer P50 | post P50 | "
      "total P50 | total P99 | FPS | dets | top score |\n");
  std::printf("|---|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|\n");

  const std::vector<PipelineConfig> pipeline_configs{
      PipelineConfig{.name = "W16 default",
                     .use_iobinding = false,
                     .skip_non_finite = false,
                     .reserve_hint = 0,
                     .max_det = 0},
      PipelineConfig{.name = "quant hardened",
                     .use_iobinding = true,
                     .skip_non_finite = true,
                     .reserve_hint = 512,
                     .max_det = 300},
  };
  for (const quant::ModelCase& model : cases) {
    for (w14::Ep requested_ep : {w14::Ep::kCpu, w14::Ep::kCuda}) {
      for (const PipelineConfig& pipeline : pipeline_configs) {
        quant::EvalConfig config{
            .detector =
                w16::DetectorConfig{
                    .ep = requested_ep,
                    .use_iobinding = pipeline.use_iobinding,
                    .skip_non_finite = pipeline.skip_non_finite,
                    .reserve_hint = pipeline.reserve_hint,
                    .max_det = pipeline.max_det,
                },
            .warmup = kWarmup,
            .iters = kIters,
            .stats_window = 128,
        };
        quant::EvalHarness harness(config);
        const std::vector<quant::ModelResult> results =
            harness.Run({model}, image_path);
        const quant::ModelResult& result = results[0];
        const double fps = 1000.0 / result.latency.total.p50;
        std::printf(
            "| %s | %s | %s | %s | %.2f | %.2f | %.2f | %.2f | %.2f | %.1f "
            "| %zu | %.4f |\n",
            pipeline.name, result.name.c_str(), EpName(requested_ep),
            EpName(result.active_ep), result.latency.pre.p50,
            result.latency.infer.p50, result.latency.post.p50,
            result.latency.total.p50, result.latency.total.p99, fps,
            result.detection_count, result.top_score);
        if (requested_ep == w14::Ep::kCuda &&
            result.active_ep == w14::Ep::kCpu &&
            !result.ep_fallback_reason.empty()) {
          std::printf(
              "> CUDA fallback: config=%s model=%s pipeline reason=%s\n",
              pipeline.name, result.name.c_str(),
              result.ep_fallback_reason.c_str());
        }
      }
    }
  }

  return 0;
}
