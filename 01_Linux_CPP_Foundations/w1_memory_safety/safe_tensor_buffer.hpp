// SPDX-License-Identifier: MIT
//
// 文件功能：SafeTensorBuffer - 基于 RAII 的内存/显存管理类 (C++20/23 版本)
//
// 【C++20/23 特性应用】
// - Concepts (编译期类型约束)
// - std::expected (零开销错误处理 - C++23)
// - std::span (边界安全视图)
// - std::format (类型安全格式化)
//
// ============================================================================

#ifndef SAFE_TENSOR_BUFFER_HPP_
#define SAFE_TENSOR_BUFFER_HPP_

#include <concepts>  // C++20 Concepts
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>  // C++23 std::expected
#include <format>    // C++20 std::format
#include <functional>
#include <iostream>
#include <memory>
#include <span>  // C++20 std::span
#include <stdexcept>
#include <string>
#include <string_view>

// ============================================================================
// 错误处理：TensorError 枚举
// ============================================================================
// [Legacy C++11/17]: 使用异常 throw/catch 或返回错误码 int
// [Pain Point]: 异常有栈展开开销；错误码容易被忽略；返回值被占用
// [Modern C++20/23]: std::expected<T, E> 强制编译期处理，零运行时开销
// ============================================================================
enum class TensorError {
  kZeroSize,        // 缓冲区大小为零
  kAllocationFail,  // 内存分配失败
  kNullPointer,     // 空指针操作
  kOutOfRange       // 越界访问
};

// 错误转字符串（constexpr 编译期求值）
constexpr std::string_view TensorErrorToString(TensorError error) {
  switch (error) {
    case TensorError::kZeroSize:
      return "Buffer size cannot be zero";
    case TensorError::kAllocationFail:
      return "Memory allocation failed";
    case TensorError::kNullPointer:
      return "Operation on null pointer";
    case TensorError::kOutOfRange:
      return "Index out of range";
  }
  return "Unknown error";
}

// ============================================================================
// SafeTensorBuffer 类 —— RAII 内存管理
// ============================================================================
// [Legacy C++11/17]: 手动 new/delete，容易忘记释放导致内存泄漏
// [Pain Point]: 异常时资源泄漏；多处 new 需对应 delete；难以追踪生命周期
// [Modern C++20/23]: RAII 保证资源自动释放；std::span 提供安全视图
// ============================================================================

/**
 * @brief 模拟显存/内存的安全管理类
 *
 * 核心特性：
 * - RAII 自动释放资源
 * - 禁用拷贝（避免深拷贝开销）
 * - 支持移动语义（零拷贝转移所有权）
 * - std::span 提供安全的数据视图
 */
class SafeTensorBuffer {
 public:
  // ==========================================================================
  // 构造函数 —— 保持原有 API 兼容
  // ==========================================================================
  // [Legacy C++11/17]: 构造失败抛异常或返回空指针
  // [Pain Point]: 异常处理代码冗长；nullptr 检查容易遗漏
  // [Modern C++20/23]: 提供 Create() 工厂返回 std::expected
  // ==========================================================================
  explicit SafeTensorBuffer(size_t size) : size_(size), data_(nullptr) {
    if (size == 0) {
      throw std::invalid_argument("Buffer size cannot be zero");
    }

    std::cout << std::format("[SafeTensorBuffer] 构造: 分配 {} 字节\n", size);

    data_ = new (std::nothrow) uint8_t[size];
    if (data_ == nullptr) {
      throw std::bad_alloc();
    }

    std::memset(data_, 0, size);
    std::cout << std::format("[SafeTensorBuffer] 地址 = {}\n",
                             static_cast<void*>(data_));
  }

  // ==========================================================================
  // 工厂函数 —— C++23 std::expected 版本（推荐）
  // ==========================================================================
  // [Legacy C++11/17]: 无法同时返回值和错误信息
  // [Pain Point]: 要么用异常（开销），要么用 pair<T, error>（笨拙）
  // [Modern C++20/23]: std::expected<T, E> 类型安全，编译期强制处理
  // ==========================================================================
  /**
   * @brief 创建 SafeTensorBuffer 的安全工厂函数
   * @param size 字节大小
   * @return std::expected<SafeTensorBuffer, TensorError>
   */
  static auto Create(size_t size)
      -> std::expected<SafeTensorBuffer, TensorError> {
    if (size == 0) {
      return std::unexpected(TensorError::kZeroSize);
    }

    uint8_t* data = new (std::nothrow) uint8_t[size];
    if (data == nullptr) {
      return std::unexpected(TensorError::kAllocationFail);
    }

    std::memset(data, 0, size);

    SafeTensorBuffer buffer;
    buffer.size_ = size;
    buffer.data_ = data;
    std::cout << std::format(
        "[SafeTensorBuffer] Create: 分配 {} 字节, 地址 = {}\n", size,
        static_cast<void*>(data));
    return buffer;
  }

  // ==========================================================================
  // 析构函数
  // ==========================================================================
  ~SafeTensorBuffer() {
    if (data_ != nullptr) {
      std::cout << std::format("[SafeTensorBuffer] 析构: 释放 {} 字节\n",
                               size_);
      delete[] data_;
      data_ = nullptr;
    } else {
      std::cout << std::format("[SafeTensorBuffer] 析构: 对象已被移动\n");
    }
  }

  // ==========================================================================
  // 禁用拷贝（Rule of Five）
  // ==========================================================================
  // [Legacy C++11/17]: 默认拷贝导致浅拷贝，双重释放
  // [Pain Point]: 大缓冲区拷贝有性能开销（~24MB 图像数据）
  // [Modern C++20/23]: 显式 delete 拷贝，强制使用移动语义
  // ==========================================================================
  SafeTensorBuffer(const SafeTensorBuffer&) = delete;
  SafeTensorBuffer& operator=(const SafeTensorBuffer&) = delete;

  // ==========================================================================
  // 移动语义
  // ==========================================================================
  // [Legacy C++11/17]: C++11 引入移动语义，但需手动实现
  // [Pain Point]: 忘记 noexcept 导致 vector 拒绝使用移动优化
  // [Modern C++20/23]: noexcept 保证 vector 扩容使用移动而非拷贝
  // ==========================================================================
  SafeTensorBuffer(SafeTensorBuffer&& other) noexcept
      : size_(other.size_), data_(other.data_) {
    std::cout << std::format("[SafeTensorBuffer] 移动构造: 从 {} 转移\n",
                             static_cast<void*>(other.data_));
    other.size_ = 0;
    other.data_ = nullptr;
  }

  SafeTensorBuffer& operator=(SafeTensorBuffer&& other) noexcept {
    if (this != &other) {
      delete[] data_;
      size_ = other.size_;
      data_ = other.data_;
      other.size_ = 0;
      other.data_ = nullptr;
    }
    return *this;
  }

  // ==========================================================================
  // 数据访问接口 —— 原有 API（小写保持兼容）
  // ==========================================================================
  const uint8_t* data() const noexcept { return data_; }
  uint8_t* data() noexcept { return data_; }
  size_t size() const noexcept { return size_; }
  bool valid() const noexcept { return data_ != nullptr; }

  void fill(uint8_t value) {
    if (data_ != nullptr) {
      std::memset(data_, value, size_);
    }
  }

  // ==========================================================================
  // C++20 增强接口 —— std::span 视图
  // ==========================================================================
  // [Legacy C++11/17]: 传递指针 + 长度 (T* ptr, size_t len)
  // [Pain Point]: 无边界检查；调用方不知道长度；类型不安全
  // [Modern C++20/23]: std::span<T> 携带长度信息，支持边界检查
  // ==========================================================================

  /**
   * @brief 获取只读数据视图 (C++20 std::span)
   * @return std::span<const uint8_t> 只读视图
   */
  std::span<const uint8_t> View() const noexcept {
    return std::span<const uint8_t>(data_, size_);
  }

  /**
   * @brief 获取可写数据视图
   * @return std::span<uint8_t> 可写视图
   */
  std::span<uint8_t> MutableView() noexcept {
    return std::span<uint8_t>(data_, size_);
  }

  /**
   * @brief 安全的元素访问（带边界检查，返回 expected）
   * @param index 索引
   * @return std::expected<std::reference_wrapper<uint8_t>, TensorError>
   */
  auto At(size_t index)
      -> std::expected<std::reference_wrapper<uint8_t>, TensorError> {
    if (data_ == nullptr) {
      return std::unexpected(TensorError::kNullPointer);
    }
    if (index >= size_) {
      return std::unexpected(TensorError::kOutOfRange);
    }
    return std::ref(data_[index]);
  }

 private:
  // 私有默认构造（供 Create 工厂使用）
  SafeTensorBuffer() : size_(0), data_(nullptr) {}

  size_t size_;  // 成员变量后缀下划线 (Google Style)
  uint8_t* data_;
};

// ============================================================================
// 智能指针辅助 —— 保持原有 API
// ============================================================================
// [Legacy C++11/17]: 手动 new/delete 管理堆对象
// [Pain Point]: 忘记 delete 导致泄漏；异常时无法释放
// [Modern C++20/23]: shared_ptr/unique_ptr 自动管理生命周期
// ============================================================================

using TensorBufferPtr = std::shared_ptr<SafeTensorBuffer>;
using TensorBufferWeakPtr = std::weak_ptr<SafeTensorBuffer>;

inline TensorBufferPtr MakeTensorBuffer(size_t size) {
  return std::make_shared<SafeTensorBuffer>(size);
}

// 保持原有 API 兼容（小写函数名）
inline TensorBufferPtr make_tensor_buffer(size_t size) {
  return MakeTensorBuffer(size);
}

// 自定义删除器示例
struct TensorBufferDeleter {
  void operator()(SafeTensorBuffer* ptr) const {
    std::cout << std::format("[TensorBufferDeleter] 自定义删除器\n");
    delete ptr;
  }
};

using UniqueTensorBuffer =
    std::unique_ptr<SafeTensorBuffer, TensorBufferDeleter>;

// ============================================================================
// C++20 Concepts 验证
// ============================================================================
// [Legacy C++11/17]: SFINAE + enable_if 约束模板
// [Pain Point]: 错误信息难以理解；代码复杂难维护
// [Modern C++20/23]: Concepts 清晰表达约束，编译错误可读
// ============================================================================

// 验证 SafeTensorBuffer 满足 movable 约束
static_assert(std::movable<SafeTensorBuffer>,
              "SafeTensorBuffer must satisfy std::movable");

// 验证不可拷贝（设计如此）
static_assert(!std::copyable<SafeTensorBuffer>,
              "SafeTensorBuffer must NOT be copyable");

#endif  // SAFE_TENSOR_BUFFER_HPP_
