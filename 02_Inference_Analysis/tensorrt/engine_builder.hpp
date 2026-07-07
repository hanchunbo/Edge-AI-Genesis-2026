// SPDX-License-Identifier: MIT
//
// 文件功能：trt engine 构建器 —— ONNX → TRT engine（FP16/FP32），带磁盘缓存。

#ifndef EDGE_AI_GENESIS_2026_TENSORRT_ENGINE_BUILDER_HPP_
#define EDGE_AI_GENESIS_2026_TENSORRT_ENGINE_BUILDER_HPP_

#include <cstddef>
#include <string>

namespace trt {

// kFp32 保留作 FP16 一致性排查对照与 M2 显式 INT8 的基线；kInt8 归 M2。
enum class Precision { kFp16, kFp32 };

struct BuildConfig {
  std::string onnx_path;
  std::string cache_dir;  // 不存在时自动创建
  Precision precision = Precision::kFp16;
  // spec 风险 2：WSL 内存有限，显式限制 builder 工作区（默认 1GB）。
  std::size_t workspace_bytes = 1ULL << 30;
};

// ONNX → TRT engine，返回 engine 文件路径。
//
// 缓存戳（spec §6）：模型内容哈希 + TRT 运行库版本 + 精度 + SM 架构全部编入
// 文件名——任一变化 → 文件名变 → 未命中自动重建。engine 不跨 TRT 版本 /
// 跨卡兼容，靠文件名戳而非 sidecar 元数据，省一次一致性维护。
// 失败抛 std::runtime_error（对齐 w16/quant 错误风格）。
[[nodiscard]] std::string BuildOrLoadEngine(const BuildConfig& config);

}  // namespace trt

#endif  // EDGE_AI_GENESIS_2026_TENSORRT_ENGINE_BUILDER_HPP_
