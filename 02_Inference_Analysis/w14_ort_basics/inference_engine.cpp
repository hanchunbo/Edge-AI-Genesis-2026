// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：W14 InferenceEngine 类实现 —— 详见 inference_engine.hpp。
// ============================================================================

#include "inference_engine.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>

namespace w14 {

Ort::Env& InferenceEngine::GlobalEnv() {
  // C++11+ 函数内 static：线程安全 + 仅初始化一次 + 程序结束时析构。
  // ORT_LOGGING_LEVEL_WARNING：屏蔽 INFO 噪音，保留警告与错误。
  static Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "EdgeAI-W14");
  return env;
}

namespace {

InferenceEngine::IoInfo MakeIoInfo(Ort::AllocatedStringPtr name,
                                   Ort::TypeInfo type_info) {
  auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
  return InferenceEngine::IoInfo{
      .name = name.get(),
      .shape = tensor_info.GetShape(),
      .dtype = tensor_info.GetElementType(),
  };
}

}  // namespace

InferenceEngine::InferenceEngine(const std::string& model_path, Ep ep) {
  // 提前给出友好错误，避免 ORT 抛 ORTCHAR 路径相关的晦涩异常。
  if (!std::filesystem::exists(model_path)) {
    throw std::runtime_error("[W14] 模型文件不存在: " + model_path);
  }

  // CUDA 路径：try 必须包住「append + Session 创建」整体——失败既可能发生在
  // AppendExecutionProvider_CUDA，也可能发生在 Session 创建阶段（provider .so
  // 找到了，但 libcudnn.so.9 / libcublas* dlopen 失败）。任意 Ort::Exception
  // 都记录原因后落到下面的 CPU 重建路径，不向上抛。
  if (ep == Ep::kCuda) {
    try {
      Ort::SessionOptions cuda_options;
      cuda_options.SetGraphOptimizationLevel(
          GraphOptimizationLevel::ORT_ENABLE_ALL);
      OrtCUDAProviderOptions cuda_opts{};  // device_id 默认 0
      cuda_options.AppendExecutionProvider_CUDA(cuda_opts);
      session_ = std::make_unique<Ort::Session>(GlobalEnv(), model_path.c_str(),
                                                cuda_options);
      active_ep_ = Ep::kCuda;
    } catch (const Ort::Exception& e) {
      ep_fallback_reason_ = e.what();  // 记录原因，回退 CPU
    }
  }

  // CPU 路径：默认请求 或 CUDA 失败回退。CPU 路径本身失败才真正抛错。
  if (!session_) {
    try {
      Ort::SessionOptions cpu_options;
      cpu_options.SetGraphOptimizationLevel(
          GraphOptimizationLevel::ORT_ENABLE_ALL);
      session_ = std::make_unique<Ort::Session>(GlobalEnv(), model_path.c_str(),
                                                cpu_options);
      active_ep_ = Ep::kCpu;
    } catch (const Ort::Exception& e) {
      throw std::runtime_error(std::string("[W14] ORT 加载失败: ") + e.what());
    }
  }

  // 构造期一次性查出 I/O 元数据缓存，Run 热路径不再触发 ORT 内部分配。
  try {
    Ort::AllocatorWithDefaultOptions allocator;

    const size_t input_count = session_->GetInputCount();
    inputs_.reserve(input_count);
    for (size_t i = 0; i < input_count; ++i) {
      inputs_.push_back(
          MakeIoInfo(session_->GetInputNameAllocated(i, allocator),
                     session_->GetInputTypeInfo(i)));
    }

    const size_t output_count = session_->GetOutputCount();
    outputs_.reserve(output_count);
    for (size_t i = 0; i < output_count; ++i) {
      outputs_.push_back(
          MakeIoInfo(session_->GetOutputNameAllocated(i, allocator),
                     session_->GetOutputTypeInfo(i)));
    }
  } catch (const Ort::Exception& e) {
    throw std::runtime_error(std::string("[W14] ORT 元数据查询失败: ") +
                             e.what());
  }
}

InferenceEngine::~InferenceEngine() = default;
InferenceEngine::InferenceEngine(InferenceEngine&&) noexcept = default;
InferenceEngine& InferenceEngine::operator=(InferenceEngine&&) noexcept =
    default;

std::vector<Ort::Value> InferenceEngine::Run(std::span<const float> input,
                                             std::span<const int64_t> shape) {
  // 零拷贝路径：CreateTensorWithDataAsOrtValue 语义是"借用外部 buffer"，
  // 不复制；input 必须在 session_->Run 返回前保持有效。
  //
  // const_cast 安全性说明：ORT 推理路径只读 input buffer
  // （C API 接口虽然是 void*，但语义上不写）。我们用 span<const float>
  // 表达对调用方的承诺，内部转 float* 是为了对接 C ABI。
  Ort::MemoryInfo mem_info =
      Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

  Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
      mem_info, const_cast<float*>(input.data()), input.size(), shape.data(),
      shape.size());

  // ORT Run 需要 const char* 数组；从缓存的 IoInfo 现搭。
  // 一次推理两次小 vector 分配，相对毫秒级推理可忽略，换 API 简洁度值得。
  std::vector<const char*> input_names;
  input_names.reserve(inputs_.size());
  for (const auto& info : inputs_) {
    input_names.push_back(info.name.c_str());
  }

  std::vector<const char*> output_names;
  output_names.reserve(outputs_.size());
  for (const auto& info : outputs_) {
    output_names.push_back(info.name.c_str());
  }

  return session_->Run(Ort::RunOptions{nullptr}, input_names.data(),
                       &input_tensor, /*input_count=*/1, output_names.data(),
                       output_names.size());
}

}  // namespace w14
