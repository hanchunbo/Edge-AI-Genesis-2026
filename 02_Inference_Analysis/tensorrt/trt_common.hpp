// SPDX-License-Identifier: MIT
//
// 文件功能：trt 公共设施 —— ILogger 适配器、CUDA 错误检查宏、GPU 显存 RAII、
//           文件读取工具。

#ifndef EDGE_AI_GENESIS_2026_TENSORRT_TRT_COMMON_HPP_
#define EDGE_AI_GENESIS_2026_TENSORRT_TRT_COMMON_HPP_

#include <NvInfer.h>
#include <cstddef>
#include <cuda_runtime_api.h>
#include <format>
#include <stdexcept>
#include <string>
#include <vector>

namespace trt {

// TRT 日志适配器：kWARNING 及以上透传 stderr——显式告警不静默（承接 quant
// 硬化原则）。TRT 要求 logger 生命周期覆盖全部 TRT 对象，统一走
// GlobalLogger()。
class Logger final : public nvinfer1::ILogger {
 public:
  void log(Severity severity, const char* msg) noexcept override;
};

[[nodiscard]] Logger& GlobalLogger();

// GPU 显存 RAII：cudaMalloc/cudaFree 配对，move-only（显存句柄不可共享复制）。
class DeviceBuffer {
 public:
  DeviceBuffer() = default;
  explicit DeviceBuffer(std::size_t bytes);
  ~DeviceBuffer();
  DeviceBuffer(DeviceBuffer&& other) noexcept;
  DeviceBuffer& operator=(DeviceBuffer&& other) noexcept;
  DeviceBuffer(const DeviceBuffer&) = delete;
  DeviceBuffer& operator=(const DeviceBuffer&) = delete;

  [[nodiscard]] void* Get() const { return ptr_; }
  [[nodiscard]] std::size_t Bytes() const { return bytes_; }

 private:
  void* ptr_ = nullptr;
  std::size_t bytes_ = 0;
};

// 整读二进制文件（ONNX 模型 / engine 缓存），失败抛 std::runtime_error。
[[nodiscard]] std::vector<char> ReadFileBytes(const std::string& path);

}  // namespace trt

// CUDA 错误检查宏：每个 CUDA 调用后必查，失败抛异常并带调用点上下文
// （spec §6：显式告警不静默回退）。do-while(false) 保证宏在 if/else 中安全。
#define TRT_CUDA_CHECK(expr)                                                  \
  do {                                                                        \
    const cudaError_t trt_cuda_check_err_ = (expr);                           \
    if (trt_cuda_check_err_ != cudaSuccess) {                                 \
      throw std::runtime_error(std::format(                                   \
          "[trt] CUDA 调用失败: {} @ {}:{} -> {}", #expr, __FILE__, __LINE__, \
          cudaGetErrorString(trt_cuda_check_err_)));                          \
    }                                                                         \
  } while (false)

#endif  // EDGE_AI_GENESIS_2026_TENSORRT_TRT_COMMON_HPP_
