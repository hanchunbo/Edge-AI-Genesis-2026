/**
 * @file safe_tensor_buffer.hpp
 * @brief SafeTensorBuffer - 基于 RAII 的内存/显存管理类
 * @note 详细知识点说明见 notes.md
 */

#ifndef SAFE_TENSOR_BUFFER_HPP
#define SAFE_TENSOR_BUFFER_HPP

#include <cstddef>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>

/**
 * @class SafeTensorBuffer
 * @brief 模拟显存/内存的安全管理类
 *
 * 特点：RAII 自动释放 | 禁用拷贝 | 支持移动 | 异常安全
 */
class SafeTensorBuffer {
public:
  /// 构造函数 - 分配内存
  explicit SafeTensorBuffer(size_t size) : size_(size), data_(nullptr) {
    if (size == 0) {
      throw std::invalid_argument("Buffer size cannot be zero");
    }

    std::cout << "[SafeTensorBuffer] 构造: 分配 " << size << " 字节"
              << std::endl;

    data_ = new (std::nothrow) uint8_t[size];
    if (data_ == nullptr) {
      throw std::bad_alloc();
    }

    std::memset(data_, 0, size);
    std::cout << "[SafeTensorBuffer] 地址 = " << static_cast<void *>(data_)
              << std::endl;
  }

  /// 析构函数 - 自动释放内存
  ~SafeTensorBuffer() {
    if (data_ != nullptr) {
      std::cout << "[SafeTensorBuffer] 析构: 释放 " << size_ << " 字节"
                << std::endl;
      delete[] data_;
      data_ = nullptr;
    } else {
      std::cout << "[SafeTensorBuffer] 析构: 对象已被移动" << std::endl;
    }
  }

  // 禁用拷贝（深拷贝代价高昂）
  SafeTensorBuffer(const SafeTensorBuffer &) = delete;
  SafeTensorBuffer &operator=(const SafeTensorBuffer &) = delete;

  /// 移动构造（零拷贝转移所有权）
  SafeTensorBuffer(SafeTensorBuffer &&other) noexcept
      : size_(other.size_), data_(other.data_) {
    std::cout << "[SafeTensorBuffer] 移动构造: 从 "
              << static_cast<void *>(other.data_) << " 转移" << std::endl;
    other.size_ = 0;
    other.data_ = nullptr;
  }

  /// 移动赋值
  SafeTensorBuffer &operator=(SafeTensorBuffer &&other) noexcept {
    if (this != &other) {
      delete[] data_;
      size_ = other.size_;
      data_ = other.data_;
      other.size_ = 0;
      other.data_ = nullptr;
    }
    return *this;
  }

  // 访问接口
  const uint8_t *data() const noexcept { return data_; }
  uint8_t *data() noexcept { return data_; }
  size_t size() const noexcept { return size_; }
  bool valid() const noexcept { return data_ != nullptr; }

  void fill(uint8_t value) {
    if (data_ != nullptr) {
      std::memset(data_, value, size_);
    }
  }

private:
  size_t size_;
  uint8_t *data_;
};

// =============================================================================
//                          智能指针辅助
// =============================================================================

using TensorBufferPtr = std::shared_ptr<SafeTensorBuffer>;
using TensorBufferWeakPtr = std::weak_ptr<SafeTensorBuffer>;

/// 创建 shared_ptr 管理的 SafeTensorBuffer
inline TensorBufferPtr make_tensor_buffer(size_t size) {
  return std::make_shared<SafeTensorBuffer>(size);
}

/// 自定义删除器示例
struct TensorBufferDeleter {
  void operator()(SafeTensorBuffer *ptr) const {
    std::cout << "[TensorBufferDeleter] 自定义删除器" << std::endl;
    delete ptr;
  }
};

using UniqueTensorBuffer =
    std::unique_ptr<SafeTensorBuffer, TensorBufferDeleter>;

#endif // SAFE_TENSOR_BUFFER_HPP
