// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：SemaphoreRingBuffer / SimulatedImage 单元测试
// ============================================================================

#include "producer_consumer.hpp"

#include <gtest/gtest.h>

namespace {

// ============================================================================
// 测试套件：SimulatedImage
// ============================================================================

TEST(SimulatedImageTest, DefaultConstructorValid) {
  w4::SimulatedImage img;
  EXPECT_EQ(img.GetId(), 0u);
  EXPECT_EQ(img.GetWidth(), 0);
  EXPECT_EQ(img.GetDataSize(), 0u);
}

TEST(SimulatedImageTest, ParameterizedConstructor) {
  w4::SimulatedImage img(42, 640, 480);
  EXPECT_EQ(img.GetId(), 42u);
  EXPECT_EQ(img.GetWidth(), 640);
  EXPECT_EQ(img.GetHeight(), 480);
  EXPECT_EQ(img.GetDataSize(), 640u * 480u * 3u);
}

TEST(SimulatedImageTest, MoveConstructorTransfersData) {
  w4::SimulatedImage src(1, 100, 100);
  const size_t expected_size = src.GetDataSize();

  w4::SimulatedImage dst(std::move(src));

  EXPECT_EQ(dst.GetDataSize(), expected_size);
  // 移动后源对象尺寸清零
  EXPECT_EQ(src.GetWidth(), 0);
  EXPECT_EQ(src.GetHeight(), 0);
}

TEST(SimulatedImageTest, MoveIsNoexcept) {
  static_assert(std::is_nothrow_move_constructible_v<w4::SimulatedImage>);
  static_assert(std::is_nothrow_move_assignable_v<w4::SimulatedImage>);
  SUCCEED();
}

TEST(SimulatedImageTest, CopyIsDeleted) {
  static_assert(!std::is_copy_constructible_v<w4::SimulatedImage>);
  SUCCEED();
}

// ============================================================================
// 测试套件：SemaphoreRingBuffer 单线程基本操作
// ============================================================================

using SmallBuffer = w4::SemaphoreRingBuffer<int, 4>;

TEST(SemaphoreRingBufferTest, CapacityIsCorrect) {
  SmallBuffer buf;
  EXPECT_EQ(buf.GetCapacity(), 4u);
}

TEST(SemaphoreRingBufferTest, PushPopSingleItem) {
  SmallBuffer buf;
  EXPECT_TRUE(buf.Push(42));
  auto item = buf.Pop();
  ASSERT_TRUE(item.has_value());
  EXPECT_EQ(*item, 42);
}

TEST(SemaphoreRingBufferTest, PushPopMultipleItems) {
  SmallBuffer buf;
  buf.Push(1);
  buf.Push(2);
  buf.Push(3);

  // FIFO 顺序
  EXPECT_EQ(*buf.Pop(), 1);
  EXPECT_EQ(*buf.Pop(), 2);
  EXPECT_EQ(*buf.Pop(), 3);
}

TEST(SemaphoreRingBufferTest, PopOnEmptyTimesOut) {
  SmallBuffer buf;
  // 空缓冲区 Pop 应在 100ms 超时后返回 nullopt
  auto result = buf.Pop();
  EXPECT_FALSE(result.has_value());
}

TEST(SemaphoreRingBufferTest, StopPreventsNewPush) {
  SmallBuffer buf;
  buf.Stop();
  EXPECT_TRUE(buf.IsStopped());
  EXPECT_FALSE(buf.Push(99));
}

TEST(SemaphoreRingBufferTest, StopMakesPopReturnNullopt) {
  SmallBuffer buf;
  buf.Push(10);
  buf.Stop();

  // Stop 后 Pop 应返回 nullopt（不再消费残留数据）
  auto result = buf.Pop();
  EXPECT_FALSE(result.has_value());
}

// ============================================================================
// 测试套件：多线程生产者-消费者集成测试
// ============================================================================

TEST(ProducerConsumerTest, ProducedEqualsConsumed) {
  // 使用高帧率（跳过帧间等待）快速跑 10 帧
  using BufType = w4::SemaphoreRingBuffer<w4::SimulatedImage, 16>;
  BufType buffer;

  w4::ImageProducer producer(buffer, 1000);  // 1000fps ≈ 无等待
  w4::ImageConsumer consumer(buffer, 1);

  consumer.Start();
  producer.Start(10);
  producer.Join();

  // 等待消费者处理完剩余帧
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  buffer.Stop();
  consumer.Stop();
  consumer.Join();

  EXPECT_EQ(producer.GetProducedCount(), 10u);
  EXPECT_EQ(consumer.GetConsumedCount(), producer.GetProducedCount());
}

}  // namespace
