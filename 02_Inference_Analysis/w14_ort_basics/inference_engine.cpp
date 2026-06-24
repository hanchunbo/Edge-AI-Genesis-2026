// SPDX-License-Identifier: MIT
//
// 文件功能：W14 InferenceEngine 类实现 —— 详见 inference_engine.hpp。

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

// 把线程配置应用到 SessionOptions（0 表示不设、用 ORT 默认）。
void ApplyThreads(Ort::SessionOptions& opts, const SessionConfig& cfg) {
  if (cfg.intra_op_threads > 0) {
    opts.SetIntraOpNumThreads(cfg.intra_op_threads);
  }
  if (cfg.inter_op_threads > 0) {
    opts.SetInterOpNumThreads(cfg.inter_op_threads);
  }
}

}  // namespace

// 既有 (model_path, Ep) 入口委托到 SessionConfig 版本，线程数取默认，
// 保证 W15/W16 等既有调用方零改动。
InferenceEngine::InferenceEngine(const std::string& model_path, Ep ep)
    : InferenceEngine(model_path, SessionConfig{.ep = ep}) {}

InferenceEngine::InferenceEngine(const std::string& model_path,
                                 const SessionConfig& cfg) {
  const Ep ep = cfg.ep;
  // 提前给出友好错误，避免 ORT 抛 ORTCHAR 路径相关的晦涩异常。
  if (!std::filesystem::exists(model_path)) {
    throw std::runtime_error("[W14] 模型文件不存在: " + model_path);
  }

  // 必须先初始化全局 Ort::Env（它在构造时注册进程级默认日志器），
  // 再 AppendExecutionProvider_CUDA。append 内部会用默认日志器，若此时
  // Env 尚未创建，会抛 "DefaultLogger but none has been registered"，
  // 导致 CUDA 可用却被误判失败、回退 CPU。该 bug 只在进程内首次构造时复现：
  // 单测因前序用例已建过 Env 而侥幸通过，是典型的顺序依赖假绿。
  Ort::Env& env = GlobalEnv();

  // CUDA 路径：try 必须包住「append + Session 创建」整体——失败既可能发生在
  // AppendExecutionProvider_CUDA，也可能发生在 Session 创建阶段（provider .so
  // 找到了，但 libcudnn.so.9 / libcublas* dlopen 失败）。任意 Ort::Exception
  // 都记录原因后落到下面的 CPU 重建路径，不向上抛。
  if (ep == Ep::kCuda) {
    try {
      Ort::SessionOptions cuda_options;
      cuda_options.SetGraphOptimizationLevel(
          GraphOptimizationLevel::ORT_ENABLE_ALL);
      ApplyThreads(cuda_options, cfg);
      OrtCUDAProviderOptions cuda_opts{};  // device_id 默认 0
      cuda_options.AppendExecutionProvider_CUDA(cuda_opts);
      session_ =
          std::make_unique<Ort::Session>(env, model_path.c_str(), cuda_options);
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
      ApplyThreads(cpu_options, cfg);
      session_ =
          std::make_unique<Ort::Session>(env, model_path.c_str(), cpu_options);
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

std::vector<Ort::Value> InferenceEngine::RunIoBinding(
    std::span<const float> input, std::span<const int64_t> shape) {
  Ort::MemoryInfo mem_info =
      Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

  // 惰性创建持久 binding，并把输出绑定一次（输出形状固定，跨 Run 复用 ORT
  // 内部分配的缓冲——这正是 IOBinding 相对 Run 省开销的地方）。
  if (!io_binding_) {
    io_binding_ = std::make_unique<Ort::IoBinding>(*session_);
    for (const auto& out : outputs_) {
      io_binding_->BindOutput(out.name.c_str(), mem_info);
    }
  }

  // 输入 buffer 每次不同，只重绑输入（仍是零拷贝借用）。
  Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
      mem_info, const_cast<float*>(input.data()), input.size(), shape.data(),
      shape.size());
  io_binding_->ClearBoundInputs();
  io_binding_->BindInput(inputs_[0].name.c_str(), input_tensor);

  session_->Run(Ort::RunOptions{nullptr}, *io_binding_);
  return io_binding_->GetOutputValues();
}

}  // namespace w14
