// SPDX-License-Identifier: MIT
//
// 文件功能：trt 运行时引擎 —— engine 反序列化 + IExecutionContext +
//           GPU 缓冲管理，同步单帧推理。

#ifndef EDGE_AI_GENESIS_2026_TENSORRT_TRT_ENGINE_HPP_
#define EDGE_AI_GENESIS_2026_TENSORRT_TRT_ENGINE_HPP_

#include "trt_common.hpp"

#include <NvInfer.h>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace trt {

// TRT engine 运行时包装：固定形状单输入单输出（YAGNI：不做 dynamic shape）。
// 成员声明顺序即析构逆序契约：context_ 必须先于 engine_ 先于 runtime_ 析构，
// 所以按 runtime_ → engine_ → context_ 顺序声明。
class TrtEngine {
 public:
  explicit TrtEngine(const std::string& engine_path);
  ~TrtEngine();
  TrtEngine(const TrtEngine&) = delete;
  TrtEngine& operator=(const TrtEngine&) = delete;

  // 同步推理：H2D → enqueueV3 → D2H → stream 同步，返回展平输出。
  // input.size() != InputCount() 时抛 std::invalid_argument。
  [[nodiscard]] std::vector<float> Infer(std::span<const float> input);

  [[nodiscard]] std::size_t InputCount() const { return input_count_; }
  [[nodiscard]] std::size_t OutputCount() const { return output_count_; }
  [[nodiscard]] const std::vector<std::int64_t>& OutputShape() const {
    return output_shape_;
  }

 private:
  std::unique_ptr<nvinfer1::IRuntime> runtime_;
  std::unique_ptr<nvinfer1::ICudaEngine> engine_;
  std::unique_ptr<nvinfer1::IExecutionContext> context_;
  std::string input_name_;
  std::string output_name_;
  std::size_t input_count_ = 0;
  std::size_t output_count_ = 0;
  std::vector<std::int64_t> output_shape_;
  DeviceBuffer input_buffer_;
  DeviceBuffer output_buffer_;
  cudaStream_t stream_ = nullptr;
};

}  // namespace trt

#endif  // EDGE_AI_GENESIS_2026_TENSORRT_TRT_ENGINE_HPP_
