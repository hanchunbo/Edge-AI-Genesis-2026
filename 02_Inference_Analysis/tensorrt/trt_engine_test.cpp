// SPDX-License-Identifier: MIT
//
// 文件功能：trt engine 测试 —— FP16 构建 + 缓存命中 +（Task 6 扩展）单帧
// smoke。

#include "trt_engine.hpp"

#include "engine_builder.hpp"

#include <algorithm>
#include <cmath>
#include <cuda_runtime_api.h>
#include <filesystem>
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace {

bool CudaAvailable() {
  int count = 0;
  return cudaGetDeviceCount(&count) == cudaSuccess && count > 0;
}

void SkipIfEnvMissing() {
  if (!CudaAvailable()) {
    GTEST_SKIP() << "无 CUDA 设备（CI/VPS），跳过";
  }
  if (!std::filesystem::exists(TRT_MODEL_PATH)) {
    GTEST_SKIP() << "缺少 yolov8n.onnx（W16 资产）";
  }
}

// 首次运行会真实构建 FP16 engine，3060 上约 1~4 分钟；之后命中缓存秒级。
TEST(TrtEngineBuilder, BuildsAndCachesFp16Engine) {
  SkipIfEnvMissing();

  const trt::BuildConfig config{
      .onnx_path = TRT_MODEL_PATH,
      .cache_dir = TRT_ENGINE_CACHE_DIR,
      .precision = trt::Precision::kFp16,
  };

  const std::string first = trt::BuildOrLoadEngine(config);
  ASSERT_TRUE(std::filesystem::exists(first));
  EXPECT_GT(std::filesystem::file_size(first), 1024u * 1024u);  // 至少 MB 级
  const auto mtime = std::filesystem::last_write_time(first);

  // 第二次调用：路径一致 + 未重建（mtime 不变）即缓存命中。
  const std::string second = trt::BuildOrLoadEngine(config);
  EXPECT_EQ(first, second);
  EXPECT_EQ(std::filesystem::last_write_time(second), mtime);
}

TEST(TrtEngine, SingleFrameSmokeInference) {
  SkipIfEnvMissing();

  const std::string engine_path = trt::BuildOrLoadEngine(trt::BuildConfig{
      .onnx_path = TRT_MODEL_PATH,
      .cache_dir = TRT_ENGINE_CACHE_DIR,
      .precision = trt::Precision::kFp16,
  });
  trt::TrtEngine engine(engine_path);

  // yolov8n 固定形状：输入 1×3×640×640，输出 [1,84,8400]。
  EXPECT_EQ(engine.InputCount(), 1u * 3u * 640u * 640u);
  ASSERT_EQ(engine.OutputShape().size(), 3u);
  EXPECT_EQ(engine.OutputShape()[1], 84);
  EXPECT_EQ(engine.OutputShape()[2], 8400);

  const std::vector<float> input(engine.InputCount(), 0.5f);
  const std::vector<float> output = engine.Infer(input);

  ASSERT_EQ(output.size(), engine.OutputCount());
  EXPECT_TRUE(std::all_of(output.begin(), output.end(),
                          [](float v) { return std::isfinite(v); }));
  // 前 4 通道是像素坐标（cx,cy,w,h），合理输出必有大于 1 的值。
  EXPECT_GT(*std::max_element(output.begin(), output.end()), 1.0f);
}

}  // namespace
