// SPDX-License-Identifier: MIT
//
// 文件功能：trt 公共设施测试 —— CUDA 检查宏抛错行为与 DeviceBuffer RAII/move。

#include "trt_common.hpp"

#include <cuda_runtime_api.h>
#include <gtest/gtest.h>
#include <stdexcept>
#include <utility>

namespace {

bool CudaAvailable() {
  int count = 0;
  return cudaGetDeviceCount(&count) == cudaSuccess && count > 0;
}

TEST(TrtCommon, CudaCheckThrowsOnBadCall) {
  if (!CudaAvailable()) {
    GTEST_SKIP() << "无 CUDA 设备（CI/VPS），跳过";
  }
  EXPECT_THROW(TRT_CUDA_CHECK(cudaSetDevice(9999)), std::runtime_error);
  // 清掉上一条错误，避免污染后续用例（invalid device 非 sticky error）。
  cudaGetLastError();
}

TEST(TrtCommon, DeviceBufferAllocatesAndMoves) {
  if (!CudaAvailable()) {
    GTEST_SKIP() << "无 CUDA 设备（CI/VPS），跳过";
  }
  trt::DeviceBuffer a(1024);
  EXPECT_NE(a.Get(), nullptr);
  EXPECT_EQ(a.Bytes(), 1024u);

  trt::DeviceBuffer b(std::move(a));
  EXPECT_NE(b.Get(), nullptr);
  EXPECT_EQ(a.Get(), nullptr);  // 被移走后不再持有，防 double-free

  trt::DeviceBuffer c;
  c = std::move(b);
  EXPECT_NE(c.Get(), nullptr);
  EXPECT_EQ(b.Get(), nullptr);
}

}  // namespace
