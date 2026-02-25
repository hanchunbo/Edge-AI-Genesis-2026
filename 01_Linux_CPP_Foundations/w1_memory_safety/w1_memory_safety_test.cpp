// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：SafeTensorBuffer 单元测试
// ============================================================================

#include "safe_tensor_buffer.hpp"

#include <gtest/gtest.h>

// ============================================================================
// 测试套件：构造函数与工厂函数
// ============================================================================

TEST(SafeTensorBufferTest, ConstructorThrowsOnZeroSize) {
  // 直接构造传入 0 应抛出 std::invalid_argument
  EXPECT_THROW(SafeTensorBuffer(0), std::invalid_argument);
}

TEST(SafeTensorBufferTest, ConstructorSuccessForValidSize) {
  SafeTensorBuffer buf(64);
  EXPECT_TRUE(buf.valid());
  EXPECT_EQ(buf.size(), 64u);
  EXPECT_NE(buf.data(), nullptr);
}

TEST(SafeTensorBufferTest, ConstructorZeroInitializesMemory) {
  SafeTensorBuffer buf(16);
  for (size_t i = 0; i < 16; ++i) {
    EXPECT_EQ(buf.data()[i], 0) << "index=" << i;
  }
}

TEST(SafeTensorBufferTest, CreateFactoryZeroSizeReturnsError) {
  auto result = SafeTensorBuffer::Create(0);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), TensorError::kZeroSize);
}

TEST(SafeTensorBufferTest, CreateFactorySuccess) {
  auto result = SafeTensorBuffer::Create(256);
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->valid());
  EXPECT_EQ(result->size(), 256u);
}

TEST(SafeTensorBufferTest, CreateFactoryZeroInitializes) {
  auto result = SafeTensorBuffer::Create(8);
  ASSERT_TRUE(result.has_value());
  auto view = result->View();
  for (uint8_t byte : view) {
    EXPECT_EQ(byte, 0);
  }
}

// ============================================================================
// 测试套件：std::span 视图接口
// ============================================================================

TEST(SafeTensorBufferTest, ViewHasCorrectSize) {
  SafeTensorBuffer buf(128);
  EXPECT_EQ(buf.View().size(), 128u);
}

TEST(SafeTensorBufferTest, MutableViewWriteAndReadBack) {
  SafeTensorBuffer buf(4);
  auto mview = buf.MutableView();
  mview[0] = 0xAA;
  mview[1] = 0xBB;
  mview[2] = 0xCC;
  mview[3] = 0xDD;

  auto rview = buf.View();
  EXPECT_EQ(rview[0], 0xAA);
  EXPECT_EQ(rview[1], 0xBB);
  EXPECT_EQ(rview[2], 0xCC);
  EXPECT_EQ(rview[3], 0xDD);
}

TEST(SafeTensorBufferTest, FillSetsAllBytes) {
  SafeTensorBuffer buf(32);
  buf.fill(0xFF);
  for (uint8_t byte : buf.View()) {
    EXPECT_EQ(byte, 0xFF);
  }
}

// ============================================================================
// 测试套件：At() 边界安全访问
// ============================================================================

TEST(SafeTensorBufferTest, AtInBoundsSuccess) {
  SafeTensorBuffer buf(10);
  buf.fill(0x42);

  auto result = buf.At(0);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(static_cast<uint8_t>(result->get()), 0x42);

  auto last = buf.At(9);
  ASSERT_TRUE(last.has_value());
}

TEST(SafeTensorBufferTest, AtOutOfBoundsReturnsError) {
  SafeTensorBuffer buf(10);
  auto result = buf.At(10);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), TensorError::kOutOfRange);
}

TEST(SafeTensorBufferTest, AtModifiesOriginalData) {
  SafeTensorBuffer buf(4);
  auto elem = buf.At(2);
  ASSERT_TRUE(elem.has_value());
  // At() 返回 reference_wrapper，修改应反映到原始数据
  elem->get() = 0x99;
  EXPECT_EQ(buf.View()[2], 0x99);
}

// ============================================================================
// 测试套件：移动语义（Rule of Five）
// ============================================================================

TEST(SafeTensorBufferTest, MoveConstructorTransfersOwnership) {
  SafeTensorBuffer src(64);
  src.fill(0x5A);
  const uint8_t* original_ptr = src.data();

  SafeTensorBuffer dst(std::move(src));

  // 目标对象接管数据
  EXPECT_TRUE(dst.valid());
  EXPECT_EQ(dst.size(), 64u);
  EXPECT_EQ(dst.data(), original_ptr);  // 指针相同，无拷贝

  // 源对象置为无效
  EXPECT_FALSE(src.valid());
  EXPECT_EQ(src.size(), 0u);
}

TEST(SafeTensorBufferTest, MoveAssignmentTransfersOwnership) {
  SafeTensorBuffer src(32);
  src.fill(0x7F);

  SafeTensorBuffer dst(16);
  dst = std::move(src);

  EXPECT_TRUE(dst.valid());
  EXPECT_EQ(dst.size(), 32u);
  EXPECT_FALSE(src.valid());
}

TEST(SafeTensorBufferTest, CopyIsDeleted) {
  // 验证拷贝构造和拷贝赋值被 delete（编译期约束已通过 static_assert 验证）
  static_assert(!std::is_copy_constructible_v<SafeTensorBuffer>);
  static_assert(!std::is_copy_assignable_v<SafeTensorBuffer>);
  SUCCEED();
}

// ============================================================================
// 测试套件：智能指针辅助
// ============================================================================

TEST(SafeTensorBufferTest, MakeTensorBufferReturnsValid) {
  auto ptr = MakeTensorBuffer(512);
  ASSERT_NE(ptr, nullptr);
  EXPECT_TRUE(ptr->valid());
  EXPECT_EQ(ptr->size(), 512u);
}

// ============================================================================
// 测试套件：TensorError 字符串转换
// ============================================================================

TEST(TensorErrorTest, AllErrorsHaveDescription) {
  EXPECT_FALSE(TensorErrorToString(TensorError::kZeroSize).empty());
  EXPECT_FALSE(TensorErrorToString(TensorError::kAllocationFail).empty());
  EXPECT_FALSE(TensorErrorToString(TensorError::kNullPointer).empty());
  EXPECT_FALSE(TensorErrorToString(TensorError::kOutOfRange).empty());
}
