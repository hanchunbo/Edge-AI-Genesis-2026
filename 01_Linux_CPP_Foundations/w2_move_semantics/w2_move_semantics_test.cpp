// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：CustomImage 移动语义与 std::span 视图单元测试
// ============================================================================

#include "custom_image.hpp"

#include <gtest/gtest.h>
#include <numeric>

// 每个测试前重置静态计数器，避免用例间互相干扰
class CustomImageTest : public ::testing::Test {
 protected:
  void SetUp() override { CustomImage::ResetCounters(); }
};

// ============================================================================
// 测试套件：构造函数
// ============================================================================

TEST_F(CustomImageTest, DefaultConstructorValid) {
  CustomImage img;
  EXPECT_TRUE(img.Valid());
  EXPECT_EQ(img.Size(), CustomImage::kImageSize);
  EXPECT_NE(img.Data(), nullptr);
}

TEST_F(CustomImageTest, FillConstructorSetsAllBytes) {
  CustomImage img(0xAB);
  for (uint8_t byte : img.View()) {
    EXPECT_EQ(byte, 0xAB);
  }
}

TEST_F(CustomImageTest, DefaultConstructorZeroInitializes) {
  CustomImage img;
  // 默认构造 memset(0)
  for (uint8_t byte : img.View()) {
    EXPECT_EQ(byte, 0u);
  }
}

TEST_F(CustomImageTest, ConstructionCountIncrements) {
  CustomImage a;
  CustomImage b;
  EXPECT_EQ(CustomImage::GetConstructionCount(), 2u);
}

// ============================================================================
// 测试套件：深拷贝语义
// ============================================================================

TEST_F(CustomImageTest, CopyConstructorDeepCopy) {
  CustomImage src(0x11);
  CustomImage dst(src);  // 深拷贝

  // 修改副本，不影响原始数据
  dst.MutableView()[0] = 0xFF;
  EXPECT_EQ(src.View()[0], 0x11);
  EXPECT_EQ(dst.View()[0], 0xFF);
}

TEST_F(CustomImageTest, CopyConstructorIncrementsCopyCount) {
  CustomImage src;
  CustomImage dst(src);
  EXPECT_EQ(CustomImage::GetCopyCount(), 1u);
}

TEST_F(CustomImageTest, CopyAssignmentDeepCopy) {
  CustomImage src(0x22);
  CustomImage dst;
  dst = src;

  dst.MutableView()[0] = 0xFF;
  EXPECT_EQ(src.View()[0], 0x22);
}

// ============================================================================
// 测试套件：移动语义（零拷贝）
// ============================================================================

TEST_F(CustomImageTest, MoveConstructorTransfersData) {
  CustomImage src(0x33);
  const uint8_t* original_ptr = src.Data();

  CustomImage dst(std::move(src));

  // 目标对象持有原始数据指针，无内存拷贝
  EXPECT_EQ(dst.Data(), original_ptr);
  EXPECT_TRUE(dst.Valid());
  EXPECT_EQ(dst.View()[0], 0x33);

  // 源对象变为无效（nullptr + size=0）
  EXPECT_FALSE(src.Valid());
  EXPECT_EQ(src.Data(), nullptr);
}

TEST_F(CustomImageTest, MoveConstructorIncrementsMoveCount) {
  CustomImage src;
  CustomImage dst(std::move(src));
  EXPECT_EQ(CustomImage::GetMoveCount(), 1u);
  EXPECT_EQ(CustomImage::GetCopyCount(), 0u);  // 移动不产生拷贝
}

TEST_F(CustomImageTest, MoveAssignmentTransfersData) {
  CustomImage src(0x44);
  CustomImage dst;
  dst = std::move(src);

  EXPECT_TRUE(dst.Valid());
  EXPECT_FALSE(src.Valid());
  EXPECT_EQ(dst.View()[0], 0x44);
}

TEST_F(CustomImageTest, MoveIsNoexcept) {
  // noexcept 保证 std::vector 扩容时优先使用移动而非拷贝
  static_assert(std::is_nothrow_move_constructible_v<CustomImage>);
  static_assert(std::is_nothrow_move_assignable_v<CustomImage>);
  SUCCEED();
}

// ============================================================================
// 测试套件：std::span 视图接口
// ============================================================================

TEST_F(CustomImageTest, ViewHasCorrectSize) {
  CustomImage img;
  EXPECT_EQ(img.View().size(), CustomImage::kImageSize);
  EXPECT_EQ(img.MutableView().size(), CustomImage::kImageSize);
}

TEST_F(CustomImageTest, RowViewCorrectSize) {
  CustomImage img;
  auto row = img.RowView(0);
  EXPECT_EQ(row.size(), CustomImage::kWidth * CustomImage::kChannels);
}

TEST_F(CustomImageTest, RowViewOutOfBoundsReturnsEmpty) {
  CustomImage img;
  // 行索引等于 kHeight 是越界
  EXPECT_TRUE(img.RowView(CustomImage::kHeight).empty());
  EXPECT_TRUE(img.RowView(CustomImage::kHeight + 10).empty());
}

TEST_F(CustomImageTest, RowViewCorrectOffset) {
  CustomImage img(0x00);
  // 给第 1 行第 0 个字节写入特定值
  size_t row1_offset = CustomImage::kWidth * CustomImage::kChannels;
  img.MutableView()[row1_offset] = 0xBB;

  auto row1 = img.RowView(1);
  EXPECT_EQ(row1[0], 0xBB);
  // 第 0 行不受影响
  EXPECT_EQ(img.RowView(0)[0], 0x00);
}

// ============================================================================
// 测试套件：工具函数
// ============================================================================

TEST(CalculateAveragePixelTest, EmptySpanReturnsZero) {
  EXPECT_DOUBLE_EQ(CalculateAveragePixel({}), 0.0);
}

TEST(CalculateAveragePixelTest, UniformValueReturnsExact) {
  // 所有像素均为 100，平均值应精确为 100.0
  std::vector<uint8_t> data(1000, 100);
  EXPECT_DOUBLE_EQ(CalculateAveragePixel(data), 100.0);
}

TEST(CalculateAveragePixelTest, KnownValues) {
  std::vector<uint8_t> data = {0, 50, 100, 150, 200};
  double expected = (0.0 + 50 + 100 + 150 + 200) / 5.0;
  EXPECT_DOUBLE_EQ(CalculateAveragePixel(data), expected);
}

TEST(AdjustBrightnessTest, IncreaseBrightness) {
  std::vector<uint8_t> data = {100, 200};
  AdjustBrightness(data, 50);
  EXPECT_EQ(data[0], 150);
  EXPECT_EQ(data[1], 250);
}

TEST(AdjustBrightnessTest, ClampAtMax) {
  std::vector<uint8_t> data = {240};
  AdjustBrightness(data, 50);
  EXPECT_EQ(data[0], 255);  // 钳位到 255
}

TEST(AdjustBrightnessTest, ClampAtMin) {
  std::vector<uint8_t> data = {10};
  AdjustBrightness(data, -50);
  EXPECT_EQ(data[0], 0);  // 钳位到 0
}
