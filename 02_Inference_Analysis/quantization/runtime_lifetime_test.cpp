// SPDX-License-Identifier: MIT
//
// 文件功能：独立进程验证显式 ORT Env 经引擎、检测器、harness 传递后的退出。

#include "eval_harness.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <format>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

/// 验证借用 Env 的两个引擎调用路径都返回完整、有限的 YOLO 张量。
/// @param construction_only 为 true 时只验证构造/销毁，供 Valgrind 使用。
void CheckEngine(Ort::Env& env, bool construction_only) {
  w14::InferenceEngine engine(QUANT_W16_MODEL_PATH,
                              w14::SessionConfig{.ep = w14::Ep::kCpu,
                                                 .intra_op_threads = 1,
                                                 .inter_op_threads = 1,
                                                 .env = &env});
  if (construction_only) {
    return;
  }
  const std::vector<float> input(3 * 640 * 640, 0.5f);
  const std::array<int64_t, 4> shape{1, 3, 640, 640};
  for (bool binding : {false, true}) {
    auto outputs =
        binding ? engine.RunIoBinding(input, shape) : engine.Run(input, shape);
    const auto count =
        outputs.at(0).GetTensorTypeAndShapeInfo().GetElementCount();
    if (count != 84 * 8400) {
      throw std::runtime_error("引擎输出张量大小错误");
    }
    const float* data = outputs[0].GetTensorData<float>();
    if (!std::all_of(data, data + count,
                     [](float value) { return std::isfinite(value); })) {
      throw std::runtime_error("引擎输出存在非有限值");
    }
  }
}

/// 验证 Env 经过两层检测配置传递时仍可完成真实单图检测。
/// @param construction_only 为 true 时仅构造检测器，不运行检测或 harness。
void CheckPipeline(Ort::Env& env, bool construction_only) {
  // Valgrind 串行模拟线程；固定单线程避免线程池自旋掩盖退出检查。
  const w16::DetectorConfig cfg{.ep = w14::Ep::kCpu,
                                .intra_op_threads = 1,
                                .inter_op_threads = 1,
                                .use_iobinding = true,
                                .env = &env};
  {
    w16::YOLODetector detector(QUANT_W16_MODEL_PATH, cfg);
    if (construction_only) {
      return;
    }
    if (detector.Detect(std::string(QUANT_W16_IMAGE_PATH)).size() != 5) {
      throw std::runtime_error("检测器输出框数错误");
    }
  }
  const quant::EvalHarness harness(quant::EvalConfig{
      .detector = cfg, .warmup = 0, .iters = 1, .stats_window = 1});
  const auto results =
      harness.Run({{"fp32", QUANT_W16_MODEL_PATH}}, QUANT_W16_IMAGE_PATH);
  if (results.at(0).detection_count != 5) {
    throw std::runtime_error("评估框架输出框数错误");
  }
}

}  // namespace

/// 以独立进程覆盖静态析构；--cuda-registry 用于 Valgrind 退出顺序回归。
/// @note Valgrind 原始报告仍需检查 WSL 驱动映射读，不能只看退出码。
int main(int argc, char** argv) {
  const bool cuda_registry =
      argc == 2 && std::string(argv[1]) == "--cuda-registry";
  if (argc > 1 && !cuda_registry) {
    return 2;
  }
  if (!std::filesystem::exists(QUANT_W16_MODEL_PATH) ||
      !std::filesystem::exists(QUANT_W16_IMAGE_PATH)) {
    std::fputs("SKIP: 缺少 YOLO 模型或测试图\n", stdout);
    return 77;
  }
  if (cuda_registry) {
    const auto providers = Ort::GetAvailableProviders();
    if (std::find(providers.begin(), providers.end(),
                  "CUDAExecutionProvider") == providers.end()) {
      std::fputs("SKIP: ORT 包不包含 CUDA EP\n", stdout);
      return 77;
    }
  }
  try {
    Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "quant_lifetime_test");
    // 退出 UAF 仅靠构造即可触发；Valgrind 不模拟昂贵的卷积运算。
    CheckEngine(env, cuda_registry);
    CheckPipeline(env, cuda_registry);
    if (cuda_registry) {
      // 注意：必须在调用引擎之后注册；若配置丢失并初始化了旧 static Env，
      // 则 CUDA 注册表会先静态析构，最后 Env 清理会访问已释放的控制块。
      Ort::SessionOptions options;
      OrtCUDAProviderOptions cuda{};
      options.AppendExecutionProvider_CUDA(cuda);
    }
    std::fputs("所有会话及输出已销毁，即将销毁局部 Env\n", stdout);
  } catch (const std::exception& error) {
    const auto message = std::format("生命周期回归失败：{}\n", error.what());
    std::fputs(message.c_str(), stderr);
    return 1;
  }
  std::fputs("Env 已销毁，正常返回 main\n", stdout);
  return 0;
}
