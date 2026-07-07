// SPDX-License-Identifier: MIT
//
// 文件功能：trt engine 构建器实现 —— 详见 engine_builder.hpp。

#include "engine_builder.hpp"

#include "trt_common.hpp"

#include <NvInfer.h>
#include <NvOnnxParser.h>
#include <cstdint>
#include <cuda_runtime_api.h>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace trt {

namespace {

// FNV-1a 64bit：内容指纹（非加密用途），避免为一个哈希引第三方库。
[[nodiscard]] std::uint64_t Fnv1aHash(const std::vector<char>& data) {
  std::uint64_t hash = 1469598103934665603ULL;
  for (const char c : data) {
    hash ^= static_cast<unsigned char>(c);
    hash *= 1099511628211ULL;
  }
  return hash;
}

[[nodiscard]] std::string SmTag() {
  int device = 0;
  TRT_CUDA_CHECK(cudaGetDevice(&device));
  cudaDeviceProp prop{};
  TRT_CUDA_CHECK(cudaGetDeviceProperties(&prop, device));
  return std::format("sm{}{}", prop.major, prop.minor);
}

[[nodiscard]] const char* PrecisionTag(Precision precision) {
  return precision == Precision::kFp16 ? "fp16" : "fp32";
}

}  // namespace

std::string BuildOrLoadEngine(const BuildConfig& config) {
  const std::vector<char> onnx_bytes = ReadFileBytes(config.onnx_path);

  const std::string stem =
      std::filesystem::path(config.onnx_path).stem().string();
  // getInferLibVersion()：运行时链到的 TRT 版本（如 101601），比头文件宏更真。
  const std::string cache_name = std::format(
      "{}.{}.{}.trt{}.{:016x}.plan", stem, PrecisionTag(config.precision),
      SmTag(), getInferLibVersion(), Fnv1aHash(onnx_bytes));
  const std::filesystem::path cache_path =
      std::filesystem::path(config.cache_dir) / cache_name;

  if (std::filesystem::exists(cache_path)) {
    return cache_path.string();
  }

  // Builder 链：IBuilder → INetworkDefinition ← IParser，IBuilderConfig 调参，
  // buildSerializedNetwork 直接产出序列化 engine（TRT 10 已移除
  // buildEngineWithConfig）。TRT 10 接口支持直接 delete，unique_ptr 即 RAII。
  std::unique_ptr<nvinfer1::IBuilder> builder(
      nvinfer1::createInferBuilder(GlobalLogger()));
  if (builder == nullptr) {
    throw std::runtime_error("[trt] createInferBuilder 失败");
  }

  // TRT 10 网络恒为 explicit batch，flags 传 0。
  std::unique_ptr<nvinfer1::INetworkDefinition> network(
      builder->createNetworkV2(0U));
  if (network == nullptr) {
    throw std::runtime_error("[trt] createNetworkV2 失败");
  }

  // parser 持有 network 引用：两者生命周期都要覆盖到 build 结束。
  std::unique_ptr<nvonnxparser::IParser> parser(
      nvonnxparser::createParser(*network, GlobalLogger()));
  if (parser == nullptr) {
    throw std::runtime_error("[trt] createParser 失败");
  }
  if (!parser->parseFromFile(
          config.onnx_path.c_str(),
          static_cast<int>(nvinfer1::ILogger::Severity::kWARNING))) {
    throw std::runtime_error("[trt] ONNX 解析失败（详见上方 TRT 日志）: " +
                             config.onnx_path);
  }

  std::unique_ptr<nvinfer1::IBuilderConfig> builder_config(
      builder->createBuilderConfig());
  if (builder_config == nullptr) {
    throw std::runtime_error("[trt] createBuilderConfig 失败");
  }
  builder_config->setMemoryPoolLimit(nvinfer1::MemoryPoolType::kWORKSPACE,
                                     config.workspace_bytes);
  if (config.precision == Precision::kFp16) {
    builder_config->setFlag(nvinfer1::BuilderFlag::kFP16);
  }

  // W16 的 yolov8n.onnx 是全动态维度导出（batch/height/width 均为符号维，
  // Task 1 trtexec 实测确认），动态输入必须挂 optimization profile 才能 build。
  // YAGNI：min=opt=max 全锁 1×3×640×640，engine 等效静态。
  // profile 由 builder 持有，不需要（也不能）手动释放。
  nvinfer1::IOptimizationProfile* profile =
      builder->createOptimizationProfile();
  if (profile == nullptr) {
    throw std::runtime_error("[trt] createOptimizationProfile 失败");
  }
  const nvinfer1::Dims4 fixed_shape{1, 3, 640, 640};
  const char* input_name = network->getInput(0)->getName();
  if (!profile->setDimensions(input_name, nvinfer1::OptProfileSelector::kMIN,
                              fixed_shape) ||
      !profile->setDimensions(input_name, nvinfer1::OptProfileSelector::kOPT,
                              fixed_shape) ||
      !profile->setDimensions(input_name, nvinfer1::OptProfileSelector::kMAX,
                              fixed_shape)) {
    throw std::runtime_error("[trt] profile setDimensions 失败");
  }
  if (builder_config->addOptimizationProfile(profile) < 0) {
    throw std::runtime_error("[trt] addOptimizationProfile 失败");
  }

  std::unique_ptr<nvinfer1::IHostMemory> serialized(
      builder->buildSerializedNetwork(*network, *builder_config));
  if (serialized == nullptr) {
    throw std::runtime_error("[trt] engine 构建失败（详见上方 TRT 日志）: " +
                             config.onnx_path);
  }

  std::filesystem::create_directories(config.cache_dir);
  // 先写 .tmp 再 rename：构建中途进程被杀不会留下半截文件被当缓存命中。
  const std::filesystem::path tmp_path{cache_path.string() + ".tmp"};
  {
    std::ofstream out(tmp_path, std::ios::binary);
    if (!out) {
      throw std::runtime_error("[trt] 无法写 engine 缓存: " +
                               tmp_path.string());
    }
    out.write(static_cast<const char*>(serialized->data()),
              static_cast<std::streamsize>(serialized->size()));
  }
  std::filesystem::rename(tmp_path, cache_path);
  return cache_path.string();
}

}  // namespace trt
