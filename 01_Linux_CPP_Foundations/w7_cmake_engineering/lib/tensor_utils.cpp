// SPDX-License-Identifier: MIT
//
// 文件功能：TensorShape 实现 —— STATIC 库编译单元，实现细节对消费者不可见

#include "tensor_utils.hpp"

#include <numeric>
#include <stdexcept>

namespace w7 {

TensorShape::TensorShape(std::initializer_list<std::size_t> dims)
    : dims_(dims) {}

TensorShape::TensorShape(std::span<const std::size_t> dims)
    : dims_(dims.begin(), dims.end()) {}

std::size_t TensorShape::NumElements() const noexcept {
  if (dims_.empty()) return 0;
  // std::multiplies<> 推导为 std::multiplies<std::size_t>，避免隐式类型转换
  return std::accumulate(dims_.begin(), dims_.end(), std::size_t{1},
                         std::multiplies<>{});
}

std::size_t TensorShape::Rank() const noexcept {
  return dims_.size();
}

std::size_t TensorShape::operator[](std::size_t i) const {
  // 边界检查：边缘部署场景下越界是严重 bug，明确抛出异常优于 UB
  if (i >= dims_.size()) {
    throw std::out_of_range("TensorShape: 维度索引越界");
  }
  return dims_[i];
}

std::span<const std::size_t> TensorShape::Dims() const noexcept {
  // 返回 span 而非 const vector&，避免暴露内部容器类型，同时保持零拷贝
  return std::span<const std::size_t>(dims_);
}

bool TensorShape::operator==(const TensorShape& other) const noexcept {
  return dims_ == other.dims_;
}

bool IsBroadcastable(const TensorShape& a, const TensorShape& b) noexcept {
  // 从尾部维度对齐，逐一检查：相等 or 其中一方为 1
  const auto& da = a.Dims();
  const auto& db = b.Dims();

  std::size_t ia = da.size();
  std::size_t ib = db.size();

  while (ia > 0 && ib > 0) {
    --ia;
    --ib;
    if (da[ia] != db[ib] && da[ia] != 1 && db[ib] != 1) {
      return false;
    }
  }
  return true;
}

}  // namespace w7
