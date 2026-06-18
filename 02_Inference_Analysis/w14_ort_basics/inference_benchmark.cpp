// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：W14.5 推理延迟基准 —— 预热后多次计时取均值/最小值，量化单次推理
//           耗时，支持 CPU / CUDA EP 对比。
//
// 用法：
//   w14_inference_benchmark [--model <path>] [--cuda] [--warmup N] [--iters N]
//   预热（默认 20 次）丢弃，规避 GPU 冷启动（kernel 加载 / cuDNN
//   autotune）虚高。
// ============================================================================

#include "inference_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <format>
#include <iostream>
#include <numeric>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::vector<int64_t> ResolveDynamicShape(std::span<const int64_t> raw) {
  std::vector<int64_t> resolved(raw.begin(), raw.end());
  for (auto& d : resolved) {
    if (d <= 0) {
      d = 1;  // 动态维（batch 等）按 1 处理
    }
  }
  return resolved;
}

size_t ElementCount(std::span<const int64_t> shape) {
  size_t n = 1;
  for (auto d : shape) {
    n *= static_cast<size_t>(d);
  }
  return n;
}

}  // namespace

int main(int argc, char** argv) {
  std::string model_path =
      "02_Inference_Analysis/w14_ort_basics/models/mobilenetv2.onnx";
  w14::Ep ep = w14::Ep::kCpu;
  int warmup = 20;
  int iters = 100;

  for (int i = 1; i < argc; ++i) {
    std::string_view arg = argv[i];
    if (arg == "--model" && i + 1 < argc) {
      model_path = argv[++i];
    } else if (arg == "--cuda") {
      ep = w14::Ep::kCuda;
    } else if (arg == "--warmup" && i + 1 < argc) {
      warmup = std::stoi(argv[++i]);
    } else if (arg == "--iters" && i + 1 < argc) {
      iters = std::stoi(argv[++i]);
    } else if (arg == "-h" || arg == "--help") {
      std::cout << "用法: w14_inference_benchmark [--model <path>] [--cuda] "
                   "[--warmup N] [--iters N]\n";
      return 0;
    } else {
      std::cerr << std::format("[W14.5] 未知参数: {}\n", arg);
      return 2;
    }
  }

  try {
    w14::InferenceEngine engine(model_path, ep);
    const char* ep_name = engine.ActiveEp() == w14::Ep::kCuda ? "CUDA" : "CPU";
    std::cout << std::format("[W14.5] 实际 EP: {}\n", ep_name);
    if (!engine.EpFallbackReason().empty()) {
      std::cout << std::format("[W14.5] CUDA 回退原因: {}\n",
                               engine.EpFallbackReason());
    }

    const auto input_shape = ResolveDynamicShape(engine.Inputs()[0].shape);
    std::vector<float> input(ElementCount(input_shape));
    std::mt19937 rng{42};
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    for (auto& x : input) {
      x = dist(rng);
    }

    // 预热：丢弃，规避冷启动
    for (int i = 0; i < warmup; ++i) {
      volatile auto out = engine.Run(input, input_shape);
      (void)out;
    }

    // 计时：逐次测，取均值与最小值（min 噪声最小，最接近真实算力）
    std::vector<double> ms;
    ms.reserve(iters);
    for (int i = 0; i < iters; ++i) {
      const auto t0 = std::chrono::steady_clock::now();
      auto out = engine.Run(input, input_shape);
      const auto t1 = std::chrono::steady_clock::now();
      ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }

    const double avg = std::accumulate(ms.begin(), ms.end(), 0.0) / iters;
    const double mn = *std::min_element(ms.begin(), ms.end());
    const double mx = *std::max_element(ms.begin(), ms.end());
    std::cout << std::format(
        "[W14.5] {} | warmup {} iters {} | avg {:.2f}ms  min {:.2f}ms  "
        "max {:.2f}ms\n",
        ep_name, warmup, iters, avg, mn, mx);
  } catch (const std::exception& e) {
    std::cerr << std::format("[W14.5] 失败: {}\n", e.what());
    return 1;
  }
  return 0;
}
