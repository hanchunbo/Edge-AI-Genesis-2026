// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：生产者-消费者模型实战 (C++20 版本)
// ============================================================================
//
// 多线程图像处理原型，演示 C++20 现代同步原语。
//
// 【C++20 特性应用】
// ┌─────────────────────────────────────────────────────────────────────────┐
// │ 特性                          │ 替代的传统方案            │ 优势       │
// ├──────────────────────────────┼──────────────────────────┼───────────┤
// │ std::counting_semaphore      │ condition_variable       │ 轻量高效   │
// │ std::binary_semaphore        │ mutex + bool flag        │ 语义清晰   │
// │ std::atomic::wait/notify     │ condition_variable       │ 无锁等待   │
// │ std::format                  │ std::ostringstream       │ 类型安全   │
// │ std::jthread                 │ std::thread + join       │ RAII       │
// └─────────────────────────────────────────────────────────────────────────┘
//
// ============================================================================
// 知识点：std::counting_semaphore vs condition_variable
// ============================================================================
//
// 【condition_variable 的问题】
// 1. 需要配合 mutex 使用（锁开销）
// 2. 可能产生虚假唤醒（spurious wakeup）
// 3. 需要手动处理 predicate
// 4. 使用模式复杂，容易出错
//
// 【std::counting_semaphore 的优势】
// 1. 轻量级：内部使用原子操作，无需 mutex
// 2. 无虚假唤醒：acquire() 返回就一定可以继续
// 3. 语义清晰：天然的资源计数模型
// 4. 性能更好：高并发场景下竞争更少
//
// 【使用场景对比】
// ┌────────────────────────────────────────────────────────────────────────┐
// │ 场景                    │ 推荐方案                                     │
// ├────────────────────────┼─────────────────────────────────────────────┤
// │ 等待某个条件成立        │ condition_variable（灵活的 predicate）      │
// │ 限制资源数量            │ counting_semaphore（如连接池、缓冲区）      │
// │ 简单的信号通知          │ binary_semaphore（替代 cv.notify_one）      │
// │ 高性能无锁等待          │ std::atomic::wait/notify                    │
// └────────────────────────────────────────────────────────────────────────┘
//
// ============================================================================

#ifndef W4_THREADING_PRODUCER_CONSUMER_CPP_
#define W4_THREADING_PRODUCER_CONSUMER_CPP_

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <format>      // C++20 类型安全格式化
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <random>
#include <semaphore>   // C++20 信号量
#include <string>
#include <thread>
#include <vector>

namespace w4 {

// ============================================================================
// 线程安全日志输出（使用 std::format）
// ============================================================================
// [Legacy C++11/17]: std::cout << 拼接或 printf("%d %s", ...) 格式化输出
// [Pain Point]: 类型不安全（printf 格式串与参数不匹配导致运行时错误）、代码冗长难读
// [Modern C++20/23]: std::format 提供编译期类型检查、类型安全的占位符语法
// ============================================================================
std::mutex g_console_mutex;

template <typename... Args>
void ThreadSafeLog(std::format_string<Args...> fmt, Args&&... args) {
  std::lock_guard<std::mutex> lock(g_console_mutex);
  std::cout << std::format(fmt, std::forward<Args>(args)...);
}

// ============================================================================
// SimulatedImage 类 - 模拟图像数据
// ============================================================================
class SimulatedImage {
 public:
  SimulatedImage() : id_(0), width_(0), height_(0), timestamp_(0) {}

  SimulatedImage(uint64_t id, int width, int height)
      : id_(id),
        width_(width),
        height_(height),
        timestamp_(
            std::chrono::steady_clock::now().time_since_epoch().count()) {
    data_.resize(static_cast<size_t>(width * height * 3));
    for (size_t i = 0; i < data_.size(); ++i) {
      data_[i] = static_cast<uint8_t>(i % 256);
    }
  }

  // 移动语义
  SimulatedImage(SimulatedImage&& other) noexcept
      : id_(other.id_),
        width_(other.width_),
        height_(other.height_),
        timestamp_(other.timestamp_),
        data_(std::move(other.data_)) {
    other.id_ = 0;
    other.width_ = 0;
    other.height_ = 0;
  }

  SimulatedImage& operator=(SimulatedImage&& other) noexcept {
    if (this != &other) {
      id_ = other.id_;
      width_ = other.width_;
      height_ = other.height_;
      timestamp_ = other.timestamp_;
      data_ = std::move(other.data_);
      other.id_ = 0;
      other.width_ = 0;
      other.height_ = 0;
    }
    return *this;
  }

  SimulatedImage(const SimulatedImage&) = delete;
  SimulatedImage& operator=(const SimulatedImage&) = delete;

  uint64_t GetId() const { return id_; }
  int GetWidth() const { return width_; }
  int GetHeight() const { return height_; }
  int64_t GetTimestamp() const { return timestamp_; }
  size_t GetDataSize() const { return data_.size(); }

  std::string ToString() const {
    return std::format("Image[id={}, {}x{}, size={} bytes]",
                       id_, width_, height_, data_.size());
  }

 private:
  uint64_t id_;
  int width_;
  int height_;
  int64_t timestamp_;
  std::vector<uint8_t> data_;
};

// ============================================================================
// SemaphoreRingBuffer - 基于信号量的环形缓冲区 (C++20)
// ============================================================================
// [Legacy C++11/17]: condition_variable + mutex + predicate 组合实现生产者-消费者同步
// [Pain Point]: 虚假唤醒需 while 循环处理、锁竞争开销大、使用模式复杂易出错
// [Modern C++20/23]: counting_semaphore 轻量原子操作、无虚假唤醒、语义清晰的资源计数
// ============================================================================
/**
 * @brief 使用 std::counting_semaphore 实现的线程安全环形缓冲区
 *
 * 【设计思想】
 * - 使用两个信号量分别表示空槽数和满槽数
 * - 生产者只需等待空槽，消费者只需等待满槽
 * - 无需额外的 condition_variable
 *
 * 【性能优势】
 * - 信号量的 acquire/release 比 cv.wait 更轻量
 * - 减少锁竞争，提高并发性
 */
template <typename T, size_t Capacity>
class SemaphoreRingBuffer {
 public:
  // ==========================================================================
  // 构造函数
  // ==========================================================================
  SemaphoreRingBuffer()
      : head_(0),
        tail_(0),
        stopped_(false),
        // 初始状态：所有槽位都是空的
        empty_slots_(Capacity),  // 可用空槽数 = Capacity
        full_slots_(0) {         // 已填充槽数 = 0
    static_assert(Capacity > 0, "Capacity must be greater than 0");
  }

  SemaphoreRingBuffer(const SemaphoreRingBuffer&) = delete;
  SemaphoreRingBuffer& operator=(const SemaphoreRingBuffer&) = delete;

  // ==========================================================================
  // Push - 入队操作（信号量版本）
  // ==========================================================================
  /**
   * @brief 入队操作
   *
   * 【信号量工作流程】
   * 1. acquire(empty_slots_)：等待有空槽可用
   * 2. 获取 mutex 保护共享数据
   * 3. 放入数据
   * 4. release(full_slots_)：通知有新数据可消费
   */
  bool Push(T item) {
    // 如果已停止，直接返回失败
    if (stopped_.load()) {
      return false;
    }

    // 等待空槽（阻塞直到有空位）
    empty_slots_.acquire();

    // 再次检查停止状态（可能在 acquire 期间被停止）
    if (stopped_.load()) {
      empty_slots_.release();  // 归还资源
      return false;
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      buffer_[tail_] = std::move(item);
      tail_ = (tail_ + 1) % Capacity;
    }

    // 通知消费者有新数据
    full_slots_.release();
    return true;
  }

  // ==========================================================================
  // Pop - 出队操作（信号量版本）
  // ==========================================================================
  /**
   * @brief 出队操作
   *
   * 【信号量工作流程】
   * 1. acquire(full_slots_)：等待有数据可消费
   * 2. 获取 mutex 保护共享数据
   * 3. 取出数据
   * 4. release(empty_slots_)：通知有空槽可用
   */
  std::optional<T> Pop() {
    // 尝试获取（限时 100ms，避免永久阻塞）
    if (!full_slots_.try_acquire_for(std::chrono::milliseconds(100))) {
      // 超时或无数据
      if (stopped_.load()) {
        return std::nullopt;
      }
      return std::nullopt;  // 超时返回空
    }

    // [Fix] 获取信号量后再次检查停止状态
    // 防止 Stop() 释放的"虚假信号"导致处理无效数据
    if (stopped_.load()) {
      // 归还信号量，避免资源泄漏
      full_slots_.release();
      return std::nullopt;
    }

    T item;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      item = std::move(buffer_[head_]);
      head_ = (head_ + 1) % Capacity;
    }

    // 通知生产者有空槽
    empty_slots_.release();
    return item;
  }

  // ==========================================================================
  // 停止机制
  // ==========================================================================
  void Stop() {
    stopped_.store(true);
    // 释放可能阻塞的信号量
    empty_slots_.release();
    full_slots_.release();
  }

  bool IsStopped() const { return stopped_.load(); }

  size_t GetCapacity() const { return Capacity; }

 private:
  std::array<T, Capacity> buffer_;
  size_t head_;
  size_t tail_;
  std::atomic<bool> stopped_;
  std::mutex mutex_;

  // C++20 计数信号量
  std::counting_semaphore<Capacity> empty_slots_;
  std::counting_semaphore<Capacity> full_slots_;
};

// ============================================================================
// 生产者类
// ============================================================================
// [Legacy C++11/17]: std::thread 需要手动 join()/detach()，忘记调用会导致 terminate()
// [Pain Point]: 异常安全问题——若构造后抛异常，线程资源可能泄漏；需额外 RAII 封装
// [Modern C++20/23]: std::jthread 提供 RAII 自动汇合、内置 stop_token 支持优雅停止
// 注：本示例保留 std::thread 用于对比学习，生产环境建议使用 std::jthread
// ============================================================================
class ImageProducer {
 public:
  using BufferType = SemaphoreRingBuffer<SimulatedImage, 16>;

  ImageProducer(BufferType& buffer, int target_fps = 30)
      : buffer_(buffer),
        target_fps_(target_fps),
        produced_count_(0),
        running_(false) {}

  void Start(int total_frames) {
    running_ = true;
    produced_count_ = 0;
    thread_ = std::thread(&ImageProducer::ProducerLoop, this, total_frames);
  }

  void Join() {
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  uint64_t GetProducedCount() const { return produced_count_; }

 private:
  void ProducerLoop(int total_frames) {
    auto frame_interval = std::chrono::milliseconds(1000 / target_fps_);

    ThreadSafeLog("[Producer] Started, FPS: {}, frames: {}\n",
                  target_fps_, total_frames);

    for (int i = 0; i < total_frames && running_; ++i) {
      auto start_time = std::chrono::steady_clock::now();

      SimulatedImage image(static_cast<uint64_t>(i + 1), 1920, 1080);

      if (buffer_.Push(std::move(image))) {
        ++produced_count_;
        ThreadSafeLog("[Producer] Frame {} pushed\n", i + 1);
      } else {
        ThreadSafeLog("[Producer] Frame {} failed (buffer stopped)\n", i + 1);
        break;
      }

      auto elapsed = std::chrono::steady_clock::now() - start_time;
      if (elapsed < frame_interval) {
        std::this_thread::sleep_for(frame_interval - elapsed);
      }
    }

    ThreadSafeLog("[Producer] Finished, total: {}\n", produced_count_.load());
  }

  BufferType& buffer_;
  int target_fps_;
  std::atomic<uint64_t> produced_count_;
  std::atomic<bool> running_;
  std::thread thread_;
};

// ============================================================================
// 消费者类
// ============================================================================
class ImageConsumer {
 public:
  using BufferType = SemaphoreRingBuffer<SimulatedImage, 16>;

  ImageConsumer(BufferType& buffer, int consumer_id)
      : buffer_(buffer),
        consumer_id_(consumer_id),
        consumed_count_(0),
        total_latency_ms_(0),
        running_(false) {}

  void Start() {
    running_ = true;
    consumed_count_ = 0;
    total_latency_ms_ = 0;
    thread_ = std::thread(&ImageConsumer::ConsumerLoop, this);
  }

  void Stop() { running_ = false; }

  void Join() {
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  uint64_t GetConsumedCount() const { return consumed_count_; }

  double GetAverageLatencyMs() const {
    if (consumed_count_ == 0) return 0.0;
    return total_latency_ms_ / static_cast<double>(consumed_count_);
  }

 private:
  void ConsumerLoop() {
    ThreadSafeLog("[Consumer {}] Started\n", consumer_id_);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> process_time_dist(5, 20);

    while (running_ || !buffer_.IsStopped()) {
      auto image = buffer_.Pop();

      if (image.has_value()) {
        auto current_time =
            std::chrono::steady_clock::now().time_since_epoch().count();
        double latency_ms =
            static_cast<double>(current_time - image->GetTimestamp()) / 1e6;
        total_latency_ms_ += latency_ms;

        int process_time = process_time_dist(gen);
        std::this_thread::sleep_for(std::chrono::milliseconds(process_time));

        ++consumed_count_;

        ThreadSafeLog("[Consumer {}] Processed {}, latency: {:.2f}ms\n",
                      consumer_id_, image->ToString(), latency_ms);
      }

      if (buffer_.IsStopped() && !image.has_value()) {
        break;
      }
    }

    ThreadSafeLog("[Consumer {}] Finished, consumed: {}, avg latency: {:.2f}ms\n",
                  consumer_id_, consumed_count_.load(), GetAverageLatencyMs());
  }

  BufferType& buffer_;
  int consumer_id_;
  std::atomic<uint64_t> consumed_count_;
  double total_latency_ms_;
  std::atomic<bool> running_;
  std::thread thread_;
};

// ============================================================================
// 测试函数
// ============================================================================

void TestSemaphoreBuffer() {
  std::cout << std::format("\n{:=^60}\n", " Test: Semaphore Ring Buffer (C++20) ");

  using BufferType = SemaphoreRingBuffer<SimulatedImage, 16>;
  BufferType buffer;

  ImageProducer producer(buffer, 60);
  ImageConsumer consumer1(buffer, 1);
  ImageConsumer consumer2(buffer, 2);

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

  uint64_t total_produced = producer.GetProducedCount();
  uint64_t total_consumed =
      consumer1.GetConsumedCount() + consumer2.GetConsumedCount();

  std::cout << std::format("\n{:-^40}\n", " Summary ");
  std::cout << std::format("  Produced: {}\n", total_produced);
  std::cout << std::format("  Consumed: {}\n", total_consumed);
  std::cout << std::format("  Consumer 1: {} frames, latency: {:.2f}ms\n",
                           consumer1.GetConsumedCount(),
                           consumer1.GetAverageLatencyMs());
  std::cout << std::format("  Consumer 2: {} frames, latency: {:.2f}ms\n",
                           consumer2.GetConsumedCount(),
                           consumer2.GetAverageLatencyMs());

  if (total_produced == total_consumed) {
    std::cout << std::format("[PASSED] Semaphore buffer test\n");
  } else {
    std::cout << std::format("[FAILED] Mismatch: produced={}, consumed={}\n",
                             total_produced, total_consumed);
  }
}

}  // namespace w4

// ============================================================================
// 主函数
// ============================================================================
int main() {
  std::cout << std::format("{:=^60}\n",
                           " W4: C++20 Threading with Semaphores ");

  w4::TestSemaphoreBuffer();

  std::cout << std::format("\n{:=^60}\n", " All tests completed! ");

  return 0;
}

#endif  // W4_THREADING_PRODUCER_CONSUMER_CPP_
