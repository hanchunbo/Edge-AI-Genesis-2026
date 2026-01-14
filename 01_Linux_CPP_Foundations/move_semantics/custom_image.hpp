/**
 * @file custom_image.hpp
 * @brief CustomImage - 模拟 4K 图像的类，用于演示移动语义
 * @note 详细知识点说明见 notes.md
 *
 * 4K 图像尺寸: 3840 x 2160 x 3 (BGR) = 24,883,200 字节 ≈ 24MB
 */

#ifndef CUSTOM_IMAGE_HPP_
#define CUSTOM_IMAGE_HPP_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <type_traits>
#include <utility>

// =============================================================================
//                          编译期类型特性验证
// =============================================================================

// 前向声明用于 static_assert
class CustomImage;

// =============================================================================
//                          CustomImage 类定义
// =============================================================================

/**
 * @class CustomImage
 * @brief 模拟 4K 图像的容器类
 *
 * 设计目的：
 * 1. 演示深拷贝的高昂代价（~24MB 数据复制）
 * 2. 演示移动语义的零拷贝优势（仅指针转移）
 * 3. 提供完整的 Rule of Five 实现范例
 */
class CustomImage {
 public:
  // 4K 图像常量
  static constexpr size_t kWidth = 3840;
  static constexpr size_t kHeight = 2160;
  static constexpr size_t kChannels = 3;  // BGR
  static constexpr size_t kImageSize = kWidth * kHeight * kChannels;

  // ---------------------------------------------------------------------------
  // 构造函数
  // ---------------------------------------------------------------------------

  /// 默认构造：分配 4K 图像缓冲区
  CustomImage() : data_(new uint8_t[kImageSize]), size_(kImageSize) {
    // 初始化为黑色图像
    std::memset(data_, 0, size_);
    ++construction_count_;
  }

  /// 带填充值的构造函数
  explicit CustomImage(uint8_t fill_value)
      : data_(new uint8_t[kImageSize]), size_(kImageSize) {
    std::memset(data_, fill_value, size_);
    ++construction_count_;
  }

  // ---------------------------------------------------------------------------
  // 析构函数
  // ---------------------------------------------------------------------------

  ~CustomImage() {
    if (data_ != nullptr) {
      delete[] data_;
      data_ = nullptr;
      ++destruction_count_;
    }
  }

  // ---------------------------------------------------------------------------
  // 拷贝语义（深拷贝 - 代价高昂）
  // ---------------------------------------------------------------------------

  /// 拷贝构造函数 - 执行深拷贝
  /// @note 这是性能瓶颈所在，需要复制 ~24MB 数据
  CustomImage(const CustomImage& other)
      : data_(new uint8_t[other.size_]), size_(other.size_) {
    std::memcpy(data_, other.data_, size_);
    ++copy_count_;
  }

  /// 拷贝赋值运算符 - 执行深拷贝
  CustomImage& operator=(const CustomImage& other) {
    if (this != &other) {
      // 先释放旧资源
      delete[] data_;
      // 分配新资源并复制
      size_ = other.size_;
      data_ = new uint8_t[size_];
      std::memcpy(data_, other.data_, size_);
      ++copy_count_;
    }
    return *this;
  }

  // ---------------------------------------------------------------------------
  // 移动语义（零拷贝 - 高效）
  // ---------------------------------------------------------------------------

  /// 移动构造函数 - 零拷贝转移所有权
  /// @note 使用 noexcept 保证强异常安全，允许 vector 优化
  CustomImage(CustomImage&& other) noexcept
      : data_(other.data_), size_(other.size_) {
    // 将源对象置于有效但未定义的状态
    other.data_ = nullptr;
    other.size_ = 0;
    ++move_count_;
  }

  /// 移动赋值运算符 - 零拷贝转移所有权
  CustomImage& operator=(CustomImage&& other) noexcept {
    if (this != &other) {
      // 释放当前资源
      delete[] data_;
      // 转移所有权
      data_ = other.data_;
      size_ = other.size_;
      // 置空源对象
      other.data_ = nullptr;
      other.size_ = 0;
      ++move_count_;
    }
    return *this;
  }

  // ---------------------------------------------------------------------------
  // 访问接口
  // ---------------------------------------------------------------------------

  const uint8_t* Data() const noexcept { return data_; }
  uint8_t* Data() noexcept { return data_; }
  size_t Size() const noexcept { return size_; }
  bool Valid() const noexcept { return data_ != nullptr; }

  // 图像尺寸信息
  static constexpr size_t Width() { return kWidth; }
  static constexpr size_t Height() { return kHeight; }
  static constexpr size_t Channels() { return kChannels; }

  // ---------------------------------------------------------------------------
  // 统计接口（用于 Benchmark）
  // ---------------------------------------------------------------------------

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

  static void PrintStats() {
    std::cout << "=== CustomImage 统计 ===" << std::endl;
    std::cout << "  构造次数: " << construction_count_ << std::endl;
    std::cout << "  析构次数: " << destruction_count_ << std::endl;
    std::cout << "  拷贝次数: " << copy_count_ << std::endl;
    std::cout << "  移动次数: " << move_count_ << std::endl;
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

// =============================================================================
//                          编译期类型特性验证
// =============================================================================

// 使用 static_assert 在编译期验证类型特性
// 这些断言如果失败，会在编译时报错，而不是运行时
static_assert(std::is_move_constructible_v<CustomImage>,
              "CustomImage must be move constructible");

static_assert(std::is_move_assignable_v<CustomImage>,
              "CustomImage must be move assignable");

static_assert(std::is_nothrow_move_constructible_v<CustomImage>,
              "CustomImage move constructor must be noexcept");

static_assert(std::is_nothrow_move_assignable_v<CustomImage>,
              "CustomImage move assignment must be noexcept");

// 验证拷贝语义也存在（用于对比测试）
static_assert(std::is_copy_constructible_v<CustomImage>,
              "CustomImage must be copy constructible for benchmark");

static_assert(std::is_copy_assignable_v<CustomImage>,
              "CustomImage must be copy assignable for benchmark");

#endif  // CUSTOM_IMAGE_HPP_
