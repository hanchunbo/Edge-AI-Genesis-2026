// SPDX-License-Identifier: MIT
//
// 文件功能：W14 InferenceEngine GTest 单测 —— 覆盖错误路径、Env 单例、
//           以及依赖 MobileNetV2 模型的加载 / I/O 元数据 / 零拷贝推理。
//
// 模型路径由 CMake 编译期常量 W14_MODEL_PATH 提供（见同目录 CMakeLists.txt）。
// 模型未下载时，依赖模型的测试用 GTEST_SKIP() 跳过，不算失败。

#include "inference_engine.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <gtest/gtest.h>
#include <memory>
#include <onnxruntime_cxx_api.h>
#include <stdexcept>
#include <vector>

namespace {

constexpr const char* kModelPath = W14_MODEL_PATH;

// 共享 fixture：所有依赖模型的测试都先检查文件存在性，
// 缺失则 SKIP（保持 CI / 新机器初次拉取代码时 ctest 不报红）。
class InferenceEngineModelTest : public ::testing::Test {
 protected:
  void SetUp() override {
    if (!std::filesystem::exists(kModelPath)) {
      GTEST_SKIP() << "模型未下载，跳过：" << kModelPath
                   << "（下载方式见 README.md「W14+ 额外依赖」节）";
    }
  }
};

}  // namespace

TEST_F(InferenceEngineModelTest, LoadsModel) {
  // RAII 加载：构造无异常即视为通过。析构由 unique_ptr 自动释放 Session，
  // 单次构造观测不到 leak；周期性泄漏需在 demo 上跑 valgrind 看（W14 不做）。
  EXPECT_NO_THROW({ w14::InferenceEngine engine(kModelPath); });
}

TEST_F(InferenceEngineModelTest, QueriesIoMetadata) {
  w14::InferenceEngine engine(kModelPath);

  // MobileNetV2 是单输入单输出分类网络。
  ASSERT_EQ(engine.Inputs().size(), 1u);
  ASSERT_EQ(engine.Outputs().size(), 1u);

  const auto& in = engine.Inputs()[0];
  EXPECT_FALSE(in.name.empty());
  EXPECT_EQ(in.dtype, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
  // 形状 [-1, 3, 224, 224]：batch 维动态，其余三维固定。
  ASSERT_EQ(in.shape.size(), 4u);
  EXPECT_LE(in.shape[0], 0);  // 动态维通常 < 0
  EXPECT_EQ(in.shape[1], 3);
  EXPECT_EQ(in.shape[2], 224);
  EXPECT_EQ(in.shape[3], 224);

  const auto& out = engine.Outputs()[0];
  EXPECT_FALSE(out.name.empty());
  EXPECT_EQ(out.dtype, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
  // 输出 [-1, 1000]：ImageNet 1000 类 logits。
  ASSERT_EQ(out.shape.size(), 2u);
  EXPECT_LE(out.shape[0], 0);
  EXPECT_EQ(out.shape[1], 1000);
}

TEST_F(InferenceEngineModelTest, RunsZeroCopyInference) {
  w14::InferenceEngine engine(kModelPath);

  // 显式控制 input buffer 的生命周期：必须存活到 Run 返回后再被读完，
  // 因为 outputs[0] 可能持有指回 input 的 view（实际上 ORT 复制了输出数据，
  // 但 input 借用约定要求 buffer 在 Run 期间存活）。
  std::vector<float> input(1 * 3 * 224 * 224, 0.5f);
  std::vector<int64_t> shape = {1, 3, 224, 224};

  auto outputs = engine.Run(input, shape);
  ASSERT_EQ(outputs.size(), 1u);

  Ort::Value& out0 = outputs[0];
  auto out_info = out0.GetTensorTypeAndShapeInfo();
  const std::vector<int64_t> out_shape = out_info.GetShape();
  ASSERT_EQ(out_shape.size(), 2u);
  EXPECT_EQ(out_shape[0], 1);
  EXPECT_EQ(out_shape[1], 1000);
  EXPECT_EQ(out_info.GetElementCount(), 1000u);

  // 简单 sanity：所有元素都是有限数（推理成功 ≠ NaN/Inf 噪音）。
  const float* data = out0.GetTensorData<float>();
  for (size_t i = 0; i < out_info.GetElementCount(); ++i) {
    ASSERT_TRUE(std::isfinite(data[i])) << "logit[" << i << "] 非有限";
  }
}

// 错误路径不依赖模型存在，独立 TEST 而不挂 fixture。
TEST(InferenceEngineErrorTest, FailsOnMissingModel) {
  EXPECT_THROW(
      {
        w14::InferenceEngine engine(
            "/tmp/__w14_definitely_does_not_exist.onnx");
      },
      std::runtime_error);
}

TEST(InferenceEngineEnvTest, EnvIsSingleton) {
  // 函数内 static 保证同一进程内多次调用拿到同一对象的引用。
  // 用地址比较验证；只要相等就证明 GlobalEnv 确实是单例。
  Ort::Env& a = w14::InferenceEngine::GlobalEnv();
  Ort::Env& b = w14::InferenceEngine::GlobalEnv();
  EXPECT_EQ(&a, &b);
}

// ---- W14.5：CUDA EP 接入 ----

// 默认构造行为不变：不传 ep 即 CPU EP、无回退原因。护住 W15 / 旧码零改动。
TEST_F(InferenceEngineModelTest, DefaultConstructionUsesCpu) {
  w14::InferenceEngine engine(kModelPath);
  EXPECT_EQ(engine.ActiveEp(), w14::Ep::kCpu);
  EXPECT_TRUE(engine.EpFallbackReason().empty());
}

// 请求 CUDA 永不抛异常：要么生效（ActiveEp==kCuda 且无原因），
// 要么回退 CPU（ActiveEp==kCpu 且有原因）。两环境下都成立。
// CPU 包运行时此处会触发真实回退，验证 fallback 路径。
TEST_F(InferenceEngineModelTest, CudaRequestFallsBackGracefully) {
  std::unique_ptr<w14::InferenceEngine> engine;
  ASSERT_NO_THROW(engine = std::make_unique<w14::InferenceEngine>(
                      kModelPath, w14::Ep::kCuda));
  if (engine->ActiveEp() == w14::Ep::kCpu) {
    EXPECT_FALSE(engine->EpFallbackReason().empty());
  } else {
    EXPECT_EQ(engine->ActiveEp(), w14::Ep::kCuda);
    EXPECT_TRUE(engine->EpFallbackReason().empty());
  }
}

// GPU 真跑：CUDA 可用时断言 EP 生效并跑通一次推理；不可用则优雅跳过。
TEST_F(InferenceEngineModelTest, LoadsModelWithCudaEp) {
  w14::InferenceEngine engine(kModelPath, w14::Ep::kCuda);
  if (engine.ActiveEp() != w14::Ep::kCuda) {
    GTEST_SKIP() << "CUDA EP 不可用，已回退 CPU：" << engine.EpFallbackReason();
  }
  std::vector<float> input(1 * 3 * 224 * 224, 0.5f);
  std::vector<int64_t> shape = {1, 3, 224, 224};
  auto outputs = engine.Run(input, shape);
  ASSERT_EQ(outputs.size(), 1u);
  auto out_info = outputs[0].GetTensorTypeAndShapeInfo();
  ASSERT_EQ(out_info.GetElementCount(), 1000u);

  // CUDA 路径同样要 sanity：元素全为有限数，否则 GPU 算子产出垃圾也会"通过"。
  const float* data = outputs[0].GetTensorData<float>();
  for (size_t i = 0; i < out_info.GetElementCount(); ++i) {
    ASSERT_TRUE(std::isfinite(data[i])) << "logit[" << i << "] 非有限";
  }
}
