// SPDX-License-Identifier: MIT
//
// 文件功能：CustomImage - 模拟 4K 图像类 (C++20 版本)
//
// 本文件演示移动语义与视图技术的结合应用：
//
// 【C++20 特性应用】
// ┌─────────────────────────────────────────────────────────────────────────┐
// │ 特性                    │ 用途                    │ 优势              │
// ├─────────────────────────┼────────────────────────┼──────────────────┤
// │ std::span               │ 零拷贝图像数据视图      │ 边界安全、通用   │
// │ std::format             │ 日志输出                │ 类型安全格式化   │
// │ Concepts                │ 编译期类型约束          │ 清晰的错误信息   │
// └─────────────────────────────────────────────────────────────────────────┘
//
// 【零拷贝视图 (View) 与移动 (Move) 的差异与配合】
//
// ┌─────────────────────────────────────────────────────────────────────────┐
// │                          View (std::span)                              │
// │ - 不拥有数据，只提供访问接口                                           │
// │ - 原对象必须存活，span 才有效                                          │
// │ - 适合临时访问、只读操作                                               │
// │ - 多个 span 可同时指向同一数据                                         │
// ├─────────────────────────────────────────────────────────────────────────┤
// │                          Move (移动语义)                               │
// │ - 转移所有权，源对象变为空壳                                           │
// │ - 目标对象独占数据                                                     │
// │ - 适合跨作用域传递、容器存储                                           │
// │ - 源对象不可再使用                                                     │
// └─────────────────────────────────────────────────────────────────────────┘
//
// 【配合使用场景】
// 1. 函数内部处理：使用 span 视图访问数据（不转移所有权）
// 2. 存入容器/跨函数：使用 move 转移所有权（避免拷贝）
// 3. 只读共享：多个处理器共用同一 span 视图
//
// ============================================================================

#ifndef CUSTOM_IMAGE_HPP_
#define CUSTOM_IMAGE_HPP_

#include <algorithm>
#include <concepts>  // C++20 Concepts
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>  // C++20 std::format
#include <iostream>
#include <span>  // C++20 std::span
#include <type_traits>
#include <utility>

// ============================================================================
// 知识点：std::span —— 零拷贝视图
// ============================================================================
//
// std::span 是 C++20 引入的轻量级视图类型，类似于 string_view 对于字符串。
//
// 【核心特性】
// - 不拥有数据：只有指针和大小，不管理生命周期
// - 零开销：没有动态内存分配，sizeof(span) == 2 * sizeof(void*)
// - 边界安全：知道自己的大小，可以进行范围检查
// - 通用性：可以接受 vector、array、原始数组等多种来源
//
// 【与原始指针对比】
// ┌────────────────────────┬────────────────────────┐
// │ 原始指针 (T*)          │ std::span<T>           │
// ├────────────────────────┼────────────────────────┤
// │ 不知道长度             │ 携带长度信息           │
// │ 无边界检查             │ 支持 at() 边界检查     │
// │ 不支持范围 for         │ 支持范围 for           │
// │ 类型不安全             │ 类型安全               │
// └────────────────────────┴────────────────────────┘
//
// 【注意事项】
// - span 不延长底层数据的生命周期
// - 必须确保底层数据存活期间 span 才有效
// ============================================================================

// ============================================================================
// CustomImage 类定义 (C++20)
// ============================================================================
/**
 * @brief 模拟 4K 图像的容器类
 *
 * 设计目的：
 * 1. 演示深拷贝的高昂代价（~24MB 数据复制）
 * 2. 演示移动语义的零拷贝优势（仅指针转移）
 * 3. 演示 std::span 视图访问（不转移所有权的零拷贝）
 * 4. 提供完整的 Rule of Five 实现范例
 */
class CustomImage {
 public:
  // ==========================================================================
  // 4K 图像常量
  // ==========================================================================
  static constexpr size_t kWidth = 3840;
  static constexpr size_t kHeight = 2160;
  static constexpr size_t kChannels = 3;  // BGR
  static constexpr size_t kImageSize = kWidth * kHeight * kChannels;

  // ==========================================================================
  // 构造函数
  // ==========================================================================

  /// 默认构造：分配 4K 图像缓冲区
  CustomImage() : data_(new uint8_t[kImageSize]), size_(kImageSize) {
    std::memset(data_, 0, size_);
    ++construction_count_;
  }

  /// 带填充值的构造函数
  explicit CustomImage(uint8_t fill_value)
      : data_(new uint8_t[kImageSize]), size_(kImageSize) {
    std::memset(data_, fill_value, size_);
    ++construction_count_;
  }

  // ==========================================================================
  // 析构函数
  // ==========================================================================
  ~CustomImage() {
    if (data_ != nullptr) {
      delete[] data_;
      data_ = nullptr;
      ++destruction_count_;
    }
  }

  // ==========================================================================
  // 拷贝语义（深拷贝 - 代价高昂）
  // ==========================================================================

  /// 拷贝构造函数 - 执行深拷贝
  /// @note 性能瓶颈：需要复制 ~24MB 数据
  CustomImage(const CustomImage& other)
      : data_(new uint8_t[other.size_]), size_(other.size_) {
    std::memcpy(data_, other.data_, size_);
    ++copy_count_;
  }

  /// 拷贝赋值运算符 - 执行深拷贝
  CustomImage& operator=(const CustomImage& other) {
    if (this != &other) {
      delete[] data_;
      size_ = other.size_;
      data_ = new uint8_t[size_];
      std::memcpy(data_, other.data_, size_);
      ++copy_count_;
    }
    return *this;
  }

  // ==========================================================================
  // 移动语义（零拷贝 - 高效）
  // ==========================================================================

  /**
   * @brief 移动构造函数 - 零拷贝转移所有权
   *
   * 【noexcept 的重要性】
   * 标记为 noexcept 后，std::vector 在扩容时会优先使用移动构造
   * 而非保守的拷贝构造。这对容器性能至关重要。
   */
  CustomImage(CustomImage&& other) noexcept
      : data_(other.data_), size_(other.size_) {
    other.data_ = nullptr;
    other.size_ = 0;
    ++move_count_;
  }

  /// 移动赋值运算符 - 零拷贝转移所有权
  CustomImage& operator=(CustomImage&& other) noexcept {
    if (this != &other) {
      delete[] data_;
      data_ = other.data_;
      size_ = other.size_;
      other.data_ = nullptr;
      other.size_ = 0;
      ++move_count_;
    }
    return *this;
  }

  // ==========================================================================
  // 数据访问接口 —— 传统方式 vs C++20 span
  // ==========================================================================

  // 传统指针访问（保持兼容性）
  const uint8_t* Data() const noexcept { return data_; }
  uint8_t* Data() noexcept { return data_; }
  size_t Size() const noexcept { return size_; }
  bool Valid() const noexcept { return data_ != nullptr; }

  // [Legacy C++11/17]: 使用原始指针 + size_t 参数传递数据 (T* data, size_t len)
  // [Pain Point]: 无边界检查、不知道长度、无法使用范围 for、类型不安全
  // [Modern C++20/23]: std::span
  // 提供类型安全的零拷贝视图，支持边界检查和范围遍历
  /**
   * @brief 获取只读数据视图 (C++20 std::span)
   *
   * 【使用场景】
   * - 传递给处理函数时，不转移所有权
   * - 多个处理器可同时持有视图
   * - 性能等同于原始指针
   *
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
   * @brief 获取指定行的视图
   *
   * 【为什么要提供行视图？】
   * 图像处理常常按行操作（如逐行扫描）。
   * 提供行视图可以：
   * - 简化像素访问代码
   * - 支持并行按行处理
   *
   * @param row 行索引 (0 到 kHeight-1)
   * @return std::span<const uint8_t> 该行的只读视图（kWidth * kChannels 字节）
   */
  std::span<const uint8_t> RowView(size_t row) const noexcept {
    if (row >= kHeight || data_ == nullptr) {
      return {};  // 返回空 span
    }
    size_t offset = row * kWidth * kChannels;
    return std::span<const uint8_t>(data_ + offset, kWidth * kChannels);
  }

  // 图像尺寸信息
  static constexpr size_t Width() { return kWidth; }
  static constexpr size_t Height() { return kHeight; }
  static constexpr size_t Channels() { return kChannels; }

  // ==========================================================================
  // 统计接口（用于 Benchmark）
  // ==========================================================================

  static size_t GetCopyCount() { return copy_count_; }
  static size_t GetMoveCount() { return move_count_; }
  static size_t GetConstructionCount() { return construction_count_; }
  static size_t GetDestructionCount() { return destruction_count_; }

  static void ResetCounters() {
    copy_count_ = 0;
    move_count_ = 0;
    construction_count_ = 0;
    destruction_count_ = 0;
  }

  // [Legacy C++11/17]: 使用 std::cout << 拼接或 printf 格式化输出
  // [Pain Point]: 类型不安全、格式串与参数不匹配时运行时错误、代码可读性差
  // [Modern C++20/23]: std::format 提供编译期类型检查、类型安全的格式化字符串
  static void PrintStats() {
    std::cout << std::format("=== CustomImage 统计 ===\n");
    std::cout << std::format("  构造次数: {}\n", construction_count_);
    std::cout << std::format("  析构次数: {}\n", destruction_count_);
    std::cout << std::format("  拷贝次数: {} (每次约 {:.1f} MB)\n", copy_count_,
                             static_cast<double>(kImageSize) / 1024.0 / 1024.0);
    std::cout << std::format("  移动次数: {} (零拷贝)\n", move_count_);
  }

 private:
  uint8_t* data_;
  size_t size_;

  // 静态计数器（用于统计）
  static inline size_t copy_count_ = 0;
  static inline size_t move_count_ = 0;
  static inline size_t construction_count_ = 0;
  static inline size_t destruction_count_ = 0;
};

// ============================================================================
// 编译期类型特性验证 (C++20 Concepts 风格)
// ============================================================================

// [Legacy C++11/17]: 使用 std::enable_if + SFINAE 实现模板约束
// [Pain Point]: 错误信息晦涩难懂、代码复杂难以维护
// [Modern C++20/23]: std::movable 等 Concept
// 提供清晰的编译期类型约束和友好错误信息

// 使用 C++20 Concepts 验证 CustomImage 满足 movable 要求
static_assert(std::movable<CustomImage>,
              "CustomImage must satisfy std::movable concept");

// 传统 static_assert 验证（更详细）
static_assert(std::is_nothrow_move_constructible_v<CustomImage>,
              "CustomImage move constructor must be noexcept");

static_assert(std::is_nothrow_move_assignable_v<CustomImage>,
              "CustomImage move assignment must be noexcept");

static_assert(std::is_copy_constructible_v<CustomImage>,
              "CustomImage must be copy constructible for benchmark");

// ============================================================================
// 使用 span 的处理函数示例
// ============================================================================

/**
 * @brief 计算图像平均像素值（使用 span 视图）
 *
 * 【View 的优势】
 * - 函数不拥有数据，调用者保持所有权
 * - 可以传入任何连续内存（vector、array、CustomImage）
 * - 性能与原始指针相当
 *
 * @param image_data 图像数据视图
 * @return 平均像素值
 */
inline double CalculateAveragePixel(std::span<const uint8_t> image_data) {
  if (image_data.empty()) {
    return 0.0;
  }

  uint64_t sum = 0;
  for (uint8_t pixel : image_data) {  // span 支持范围 for
    sum += pixel;
  }
  return static_cast<double>(sum) / static_cast<double>(image_data.size());
}

/**
 * @brief 就地修改图像亮度（使用 span 视图）
 *
 * @param image_data 可写图像数据视图
 * @param delta 亮度调整值
 */
inline void AdjustBrightness(std::span<uint8_t> image_data, int delta) {
  for (uint8_t& pixel : image_data) {
    int new_val = static_cast<int>(pixel) + delta;
    pixel = static_cast<uint8_t>(std::clamp(new_val, 0, 255));
  }
}

#endif  // CUSTOM_IMAGE_HPP_
