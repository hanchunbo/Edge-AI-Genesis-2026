// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：张量形状工具库的公开头文件 —— 随 STATIC 库以 PUBLIC
// 方式暴露给消费者
// ============================================================================

#pragma once

#include <cstddef>
#include <initializer_list>
#include <span>
#include <vector>

namespace w7 {

// [Modern C++20]: Concept 约束合法的张量标量类型（float/int/uint8_t
// 等数值类型） [Pain Point]: 旧版用 SFINAE/enable_if，错误信息难以阅读 [Modern
// C++20]: concept + requires 编译期约束，报错直接指向调用处
template <typename T>
concept TensorScalar = std::is_arithmetic_v<T>;

// 张量形状描述类，表示 AI 推理中 {N, C, H, W} 等多维度信息
// 例：ResNet 输入 → TensorShape{1, 3, 224, 224}
class TensorShape {
 public:
  TensorShape() = default;

  // 支持花括号初始化：TensorShape{1, 3, 224, 224}
  explicit TensorShape(std::initializer_list<std::size_t> dims);

  // 支持从外部 span 构造，避免拷贝调用方的 vector
  explicit TensorShape(std::span<const std::size_t> dims);

  // 各维度之积 = 总元素数（如 {1,3,224,224} → 150528）
  [[nodiscard]] std::size_t NumElements() const noexcept;

  // 维度数（rank），如 {1,3,224,224} 的 rank = 4
  [[nodiscard]] std::size_t Rank() const noexcept;

  // 访问单个维度，越界时抛 std::out_of_range
  [[nodiscard]] std::size_t operator[](std::size_t i) const;

  // 返回维度的零拷贝视图，避免返回 vector 触发拷贝
  [[nodiscard]] std::span<const std::size_t> Dims() const noexcept;

  [[nodiscard]] bool operator==(const TensorShape& other) const noexcept;

 private:
  std::vector<std::size_t> dims_;
};

// 工具函数：判断两个形状是否可广播（AI 推理中常见的维度兼容性检查）
// 规则与 NumPy 广播规则一致：从尾部维度对齐，逐维度判断是否为 1 或相等
[[nodiscard]] bool IsBroadcastable(const TensorShape& a,
                                   const TensorShape& b) noexcept;

}  // namespace w7
