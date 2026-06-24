// SPDX-License-Identifier: MIT
//
// 文件功能：W16 YOLOv8n 推理基准 —— batch 1 vs 4 × {CPU,CUDA} × {Run,
// IOBinding}
//           的 P50/P99 延迟 + 吞吐，含 CUDA warmup。是「工程快测数字」，
//           专业瓶颈归因留 W18（Nsight/Roofline）。
//
// 运行（CPU EP）：build 下直接跑；（CUDA EP）：用 build-gpu 且设好 CUDA LD
// 路径。

#include "custom_resize.hpp"  // w10::LetterboxToTensor
#include "inference_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <string>
#include <vector>

namespace {

constexpr int kInput = 640;
constexpr int kWarmup = 10;  // 丢弃首次 cuDNN autotuning 等冷启动开销
constexpr int kIters = 50;

struct Stat {
  double p50_ms;
  double p99_ms;
  double fps;  // 吞吐 = batch * 1000 / p50
};

double Percentile(std::vector<double> v, double q) {
  std::sort(v.begin(), v.end());
  const auto idx = static_cast<std::size_t>(q * (v.size() - 1));
  return v[idx];
}

// 把单图张量平铺 batch 份，构成 [batch,3,640,640] 连续 buffer。
std::vector<float> TileBatch(const std::vector<float>& one, int batch) {
  std::vector<float> out;
  out.reserve(one.size() * batch);
  for (int b = 0; b < batch; ++b) {
    out.insert(out.end(), one.begin(), one.end());
  }
  return out;
}

// 计时一种配置：use_binding 决定走 Run 还是 RunIoBinding。
Stat Bench(w14::InferenceEngine& engine, const std::vector<float>& input,
           const std::vector<int64_t>& shape, int batch, bool use_binding) {
  for (int i = 0; i < kWarmup; ++i) {
    if (use_binding) {
      engine.RunIoBinding(input, shape);
    } else {
      engine.Run(input, shape);
    }
  }
  std::vector<double> lat;
  lat.reserve(kIters);
  for (int i = 0; i < kIters; ++i) {
    auto t0 = std::chrono::steady_clock::now();
    if (use_binding) {
      engine.RunIoBinding(input, shape);
    } else {
      engine.Run(input, shape);
    }
    auto t1 = std::chrono::steady_clock::now();
    lat.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
  }
  const double p50 = Percentile(lat, 0.50);
  return Stat{p50, Percentile(lat, 0.99), batch * 1000.0 / p50};
}

}  // namespace

int main(int argc, char** argv) {
  const std::string model = (argc > 1) ? argv[1] : W16_MODEL_PATH;
  const std::string image = (argc > 2) ? argv[2] : W16_IMAGE_PATH;

  cv::Mat bgr = cv::imread(image, cv::IMREAD_COLOR);
  if (bgr.empty()) {
    std::fprintf(stderr, "图片读取失败: %s\n", image.c_str());
    return 1;
  }
  cv::Mat rgb;
  cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
  w10::LetterboxInfo info{};
  const std::vector<float> one =
      w10::LetterboxToTensor(rgb, kInput, kInput, info);

  std::printf("# W16 YOLOv8n 推理基准（warmup=%d, iters=%d）\n", kWarmup,
              kIters);
  std::printf("| EP | batch | 模式 | P50(ms) | P99(ms) | 吞吐(img/s) |\n");
  std::printf("|---|---|---|---|---|---|\n");

  for (w14::Ep ep : {w14::Ep::kCpu, w14::Ep::kCuda}) {
    // 每个 (ep,batch) 用独立 engine：RunIoBinding 的持久输出绑定假定输出形状
    // 固定，故不能跨 batch 复用同一 engine（batch 变 -> 输出形状变）。
    bool fellback = false;
    for (int batch : {1, 4}) {
      w14::InferenceEngine engine(model, w14::SessionConfig{.ep = ep});
      if (ep == w14::Ep::kCuda && engine.ActiveEp() == w14::Ep::kCpu) {
        fellback = true;
        break;
      }
      const char* ep_name =
          engine.ActiveEp() == w14::Ep::kCuda ? "CUDA" : "CPU";
      const std::vector<float> input = TileBatch(one, batch);
      const std::vector<int64_t> shape{batch, 3, kInput, kInput};
      const Stat run = Bench(engine, input, shape, batch, /*binding=*/false);
      const Stat bind = Bench(engine, input, shape, batch, /*binding=*/true);
      std::printf("| %s | %d | Run | %.2f | %.2f | %.1f |\n", ep_name, batch,
                  run.p50_ms, run.p99_ms, run.fps);
      std::printf("| %s | %d | IOBinding | %.2f | %.2f | %.1f |\n", ep_name,
                  batch, bind.p50_ms, bind.p99_ms, bind.fps);
    }
    if (fellback) {
      std::printf(
          "> 注：CUDA EP 不可用，已回退 CPU（用 build-gpu 构建并设好 "
          "CUDA 库路径以测 GPU）。\n");
      break;
    }
  }

  // IntraOp 线程扫描（CPU, batch=1）：展示单算子内并行度对延迟的影响。
  std::printf("\n## IntraOp 线程扫描（CPU, batch=1, Run）\n");
  std::printf("| intra_op_threads | P50(ms) | 吞吐(img/s) |\n|---|---|---|\n");
  const std::vector<int64_t> shape1{1, 3, kInput, kInput};
  for (int t : {1, 2, 4}) {
    w14::InferenceEngine eng(
        model, w14::SessionConfig{.ep = w14::Ep::kCpu, .intra_op_threads = t});
    const Stat s = Bench(eng, one, shape1, 1, /*binding=*/false);
    std::printf("| %d | %.2f | %.1f |\n", t, s.p50_ms, s.fps);
  }
  return 0;
}
