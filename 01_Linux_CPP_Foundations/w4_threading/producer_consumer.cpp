// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：生产者-消费者演示程序入口
// ============================================================================

#include "producer_consumer.hpp"

#include <format>
#include <iostream>

// ============================================================================
// 演示函数
// ============================================================================
void TestSemaphoreBuffer() {
  std::cout << std::format("\n{:=^60}\n",
                           " Test: Semaphore Ring Buffer (C++20) ");

  using BufferType = w4::SemaphoreRingBuffer<w4::SimulatedImage, 16>;
  BufferType buffer;

  w4::ImageProducer producer(buffer, 60);
  w4::ImageConsumer consumer1(buffer, 1);
  w4::ImageConsumer consumer2(buffer, 2);

  consumer1.Start();
  consumer2.Start();
  producer.Start(30);

  producer.Join();
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  buffer.Stop();
  consumer1.Stop();
  consumer2.Stop();
  consumer1.Join();
  consumer2.Join();

  uint64_t produced = producer.GetProducedCount();
  uint64_t consumed =
      consumer1.GetConsumedCount() + consumer2.GetConsumedCount();

  std::cout << std::format("\n  Produced: {}, Consumed: {}\n", produced,
                           consumed);
  if (produced == consumed) {
    std::cout << std::format("[PASSED] Semaphore buffer test\n");
  } else {
    std::cout << std::format("[FAILED] Mismatch: {} vs {}\n", produced,
                             consumed);
  }
}

int main() {
  std::cout << std::format("{:=^60}\n",
                           " W4: C++20 Threading with Semaphores ");
  TestSemaphoreBuffer();
  std::cout << std::format("\n{:=^60}\n", " Done ");
  return 0;
}
