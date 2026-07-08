// SPDX-License-Identifier: MIT
//
// 文件功能：trt 运行时引擎实现 —— 详见 trt_engine.hpp。

#include "trt_engine.hpp"

#include <cuda_runtime_api.h>
#include <format>
#include <stdexcept>

namespace trt {

namespace {

[[nodiscard]] std::size_t ElementCount(const nvinfer1::Dims& dims) {
  std::size_t count = 1;
  for (std::int32_t i = 0; i < dims.nbDims; ++i) {
    count *= static_cast<std::size_t>(dims.d[i]);
  }
  return count;
}

}  // namespace

TrtEngine::TrtEngine(const std::string& engine_path) {
  const std::vector<char> blob = ReadFileBytes(engine_path);

  runtime_.reset(nvinfer1::createInferRuntime(GlobalLogger()));
  if (runtime_ == nullptr) {
    throw std::runtime_error("[trt] createInferRuntime 失败");
  }
  engine_.reset(runtime_->deserializeCudaEngine(blob.data(), blob.size()));
  if (engine_ == nullptr) {
    // 常见原因：engine 由其他 TRT 版本/显卡构建。缓存戳编在文件名里，
    // 正常流程走 BuildOrLoadEngine 不会进到这里。
    throw std::runtime_error("[trt] engine 反序列化失败: " + engine_path);
  }
  context_.reset(engine_->createExecutionContext());
  if (context_ == nullptr) {
    throw std::runtime_error("[trt] createExecutionContext 失败");
  }

  // 按 IO mode 识别输入/输出名（TRT 10 全面转向按名字寻址的 tensor API）。
  // yolov8n 恰为 1 入 1 出；多输入/多输出模型不在本交付物范围，直接报错。
  for (std::int32_t i = 0; i < engine_->getNbIOTensors(); ++i) {
    const char* name = engine_->getIOTensorName(i);
    if (engine_->getTensorIOMode(name) == nvinfer1::TensorIOMode::kINPUT) {
      if (!input_name_.empty()) {
        throw std::runtime_error("[trt] 仅支持单输入模型");
      }
      input_name_ = name;
    } else {
      if (!output_name_.empty()) {
        throw std::runtime_error("[trt] 仅支持单输出模型");
      }
      output_name_ = name;
    }
  }
  if (input_name_.empty() || output_name_.empty()) {
    throw std::runtime_error("[trt] 未找到输入/输出 tensor");
  }

  // engine 由动态 ONNX + 单 profile（min=opt=max）构建：engine 级
  // getTensorShape 对动态维返回 -1，具体形状要从 profile 取；再经
  // setInputShape 绑定后，context 级 getTensorShape 才能给出全具体的输出形状。
  nvinfer1::Dims in_dims = engine_->getTensorShape(input_name_.c_str());
  for (std::int32_t i = 0; i < in_dims.nbDims; ++i) {
    if (in_dims.d[i] < 0) {
      in_dims = engine_->getProfileShape(input_name_.c_str(), 0,
                                         nvinfer1::OptProfileSelector::kOPT);
      break;
    }
  }
  if (!context_->setInputShape(input_name_.c_str(), in_dims)) {
    throw std::runtime_error("[trt] setInputShape 失败");
  }
  const nvinfer1::Dims out_dims =
      context_->getTensorShape(output_name_.c_str());
  for (std::int32_t i = 0; i < out_dims.nbDims; ++i) {
    if (out_dims.d[i] < 0) {
      throw std::runtime_error("[trt] 输出形状未完全确定（动态维未解析）");
    }
  }
  input_count_ = ElementCount(in_dims);
  output_count_ = ElementCount(out_dims);
  output_shape_.assign(out_dims.d, out_dims.d + out_dims.nbDims);

  // FP16 engine 的网络边界 I/O 默认仍是 FP32（kFP16 只影响内部层精度），
  // 缓冲按 float 分配。
  input_buffer_ = DeviceBuffer(input_count_ * sizeof(float));
  output_buffer_ = DeviceBuffer(output_count_ * sizeof(float));
  TRT_CUDA_CHECK(cudaStreamCreate(&stream_));

  // 地址固定（缓冲随对象生存期不变），构造期一次绑定，Infer 里不再重复设置。
  if (!context_->setTensorAddress(input_name_.c_str(), input_buffer_.Get()) ||
      !context_->setTensorAddress(output_name_.c_str(), output_buffer_.Get())) {
    throw std::runtime_error("[trt] setTensorAddress 失败");
  }
}

TrtEngine::~TrtEngine() {
  if (stream_ != nullptr) {
    // 析构路径不抛：同步失败也要继续销毁 stream。
    static_cast<void>(cudaStreamSynchronize(stream_));
    static_cast<void>(cudaStreamDestroy(stream_));
  }
}

std::vector<float> TrtEngine::Infer(std::span<const float> input) {
  if (input.size() != input_count_) {
    throw std::invalid_argument(std::format(
        "[trt] 输入元素数不符: got {} want {}", input.size(), input_count_));
  }
  TRT_CUDA_CHECK(cudaMemcpyAsync(input_buffer_.Get(), input.data(),
                                 input.size_bytes(), cudaMemcpyHostToDevice,
                                 stream_));
  if (!context_->enqueueV3(stream_)) {
    throw std::runtime_error("[trt] enqueueV3 失败");
  }
  std::vector<float> output(output_count_);
  TRT_CUDA_CHECK(cudaMemcpyAsync(output.data(), output_buffer_.Get(),
                                 output_count_ * sizeof(float),
                                 cudaMemcpyDeviceToHost, stream_));
  TRT_CUDA_CHECK(cudaStreamSynchronize(stream_));
  return output;
}

}  // namespace trt
