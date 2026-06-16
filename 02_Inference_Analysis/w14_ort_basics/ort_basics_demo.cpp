// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：W14 ORT 基础闭环 demo —— 加载 ONNX 模型、打印 I/O 节点元数据、
//           生成随机输入、跑一次推理、打印输出 shape 与 Top-1 索引。
//
// 用法：
//   w14_ort_basics_demo [--model <path>]
//   默认模型路径：02_Inference_Analysis/w14_ort_basics/models/mobilenetv2.onnx
// ============================================================================

#include "inference_engine.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <format>
#include <iostream>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::string DtypeToString(ONNXTensorElementDataType dt) {
  switch (dt) {
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT:
      return "float32";
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16:
      return "float16";
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64:
      return "int64";
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32:
      return "int32";
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8:
      return "uint8";
    default:
      return std::format("dtype#{}", static_cast<int>(dt));
  }
}

std::string ShapeToString(std::span<const int64_t> shape) {
  std::string out = "[";
  for (size_t i = 0; i < shape.size(); ++i) {
    if (i > 0) {
      out += ",";
    }
    out += std::format("{}", shape[i]);
  }
  out += "]";
  return out;
}

// ONNX 形状中 -1 / 0 表示动态维（如 batch、序列长度）。
// 推理实际跑起来必须给定具体值；分类模型常见做法：动态维一律按 1 处理。
std::vector<int64_t> ResolveDynamicShape(std::span<const int64_t> raw) {
  std::vector<int64_t> resolved(raw.begin(), raw.end());
  for (auto& d : resolved) {
    if (d <= 0) {
      d = 1;
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

  for (int i = 1; i < argc; ++i) {
    std::string_view arg = argv[i];
    if (arg == "--model" && i + 1 < argc) {
      model_path = argv[++i];
    } else if (arg == "--cuda") {
      ep = w14::Ep::kCuda;
    } else if (arg == "-h" || arg == "--help") {
      std::cout << "用法: w14_ort_basics_demo [--model <path>] [--cuda]\n";
      return 0;
    } else {
      std::cerr << std::format("[W14] 未知参数: {}\n", arg);
      return 2;
    }
  }

  std::cout << std::format("[W14] ONNX Runtime 版本: {}\n",
                           Ort::GetVersionString());
  std::cout << std::format("[W14] 加载模型: {}\n", model_path);

  try {
    w14::InferenceEngine engine(model_path, ep);

    const char* ep_name = engine.ActiveEp() == w14::Ep::kCuda ? "CUDA" : "CPU";
    std::cout << std::format("[W14] 实际 EP: {}\n", ep_name);
    if (!engine.EpFallbackReason().empty()) {
      std::cout << std::format("[W14] CUDA 回退原因: {}\n",
                               engine.EpFallbackReason());
    }

    std::cout << "[W14] === I/O 元数据 ===\n";
    for (const auto& info : engine.Inputs()) {
      std::cout << std::format("  Input : {} {} {}\n", info.name,
                               ShapeToString(info.shape),
                               DtypeToString(info.dtype));
    }
    for (const auto& info : engine.Outputs()) {
      std::cout << std::format("  Output: {} {} {}\n", info.name,
                               ShapeToString(info.shape),
                               DtypeToString(info.dtype));
    }

    // 简化假设：单输入 float32（W14 目标模型 MobileNetV2 / ResNet18 都满足）。
    // 多输入 / 非 float32 留到 W15 接前后处理时再扩展。
    if (engine.Inputs().size() != 1) {
      throw std::runtime_error("W14 demo 暂只支持单输入模型");
    }
    if (engine.Inputs()[0].dtype != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
      throw std::runtime_error("W14 demo 暂只支持 float32 输入");
    }

    const auto input_shape = ResolveDynamicShape(engine.Inputs()[0].shape);
    const size_t elem_count = ElementCount(input_shape);
    std::vector<float> input_buffer(elem_count);

    // 固定种子保证 demo 多次运行 Top-1 一致，方便日志比对。
    std::mt19937 rng{42};
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    for (auto& x : input_buffer) {
      x = dist(rng);
    }

    std::cout << std::format("[W14] 输入实际 shape: {} (元素数 {})\n",
                             ShapeToString(input_shape), elem_count);

    auto outputs = engine.Run(input_buffer, input_shape);
    if (outputs.empty()) {
      throw std::runtime_error("推理无输出");
    }

    Ort::Value& out0 = outputs[0];
    auto out_info = out0.GetTensorTypeAndShapeInfo();
    const std::vector<int64_t> out_shape = out_info.GetShape();
    const size_t out_count = out_info.GetElementCount();
    const float* out_data = out0.GetTensorData<float>();

    const float* max_it = std::max_element(out_data, out_data + out_count);
    const size_t top1_idx = static_cast<size_t>(max_it - out_data);

    std::cout << std::format("[W14] 输出 [{}] shape: {}\n",
                             engine.Outputs()[0].name,
                             ShapeToString(out_shape));
    std::cout << std::format("[W14] Top-1 索引: {} (logit={:.4f})\n", top1_idx,
                             *max_it);
  } catch (const std::exception& e) {
    std::cerr << std::format("[W14] 失败: {}\n", e.what());
    return 1;
  }

  return 0;
}
