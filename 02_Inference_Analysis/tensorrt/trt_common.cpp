// SPDX-License-Identifier: MIT
//
// 文件功能：trt 公共设施实现 —— 详见 trt_common.hpp。

#include "trt_common.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <utility>

namespace trt {

void Logger::log(Severity severity, const char* msg) noexcept {
  // Severity 枚举值越小越严重（kINTERNAL_ERROR=0 … kVERBOSE=4）。
  if (severity <= Severity::kWARNING) {
    const char* tag = severity == Severity::kWARNING ? "W" : "E";
    std::fprintf(stderr, "[trt][%s] %s\n", tag, msg);
  }
}

Logger& GlobalLogger() {
  static Logger logger;
  return logger;
}

DeviceBuffer::DeviceBuffer(std::size_t bytes) : bytes_(bytes) {
  TRT_CUDA_CHECK(cudaMalloc(&ptr_, bytes));
}

DeviceBuffer::~DeviceBuffer() {
  if (ptr_ != nullptr) {
    // 析构路径不抛异常，释放失败只能忽略（进程退出时 CUDA 上下文自会回收）。
    static_cast<void>(cudaFree(ptr_));
  }
}

DeviceBuffer::DeviceBuffer(DeviceBuffer&& other) noexcept
    : ptr_(std::exchange(other.ptr_, nullptr)),
      bytes_(std::exchange(other.bytes_, 0)) {}

DeviceBuffer& DeviceBuffer::operator=(DeviceBuffer&& other) noexcept {
  if (this != &other) {
    if (ptr_ != nullptr) {
      static_cast<void>(cudaFree(ptr_));
    }
    ptr_ = std::exchange(other.ptr_, nullptr);
    bytes_ = std::exchange(other.bytes_, 0);
  }
  return *this;
}

std::vector<char> ReadFileBytes(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("[trt] 无法读取文件: " + path);
  }
  return {std::istreambuf_iterator<char>(file),
          std::istreambuf_iterator<char>()};
}

}  // namespace trt
