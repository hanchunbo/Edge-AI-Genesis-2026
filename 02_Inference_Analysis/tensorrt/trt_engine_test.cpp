// SPDX-License-Identifier: MIT
//
// 文件功能：trt engine 测试 —— FP16 构建 + 缓存命中 +（Task 6 扩展）单帧
// smoke。

#include "engine_builder.hpp"

#include <cuda_runtime_api.h>
#include <filesystem>
#include <gtest/gtest.h>
#include <string>

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

}  // namespace
