// SPDX-License-Identifier: MIT
//
// 文件功能：TensorShape 单元测试 —— 同样无需手动指定 include 路径，验证 PUBLIC
// 传播

#include "tensor_utils.hpp"

#include <gtest/gtest.h>

// ─────────────── TensorShape 构造与基础属性 ───────────────

TEST(TensorShapeTest, DefaultConstruct) {
  w7::TensorShape shape;
  EXPECT_EQ(shape.Rank(), 0u);
  // 空形状的元素数定义为 0（与 NumPy 行为一致）
  EXPECT_EQ(shape.NumElements(), 0u);
}

TEST(TensorShapeTest, InitializerListConstruct) {
  w7::TensorShape shape{1, 3, 224, 224};
  EXPECT_EQ(shape.Rank(), 4u);
  EXPECT_EQ(shape.NumElements(), 1u * 3u * 224u * 224u);
}

TEST(TensorShapeTest, SpanConstruct) {
  std::array<std::size_t, 3> dims{2, 64, 64};
  std::span<const std::size_t> s{dims};
  w7::TensorShape shape{s};
  EXPECT_EQ(shape.Rank(), 3u);
  EXPECT_EQ(shape.NumElements(), 2u * 64u * 64u);
}

TEST(TensorShapeTest, IndexAccess) {
  w7::TensorShape shape{1, 3, 224, 224};
  EXPECT_EQ(shape[0], 1u);    // batch
  EXPECT_EQ(shape[1], 3u);    // channels
  EXPECT_EQ(shape[2], 224u);  // height
  EXPECT_EQ(shape[3], 224u);  // width
}

TEST(TensorShapeTest, IndexOutOfRangeThrows) {
  w7::TensorShape shape{1, 3, 224};
  // 越界必须抛异常，而非触发 UB
  // static_cast<void> 显式丢弃返回值，消除 [[nodiscard]] 警告
  EXPECT_THROW(static_cast<void>(shape[3]), std::out_of_range);
}

TEST(TensorShapeTest, DimsViewIsZeroCopy) {
  w7::TensorShape shape{4, 8, 16};
  auto dims = shape.Dims();
  // span 不拷贝数据，size 应与 Rank 一致
  EXPECT_EQ(dims.size(), shape.Rank());
  EXPECT_EQ(dims[0], 4u);
  EXPECT_EQ(dims[2], 16u);
}

TEST(TensorShapeTest, EqualityOperator) {
  w7::TensorShape a{1, 3, 224, 224};
  w7::TensorShape b{1, 3, 224, 224};
  w7::TensorShape c{1, 3, 224, 256};
  EXPECT_EQ(a, b);
  EXPECT_NE(a, c);
}

// ─────────────── NumElements 边界情况 ───────────────

TEST(TensorShapeTest, SingleDim) {
  w7::TensorShape shape{512};
  EXPECT_EQ(shape.Rank(), 1u);
  EXPECT_EQ(shape.NumElements(), 512u);
}

TEST(TensorShapeTest, ContainsDimOne) {
  // {1, 1, 1} 是合法形状，NumElements = 1
  w7::TensorShape shape{1, 1, 1};
  EXPECT_EQ(shape.NumElements(), 1u);
}

// ─────────────── IsBroadcastable ───────────────

TEST(IsBroadcastableTest, IdenticalShapes) {
  w7::TensorShape a{3, 224, 224};
  EXPECT_TRUE(w7::IsBroadcastable(a, a));
}

TEST(IsBroadcastableTest, BroadcastWithOnes) {
  // {1, 224, 224} 可广播到 {3, 224, 224}
  w7::TensorShape a{1, 224, 224};
  w7::TensorShape b{3, 224, 224};
  EXPECT_TRUE(w7::IsBroadcastable(a, b));
  EXPECT_TRUE(w7::IsBroadcastable(b, a));
}

TEST(IsBroadcastableTest, IncompatibleDims) {
  // {2, 224} 不能广播到 {3, 224}：第 0 维 2 vs 3，均非 1
  w7::TensorShape a{2, 224};
  w7::TensorShape b{3, 224};
  EXPECT_FALSE(w7::IsBroadcastable(a, b));
}

TEST(IsBroadcastableTest, DifferentRank) {
  // {224, 224} 与 {1, 3, 224, 224}：从尾部对齐均为 224，兼容
  w7::TensorShape a{224, 224};
  w7::TensorShape b{1, 3, 224, 224};
  EXPECT_TRUE(w7::IsBroadcastable(a, b));
}

// ─────────────── CMake INTERFACE 宏注入验证 ───────────────

TEST(CMakeInterfaceTest, MacroInjected) {
  // W7_TENSOR_UTILS_AVAILABLE 通过 lib/CMakeLists.txt 的 INTERFACE 定义注入
  // 如果编译通过此断言成立，说明 INTERFACE 传播机制正确工作
#ifdef W7_TENSOR_UTILS_AVAILABLE
  EXPECT_EQ(W7_TENSOR_UTILS_AVAILABLE, 1);
#else
  FAIL() << "W7_TENSOR_UTILS_AVAILABLE 未定义，INTERFACE 传播失败";
#endif
}
