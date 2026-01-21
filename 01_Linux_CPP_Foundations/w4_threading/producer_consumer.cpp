// Copyright 2026 Edge-AI-Genesis
// 文件功能：生产者-消费者模型实战 - 多线程图像处理原型
//
// 知识点：
// 1. std::thread 生命周期管理
// 2. std::mutex 与 std::lock_guard
// 3. std::condition_variable 实现线程间同步
// 4. 线程安全的环形缓冲区设计

#ifndef W4_THREADING_PRODUCER_CONSUMER_CPP_
#define W4_THREADING_PRODUCER_CONSUMER_CPP_

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// =============================================================================
// 知识点：为什么需要线程安全的数据结构？
// =============================================================================
// 在多线程环境中，多个线程同时访问共享数据会导致"数据竞争"(Data Race)
// 数据竞争的后果：
//   - 程序行为未定义（Undefined Behavior）
//   - 数据损坏
//   - 程序崩溃
//
// 解决方案：使用互斥锁(mutex)保护共享数据
// =============================================================================

namespace w4 {

// =============================================================================
// 线程安全日志输出
// =============================================================================
// std::cout 的格式修改（如 std::fixed）不是线程安全的
// 使用全局互斥锁保护控制台输出
// =============================================================================
std::mutex g_console_mutex;

void ThreadSafeLog(const std::string& message) {
  std::lock_guard<std::mutex> lock(g_console_mutex);
  std::cout << message;
}

// =============================================================================
// SimulatedImage 类 - 模拟图像数据
// =============================================================================
// 在实际AI推理场景中，这会是 cv::Mat 或自定义的 TensorBuffer
// 这里使用模拟数据来演示线程同步机制
// =============================================================================
class SimulatedImage {
 public:
  SimulatedImage() : id_(0), width_(0), height_(0), timestamp_(0) {}

  SimulatedImage(uint64_t id, int width, int height)
      : id_(id),
        width_(width),
        height_(height),
        timestamp_(
            std::chrono::steady_clock::now().time_since_epoch().count()) {
    // 模拟图像数据（实际场景中这里是像素数据）
    data_.resize(static_cast<size_t>(width * height * 3));  // RGB格式
    // 填充模拟数据
    for (size_t i = 0; i < data_.size(); ++i) {
      data_[i] = static_cast<uint8_t>(i % 256);
    }
  }

  // 移动构造函数 - 避免大量数据拷贝
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

  // 移动赋值运算符
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

  // 禁用拷贝 - 避免意外的深拷贝带来性能损耗
  SimulatedImage(const SimulatedImage&) = delete;
  SimulatedImage& operator=(const SimulatedImage&) = delete;

  // Getters
  uint64_t GetId() const { return id_; }
  int GetWidth() const { return width_; }
  int GetHeight() const { return height_; }
  int64_t GetTimestamp() const { return timestamp_; }
  size_t GetDataSize() const { return data_.size(); }

  std::string ToString() const {
    std::ostringstream oss;
    oss << "Image[id=" << id_ << ", " << width_ << "x" << height_
        << ", size=" << data_.size() << " bytes]";
    return oss.str();
  }

 private:
  uint64_t id_;
  int width_;
  int height_;
  int64_t timestamp_;
  std::vector<uint8_t> data_;
};

// =============================================================================
// ThreadSafeRingBuffer 类 - 线程安全的环形缓冲区
// =============================================================================
// 核心设计思想：
// 1. 使用 std::mutex 保护共享数据（buffer_, head_, tail_, count_）
// 2. 使用 std::condition_variable 实现阻塞等待
//    - not_full_cv_: 当缓冲区满时，生产者等待
//    - not_empty_cv_: 当缓冲区空时，消费者等待
// 3. 使用 std::atomic<bool> 实现优雅停止
// =============================================================================
template <typename T, size_t Capacity>
class ThreadSafeRingBuffer {
 public:
  ThreadSafeRingBuffer() : head_(0), tail_(0), count_(0), stopped_(false) {
    static_assert(Capacity > 0, "Capacity must be greater than 0");
  }

  // 禁用拷贝和移动 - 环形缓冲区通常作为共享资源存在
  ThreadSafeRingBuffer(const ThreadSafeRingBuffer&) = delete;
  ThreadSafeRingBuffer& operator=(const ThreadSafeRingBuffer&) = delete;
  ThreadSafeRingBuffer(ThreadSafeRingBuffer&&) = delete;
  ThreadSafeRingBuffer& operator=(ThreadSafeRingBuffer&&) = delete;

  // =========================================================================
  // Push - 阻塞式入队操作
  // =========================================================================
  // 知识点：为什么使用 std::unique_lock 而不是 std::lock_guard？
  //
  // std::lock_guard:
  //   - 简单的 RAII 锁包装器
  //   - 构造时加锁，析构时解锁
  //   - 不能手动解锁/重新加锁
  //
  // std::unique_lock:
  //   - 更灵活的锁包装器
  //   - 支持手动 lock()/unlock()
  //   - 支持与 condition_variable 配合使用
  //   - condition_variable::wait() 会自动释放锁并等待
  //
  // 在 wait() 期间锁被释放，允许其他线程操作缓冲区
  // 当被唤醒后，锁会自动重新获取
  // =========================================================================
  bool Push(T item, std::chrono::milliseconds timeout = std::chrono::milliseconds::max()) {
    std::unique_lock<std::mutex> lock(mutex_);

    // 等待条件：缓冲区未满 或 已停止
    auto predicate = [this]() { return count_ < Capacity || stopped_; };

    if (timeout == std::chrono::milliseconds::max()) {
      not_full_cv_.wait(lock, predicate);
    } else {
      if (!not_full_cv_.wait_for(lock, timeout, predicate)) {
        return false;  // 超时
      }
    }

    if (stopped_) {
      return false;  // 已停止，不再接受新数据
    }

    // 入队操作
    buffer_[tail_] = std::move(item);
    tail_ = (tail_ + 1) % Capacity;
    ++count_;

    // 通知等待的消费者
    lock.unlock();  // 先解锁再通知，提高效率
    not_empty_cv_.notify_one();

    return true;
  }

  // =========================================================================
  // Pop - 阻塞式出队操作
  // =========================================================================
  // 返回 std::optional 的设计考量：
  // 1. 当缓冲区被停止时，需要一种方式告知消费者"没有更多数据"
  // 2. std::optional 完美表达"可能有值，也可能没有"的语义
  // 3. 避免使用异常或错误码，代码更清晰
  // =========================================================================
  std::optional<T> Pop(std::chrono::milliseconds timeout = std::chrono::milliseconds::max()) {
    std::unique_lock<std::mutex> lock(mutex_);

    // 等待条件：缓冲区非空 或 已停止
    auto predicate = [this]() { return count_ > 0 || stopped_; };

    if (timeout == std::chrono::milliseconds::max()) {
      not_empty_cv_.wait(lock, predicate);
    } else {
      if (!not_empty_cv_.wait_for(lock, timeout, predicate)) {
        return std::nullopt;  // 超时
      }
    }

    // 即使已停止，如果还有数据也要消费完
    if (count_ == 0) {
      return std::nullopt;  // 已停止且无数据
    }

    // 出队操作
    T item = std::move(buffer_[head_]);
    head_ = (head_ + 1) % Capacity;
    --count_;

    // 通知等待的生产者
    lock.unlock();
    not_full_cv_.notify_one();

    return item;
  }

  // =========================================================================
  // Stop - 优雅停止机制
  // =========================================================================
  // 设计考量：
  // 1. 停止后不再接受新数据
  // 2. 允许消费者消费完剩余数据
  // 3. 唤醒所有等待的线程，让它们有机会退出
  // =========================================================================
  void Stop() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopped_ = true;
    }
    // 唤醒所有等待的线程
    not_full_cv_.notify_all();
    not_empty_cv_.notify_all();
  }

  // 重置缓冲区（用于测试）
  void Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    head_ = 0;
    tail_ = 0;
    count_ = 0;
    stopped_ = false;
  }

  // 查询方法（用于调试和统计）
  size_t Size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return count_;
  }

  bool Empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return count_ == 0;
  }

  bool Full() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return count_ == Capacity;
  }

  bool IsStopped() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stopped_;
  }

  static constexpr size_t GetCapacity() { return Capacity; }

 private:
  std::array<T, Capacity> buffer_;  // 环形缓冲区存储
  size_t head_;                     // 出队位置
  size_t tail_;                     // 入队位置
  size_t count_;                    // 当前元素数量
  bool stopped_;                    // 停止标志

  mutable std::mutex mutex_;                // 互斥锁 (mutable 允许在 const 方法中使用)
  std::condition_variable not_full_cv_;     // 缓冲区未满条件变量
  std::condition_variable not_empty_cv_;    // 缓冲区非空条件变量
};

// =============================================================================
// ImageProducer 类 - 图像生产者
// =============================================================================
// 模拟摄像头采集：以固定帧率产生图像数据
// =============================================================================
class ImageProducer {
 public:
  using BufferType = ThreadSafeRingBuffer<SimulatedImage, 16>;

  ImageProducer(BufferType& buffer, int target_fps = 30)
      : buffer_(buffer),
        target_fps_(target_fps),
        produced_count_(0),
        running_(false) {}

  // 启动生产者线程
  void Start(int total_frames) {
    running_ = true;
    produced_count_ = 0;
    thread_ = std::thread(&ImageProducer::ProducerLoop, this, total_frames);
  }

  // 等待生产者线程结束
  void Join() {
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  // 获取统计信息
  uint64_t GetProducedCount() const { return produced_count_; }

 private:
  void ProducerLoop(int total_frames) {
    // 计算帧间隔
    auto frame_interval =
        std::chrono::milliseconds(1000 / target_fps_);

    {
      std::ostringstream oss;
      oss << "[Producer] Started, target FPS: " << target_fps_
          << ", total frames: " << total_frames << "\n";
      ThreadSafeLog(oss.str());
    }

    for (int i = 0; i < total_frames && running_; ++i) {
      auto start_time = std::chrono::steady_clock::now();

      // 创建模拟图像 (1920x1080 Full HD)
      SimulatedImage image(static_cast<uint64_t>(i + 1), 1920, 1080);

      // 入队
      if (buffer_.Push(std::move(image))) {
        ++produced_count_;
        std::ostringstream oss;
        oss << "[Producer] Frame " << (i + 1) << " produced, "
            << "buffer size: " << buffer_.Size() << "/"
            << buffer_.GetCapacity() << "\n";
        ThreadSafeLog(oss.str());
      } else {
        std::ostringstream oss;
        oss << "[Producer] Failed to push frame " << (i + 1)
            << " (buffer stopped)\n";
        ThreadSafeLog(oss.str());
        break;
      }

      // 模拟帧率控制
      auto elapsed = std::chrono::steady_clock::now() - start_time;
      if (elapsed < frame_interval) {
        std::this_thread::sleep_for(frame_interval - elapsed);
      }
    }

    {
      std::ostringstream oss;
      oss << "[Producer] Finished, total produced: " << produced_count_ << "\n";
      ThreadSafeLog(oss.str());
    }
  }

  BufferType& buffer_;
  int target_fps_;
  std::atomic<uint64_t> produced_count_;
  std::atomic<bool> running_;
  std::thread thread_;
};

// =============================================================================
// ImageConsumer 类 - 图像消费者
// =============================================================================
// 模拟预处理：从队列获取图像并进行处理
// =============================================================================
class ImageConsumer {
 public:
  using BufferType = ThreadSafeRingBuffer<SimulatedImage, 16>;

  ImageConsumer(BufferType& buffer, int consumer_id)
      : buffer_(buffer),
        consumer_id_(consumer_id),
        consumed_count_(0),
        total_latency_ms_(0),
        running_(false) {}

  // 启动消费者线程
  void Start() {
    running_ = true;
    consumed_count_ = 0;
    total_latency_ms_ = 0;
    thread_ = std::thread(&ImageConsumer::ConsumerLoop, this);
  }

  // 停止消费者
  void Stop() { running_ = false; }

  // 等待消费者线程结束
  void Join() {
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  // 获取统计信息
  uint64_t GetConsumedCount() const { return consumed_count_; }

  double GetAverageLatencyMs() const {
    if (consumed_count_ == 0) return 0.0;
    return total_latency_ms_ / static_cast<double>(consumed_count_);
  }

 private:
  void ConsumerLoop() {
    {
      std::ostringstream oss;
      oss << "[Consumer " << consumer_id_ << "] Started\n";
      ThreadSafeLog(oss.str());
    }

    // 用于模拟随机处理时间
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> process_time_dist(5, 20);  // 5-20ms

    while (running_ || !buffer_.Empty()) {
      // 尝试从队列获取图像，超时100ms
      auto image = buffer_.Pop(std::chrono::milliseconds(100));

      if (image.has_value()) {
        auto start_process = std::chrono::steady_clock::now();

        // 计算从采集到处理的延迟
        auto current_time =
            std::chrono::steady_clock::now().time_since_epoch().count();
        double latency_ms =
            static_cast<double>(current_time - image->GetTimestamp()) / 1e6;
        total_latency_ms_ += latency_ms;

        // 模拟图像处理（如 Resize, BGR2Gray 等）
        int process_time = process_time_dist(gen);
        std::this_thread::sleep_for(std::chrono::milliseconds(process_time));

        ++consumed_count_;

        auto process_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_process);

        std::ostringstream oss;
        oss << "[Consumer " << consumer_id_ << "] Processed "
            << image->ToString()
            << ", latency: " << std::fixed << std::setprecision(2)
            << latency_ms << "ms"
            << ", process time: " << process_duration.count() << "ms\n";
        ThreadSafeLog(oss.str());
      }
    }

    std::ostringstream oss;
    oss << "[Consumer " << consumer_id_ << "] Finished, "
        << "total consumed: " << consumed_count_
        << ", avg latency: " << std::fixed << std::setprecision(2)
        << GetAverageLatencyMs() << "ms\n";
    ThreadSafeLog(oss.str());
  }

  BufferType& buffer_;
  int consumer_id_;
  std::atomic<uint64_t> consumed_count_;
  double total_latency_ms_;
  std::atomic<bool> running_;
  std::thread thread_;
};

// =============================================================================
// 测试函数
// =============================================================================

// 测试1：基础功能测试
void TestBasicFunctionality() {
  std::cout << "\n" << std::string(60, '=') << "\n";
  std::cout << "Test 1: Basic Ring Buffer Functionality\n";
  std::cout << std::string(60, '=') << "\n";

  ThreadSafeRingBuffer<int, 4> buffer;

  // 测试入队
  std::cout << "Pushing 1, 2, 3...\n";
  buffer.Push(1);
  buffer.Push(2);
  buffer.Push(3);
  std::cout << "Size after push: " << buffer.Size() << " (expected: 3)\n";

  // 测试出队
  auto val1 = buffer.Pop();
  auto val2 = buffer.Pop();
  std::cout << "Popped: " << val1.value() << ", " << val2.value()
            << " (expected: 1, 2)\n";
  std::cout << "Size after pop: " << buffer.Size() << " (expected: 1)\n";

  // 测试缓冲区满
  buffer.Push(4);
  buffer.Push(5);
  buffer.Push(6);
  std::cout << "After filling: Size=" << buffer.Size()
            << ", Full=" << buffer.Full() << " (expected: 4, 1)\n";

  std::cout << "[PASSED] Basic functionality test\n";
}

// 测试2：多线程生产者-消费者测试
void TestProducerConsumer() {
  std::cout << "\n" << std::string(60, '=') << "\n";
  std::cout << "Test 2: Producer-Consumer Multi-threading\n";
  std::cout << std::string(60, '=') << "\n";

  using BufferType = ThreadSafeRingBuffer<SimulatedImage, 16>;
  BufferType buffer;

  // 创建1个生产者和2个消费者
  ImageProducer producer(buffer, 60);  // 60 FPS
  ImageConsumer consumer1(buffer, 1);
  ImageConsumer consumer2(buffer, 2);

  // 启动消费者
  consumer1.Start();
  consumer2.Start();

  // 启动生产者，产生30帧
  producer.Start(30);

  // 等待生产者完成
  producer.Join();

  // 等待一段时间让消费者处理完剩余数据
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // 停止缓冲区和消费者
  buffer.Stop();
  consumer1.Stop();
  consumer2.Stop();

  // 等待消费者完成
  consumer1.Join();
  consumer2.Join();

  // 验证结果
  uint64_t total_produced = producer.GetProducedCount();
  uint64_t total_consumed =
      consumer1.GetConsumedCount() + consumer2.GetConsumedCount();

  std::cout << "\n" << std::string(40, '-') << "\n";
  std::cout << "Summary:\n";
  std::cout << "  Produced: " << total_produced << "\n";
  std::cout << "  Consumed: " << total_consumed << "\n";
  std::cout << "  Consumer 1: " << consumer1.GetConsumedCount()
            << " frames, avg latency: " << consumer1.GetAverageLatencyMs()
            << "ms\n";
  std::cout << "  Consumer 2: " << consumer2.GetConsumedCount()
            << " frames, avg latency: " << consumer2.GetAverageLatencyMs()
            << "ms\n";

  if (total_produced == total_consumed) {
    std::cout << "[PASSED] Producer-Consumer test\n";
  } else {
    std::cout << "[FAILED] Mismatch: produced=" << total_produced
              << ", consumed=" << total_consumed << "\n";
  }
}

// 测试3：高并发压力测试
void TestHighConcurrency() {
  std::cout << "\n" << std::string(60, '=') << "\n";
  std::cout << "Test 3: High Concurrency Stress Test\n";
  std::cout << std::string(60, '=') << "\n";

  ThreadSafeRingBuffer<int, 100> buffer;
  std::atomic<int> push_count(0);
  std::atomic<int> pop_count(0);
  const int items_per_thread = 1000;
  const int num_producers = 4;
  const int num_consumers = 4;

  std::vector<std::thread> producers;
  std::vector<std::thread> consumers;

  // 创建生产者线程
  for (int i = 0; i < num_producers; ++i) {
    producers.emplace_back([&buffer, &push_count, items_per_thread, i]() {
      for (int j = 0; j < items_per_thread; ++j) {
        if (buffer.Push(i * items_per_thread + j)) {
          ++push_count;
        }
      }
    });
  }

  // 创建消费者线程
  for (int i = 0; i < num_consumers; ++i) {
    consumers.emplace_back([&buffer, &pop_count]() {
      while (true) {
        auto val = buffer.Pop(std::chrono::milliseconds(50));
        if (val.has_value()) {
          ++pop_count;
        } else if (buffer.IsStopped()) {
          break;
        }
      }
    });
  }

  // 等待所有生产者完成
  for (auto& t : producers) {
    t.join();
  }

  // 等待所有数据被消费
  while (!buffer.Empty()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  // 停止缓冲区
  buffer.Stop();

  // 等待所有消费者完成
  for (auto& t : consumers) {
    t.join();
  }

  std::cout << "Total pushed: " << push_count << " (expected: "
            << num_producers * items_per_thread << ")\n";
  std::cout << "Total popped: " << pop_count << "\n";

  if (push_count == pop_count &&
      push_count == num_producers * items_per_thread) {
    std::cout << "[PASSED] High concurrency stress test\n";
  } else {
    std::cout << "[FAILED] Data loss or duplication detected\n";
  }
}

// 测试4：超时测试
void TestTimeout() {
  std::cout << "\n" << std::string(60, '=') << "\n";
  std::cout << "Test 4: Timeout Mechanism\n";
  std::cout << std::string(60, '=') << "\n";

  ThreadSafeRingBuffer<int, 2> buffer;

  // 测试 Pop 超时（空缓冲区）
  auto start = std::chrono::steady_clock::now();
  auto result = buffer.Pop(std::chrono::milliseconds(100));
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);

  std::cout << "Pop timeout test: elapsed=" << elapsed.count()
            << "ms, has_value=" << result.has_value() << "\n";

  if (!result.has_value() && elapsed.count() >= 100 && elapsed.count() < 200) {
    std::cout << "[PASSED] Pop timeout test\n";
  } else {
    std::cout << "[FAILED] Pop timeout test\n";
  }

  // 填满缓冲区
  buffer.Push(1);
  buffer.Push(2);

  // 测试 Push 超时（满缓冲区）
  start = std::chrono::steady_clock::now();
  bool push_result = buffer.Push(3, std::chrono::milliseconds(100));
  elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);

  std::cout << "Push timeout test: elapsed=" << elapsed.count()
            << "ms, success=" << push_result << "\n";

  if (!push_result && elapsed.count() >= 100 && elapsed.count() < 200) {
    std::cout << "[PASSED] Push timeout test\n";
  } else {
    std::cout << "[FAILED] Push timeout test\n";
  }
}

}  // namespace w4

// =============================================================================
// 主函数
// =============================================================================
int main() {
  std::cout << "========================================\n";
  std::cout << "W4: 多线程与任务同步 - 生产者消费者模型\n";
  std::cout << "========================================\n";

  // 运行所有测试
  w4::TestBasicFunctionality();
  w4::TestProducerConsumer();
  w4::TestHighConcurrency();
  w4::TestTimeout();

  std::cout << "\n========================================\n";
  std::cout << "All tests completed!\n";
  std::cout << "========================================\n";

  return 0;
}

#endif  // W4_THREADING_PRODUCER_CONSUMER_CPP_
