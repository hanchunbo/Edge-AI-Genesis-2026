// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：生产者-消费者核心类声明（从 producer_consumer.cpp 提取）
// ============================================================================

#ifndef W4_THREADING_PRODUCER_CONSUMER_HPP_
#define W4_THREADING_PRODUCER_CONSUMER_HPP_

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <format>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <random>
#include <semaphore>
#include <string>
#include <thread>
#include <vector>

namespace w4 {

// 线程安全日志（inline mutex 避免多编译单元 ODR 违规）
inline std::mutex g_console_mutex;

template <typename... Args>
void ThreadSafeLog(std::format_string<Args...> fmt, Args&&... args) {
  std::lock_guard<std::mutex> lock(g_console_mutex);
  std::cout << std::format(fmt, std::forward<Args>(args)...);
}

// ============================================================================
// SimulatedImage —— 模拟图像数据（只移不拷）
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
    return std::format("Image[id={}, {}x{}, size={} bytes]", id_, width_,
                       height_, data_.size());
  }

 private:
  uint64_t id_;
  int width_;
  int height_;
  int64_t timestamp_;
  std::vector<uint8_t> data_;
};

// ============================================================================
// SemaphoreRingBuffer<T, Capacity> —— 基于 counting_semaphore 的环形缓冲区
// ============================================================================
template <typename T, size_t Capacity>
class SemaphoreRingBuffer {
 public:
  SemaphoreRingBuffer()
      : head_(0),
        tail_(0),
        stopped_(false),
        empty_slots_(Capacity),
        full_slots_(0) {
    static_assert(Capacity > 0, "Capacity must be greater than 0");
  }

  SemaphoreRingBuffer(const SemaphoreRingBuffer&) = delete;
  SemaphoreRingBuffer& operator=(const SemaphoreRingBuffer&) = delete;

  bool Push(T item) {
    if (stopped_.load()) return false;

    empty_slots_.acquire();
    if (stopped_.load()) {
      empty_slots_.release();
      return false;
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      buffer_[tail_] = std::move(item);
      tail_ = (tail_ + 1) % Capacity;
    }

    full_slots_.release();
    return true;
  }

  std::optional<T> Pop() {
    if (!full_slots_.try_acquire_for(std::chrono::milliseconds(100))) {
      return std::nullopt;
    }

    if (stopped_.load()) {
      full_slots_.release();
      return std::nullopt;
    }

    T item;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      item = std::move(buffer_[head_]);
      head_ = (head_ + 1) % Capacity;
    }

    empty_slots_.release();
    return item;
  }

  void Stop() {
    stopped_.store(true);
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
  std::counting_semaphore<Capacity> empty_slots_;
  std::counting_semaphore<Capacity> full_slots_;
};

// ============================================================================
// ImageProducer / ImageConsumer
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
    if (thread_.joinable()) thread_.join();
  }

  uint64_t GetProducedCount() const { return produced_count_; }

 private:
  void ProducerLoop(int total_frames) {
    auto frame_interval = std::chrono::milliseconds(1000 / target_fps_);
    for (int i = 0; i < total_frames && running_; ++i) {
      auto t0 = std::chrono::steady_clock::now();
      SimulatedImage image(static_cast<uint64_t>(i + 1), 1920, 1080);
      if (buffer_.Push(std::move(image))) {
        ++produced_count_;
      } else {
        break;
      }
      auto elapsed = std::chrono::steady_clock::now() - t0;
      if (elapsed < frame_interval)
        std::this_thread::sleep_for(frame_interval - elapsed);
    }
  }

  BufferType& buffer_;
  int target_fps_;
  std::atomic<uint64_t> produced_count_;
  std::atomic<bool> running_;
  std::thread thread_;
};

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
    if (thread_.joinable()) thread_.join();
  }

  uint64_t GetConsumedCount() const { return consumed_count_; }

  double GetAverageLatencyMs() const {
    if (consumed_count_ == 0) return 0.0;
    return total_latency_ms_ / static_cast<double>(consumed_count_);
  }

 private:
  void ConsumerLoop() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(1, 5);

    while (running_ || !buffer_.IsStopped()) {
      auto image = buffer_.Pop();
      if (image.has_value()) {
        auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        total_latency_ms_ +=
            static_cast<double>(now - image->GetTimestamp()) / 1e6;
        std::this_thread::sleep_for(std::chrono::milliseconds(dist(gen)));
        ++consumed_count_;
      }
      if (buffer_.IsStopped() && !image.has_value()) break;
    }
  }

  BufferType& buffer_;
  int consumer_id_;
  std::atomic<uint64_t> consumed_count_;
  double total_latency_ms_;
  std::atomic<bool> running_;
  std::thread thread_;
};

}  // namespace w4

#endif  // W4_THREADING_PRODUCER_CONSUMER_HPP_
