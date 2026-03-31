// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：ImagePipeline 类实现
//   核心路径：SubmitBatch → worker 线程 → ProcessGray/ProcessTensor →
//   RecordTiming
// ============================================================================

#include "image_pipeline.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <stdexcept>

// W9 BGR2Gray 算子（通过 w9_bgr2gray_lib 链接）
#include "w9_opencv_optimized/bgr2gray.hpp"
// W10 Resize/Letterbox 算子（通过 w10_resize_lib 链接）
#include "w10_resize/custom_resize.hpp"

namespace w13 {

// ============================================================================
// 构造函数
// ============================================================================
ImagePipeline::ImagePipeline(const PipelineConfig& config)
    : config_(config),
      // num_threads==0 时取硬件并发数；ThreadPool 内部再做 max(1) 防御
      pool_(config.num_threads == 0 ? std::thread::hardware_concurrency()
                                    : config.num_threads),
      // 在 pool_ 构造后缓存实际线程数（Shutdown 后
      // workers_.size()==0，需提前记录）
      thread_count_cache_(pool_.GetThreadCount()) {}

// ============================================================================
// 析构函数
// ============================================================================
ImagePipeline::~ImagePipeline() {
  // 显式关闭：即使线程池析构会自动 join，这里也明确表达关闭意图
  Shutdown();
}

// ============================================================================
// SubmitBatch — 批量提交核心路径
// ============================================================================
std::vector<std::future<FrameResult>> ImagePipeline::SubmitBatch(
    std::span<const cv::Mat> frames) {
  std::vector<std::future<FrameResult>> futures;
  futures.reserve(frames.size());

  for (size_t i = 0; i < frames.size(); ++i) {
    // 仅借用引用：cv::Mat 是引用计数的浅拷贝类型，捕获引用不涉及像素拷贝。
    // 生命周期由调用方保证（见头文件中的约定注释）。
    const cv::Mat& frame = frames[i];

    // Submit 内部用 packaged_task 包装 lambda，异常会通过 future 传播。
    // 捕获 i（值）和 frame（引用），mode 通过 this 隐式访问。
    futures.push_back(pool_.Submit([this, &frame, i]() -> FrameResult {
      if (config_.mode == ProcessMode::kGray) {
        return ProcessGray(frame, i);
      } else {
        return ProcessTensor(frame, i);
      }
    }));
  }

  return futures;
}

// ============================================================================
// Submit — 单帧便捷接口
// ============================================================================
std::future<FrameResult> ImagePipeline::Submit(const cv::Mat& frame) {
  // 用临时数组构造 span，委托给 SubmitBatch 保持逻辑一致
  auto futures = SubmitBatch(std::span<const cv::Mat>{&frame, 1});
  return std::move(futures[0]);
}

// ============================================================================
// WaitForAll / Shutdown / ResetStats
// ============================================================================
void ImagePipeline::WaitForAll() {
  pool_.WaitForAll();
}

void ImagePipeline::Shutdown() {
  pool_.Shutdown();
}

void ImagePipeline::ResetStats() noexcept {
  total_frames_.store(0, std::memory_order_relaxed);
  total_us_.store(0, std::memory_order_relaxed);
  min_us_.store(UINT64_MAX, std::memory_order_relaxed);
  max_us_.store(0, std::memory_order_relaxed);

  // 重置墙钟起始标志（下一帧触发时重新记录）
  std::lock_guard<std::mutex> lock(wall_mutex_);
  wall_started_.store(false, std::memory_order_release);
}

// ============================================================================
// 查询接口
// ============================================================================
size_t ImagePipeline::GetThreadCount() const noexcept {
  return thread_count_cache_;
}

bool ImagePipeline::IsStopped() const noexcept {
  return pool_.IsStopped();
}

// ============================================================================
// ProcessGray — 灰度处理路径（在 worker 线程执行）
// ============================================================================
FrameResult ImagePipeline::ProcessGray(const cv::Mat& frame,
                                       size_t frame_index) const {
  // 用 steady_clock 计时：单调时钟，不受系统时间调整影响，适合性能计量
  auto t0 = std::chrono::steady_clock::now();

  // BgrToGrayV4：AVX2 加速版（无 AVX2 时自动回退到 V2，行为等价）
  // w9 函数在 src.empty() 或 type!=CV_8UC3 时抛 std::invalid_argument，
  // packaged_task 会捕获并存入 future，调用方 future.get() 时重新抛出。
  cv::Mat gray = w9::BgrToGrayV4(frame);

  auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - t0);

  // RecordTiming 是无锁的，可以安全地在 worker 线程中调用
  const_cast<ImagePipeline*>(this)->RecordTiming(elapsed);

  return FrameResult{
      .frame_index = frame_index,
      .data = std::move(gray),
      .letterbox_info = {1.0f, 0, 0},  // 灰度模式无 letterbox 变换
      .processing_time = elapsed,
  };
}

// ============================================================================
// ProcessTensor — 张量处理路径（在 worker 线程执行）
// ============================================================================
FrameResult ImagePipeline::ProcessTensor(const cv::Mat& frame,
                                         size_t frame_index) const {
  auto t0 = std::chrono::steady_clock::now();

  // LetterboxToTensor：等比缩放 + 填充 + 归一化 + CHW 转置，一步到位
  // 输出 float32 向量，大小 = 3 × dst_h × dst_w，对应 {1,3,H,W} ONNX 输入
  w10::LetterboxInfo info{};
  std::vector<float> tensor = w10::LetterboxToTensor(
      frame, config_.dst_w, config_.dst_h, info, config_.pad_color);

  auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - t0);

  const_cast<ImagePipeline*>(this)->RecordTiming(elapsed);

  return FrameResult{
      .frame_index = frame_index,
      .data = std::move(tensor),
      .letterbox_info = info,  // 供后处理阶段检测框坐标反算
      .processing_time = elapsed,
  };
}

// ============================================================================
// RecordTiming — 无锁统计写入（worker 线程调用）
// ============================================================================
void ImagePipeline::RecordTiming(
    std::chrono::microseconds elapsed_us) noexcept {
  auto us = static_cast<uint64_t>(elapsed_us.count());

  // relaxed 足够：这几个计数器之间无顺序依赖，汇总时在 GetTimingReport() 用
  // acquire 做一次同步屏障即可
  total_frames_.fetch_add(1, std::memory_order_relaxed);
  total_us_.fetch_add(us, std::memory_order_relaxed);

  // CAS 循环更新原子 min：compare_exchange_weak 允许伪失败，循环重试即可
  uint64_t cur_min = min_us_.load(std::memory_order_relaxed);
  while (us < cur_min &&
         !min_us_.compare_exchange_weak(cur_min, us, std::memory_order_relaxed,
                                        std::memory_order_relaxed)) {
  }

  // CAS 循环更新原子 max
  uint64_t cur_max = max_us_.load(std::memory_order_relaxed);
  while (us > cur_max &&
         !max_us_.compare_exchange_weak(cur_max, us, std::memory_order_relaxed,
                                        std::memory_order_relaxed)) {
  }

  // 双重检查锁定（DCLP）记录墙钟起始时间（仅第一帧触发，之后不再竞争）
  // acquire 保证：读到 true 时，wall_start_ 的写入对本线程可见
  if (!wall_started_.load(std::memory_order_acquire)) {
    std::lock_guard<std::mutex> lock(wall_mutex_);
    // 第二次检查：可能有其他线程已经在持锁阶段完成初始化
    if (!wall_started_.load(std::memory_order_relaxed)) {
      wall_start_ = std::chrono::steady_clock::now();
      // release 保证：后续读取 true 的线程能看到 wall_start_ 的正确值
      wall_started_.store(true, std::memory_order_release);
    }
  }
}

// ============================================================================
// GetTimingReport — 汇总统计报告
// ============================================================================
TimingReport ImagePipeline::GetTimingReport() const {
  // acquire 确保能看到所有 relaxed 写入的最新值
  uint64_t n = total_frames_.load(std::memory_order_acquire);
  if (n == 0) {
    return TimingReport{.thread_count = pool_.GetThreadCount()};
  }

  uint64_t sum = total_us_.load(std::memory_order_relaxed);
  uint64_t mn = min_us_.load(std::memory_order_relaxed);
  uint64_t mx = max_us_.load(std::memory_order_relaxed);

  // 吞吐量基于墙钟时间：wall time 涵盖了线程间的真实并发，
  // 比"累计处理时间/线程数"更接近实际场景的 FPS。
  double fps = 0.0;
  if (wall_started_.load(std::memory_order_acquire)) {
    auto wall_us = std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::steady_clock::now() - wall_start_)
                       .count();
    if (wall_us > 0) {
      fps = static_cast<double>(n) * 1e6 / static_cast<double>(wall_us);
    }
  }

  return TimingReport{
      .total_frames = static_cast<size_t>(n),
      .total_us = static_cast<double>(sum),
      .min_us = static_cast<double>(mn == UINT64_MAX ? 0 : mn),
      .max_us = static_cast<double>(mx),
      .avg_us = static_cast<double>(sum) / static_cast<double>(n),
      .throughput_fps = fps,
      .thread_count = thread_count_cache_,
  };
}

// ============================================================================
// TimingReport::ToString — 格式化输出
// ============================================================================
std::string TimingReport::ToString() const {
  if (total_frames == 0) {
    return "TimingReport: 暂无数据（尚未处理任何帧）\n";
  }
  return std::format(
      "┌────────────────────────────────────────────────┐\n"
      "│ W13 TimingReport                               │\n"
      "├────────────────────────────────────────────────┤\n"
      "│ 总帧数    : {:>6}   线程数: {:>3}               │\n"
      "│ 平均耗时  : {:>8.1f} µs  ({:.3f} ms)           │\n"
      "│ 最小耗时  : {:>8.1f} µs                        │\n"
      "│ 最大耗时  : {:>8.1f} µs                        │\n"
      "│ 吞吐量    : {:>8.1f} FPS                       │\n"
      "└────────────────────────────────────────────────┘\n",
      total_frames, thread_count, avg_us, avg_us / 1000.0, min_us, max_us,
      throughput_fps);
}

}  // namespace w13
