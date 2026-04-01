// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：高性能多线程图像预处理引擎（W13 综合项目）
//   封装 W5 线程池 + W9 BGR→Gray + W10 Letterbox，对外暴露批量处理 API。
//   作为 Q1 所有知识的综合产出，兼做 Q2 推理引擎的预处理前端。
// ============================================================================
//
// [Legacy C++11/17]: 手写线程调度 + 全局 mutex 保护结果 map + 回调通知
// [Pain Point]: 数据竞争风险高，错误通过返回码传递，调用方必须轮询结果
// [Modern C++20]: jthread + packaged_task + future，异常自动传播，future.get()
// 同步

#ifndef W13_IMAGE_PIPELINE_HPP_
#define W13_IMAGE_PIPELINE_HPP_

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <mutex>
#include <opencv2/core.hpp>
#include <span>
#include <string>
#include <variant>
#include <vector>

// W5 线程池（header-only），通过 include 路径 01_Linux_CPP_Foundations 寻址
#include "w5_thread_pool/thread_pool.hpp"
// W10 LetterboxInfo 结构体定义（FrameResult 需要存储它）
#include "w10_resize/custom_resize.hpp"

namespace w13 {

// ============================================================================
// ProcessMode — 处理模式枚举
// ============================================================================
// kGray  ：BGR → BgrToGrayV4（灰度任务，FrameResult.data 为 cv::Mat CV_8U）
// kTensor：BGR → LetterboxToTensor（推理引擎，FrameResult.data 为 float32 CHW）
enum class ProcessMode {
  kGray,    // 灰度模式：输出 uint8 灰度图
  kTensor,  // 张量模式：输出 float32 CHW，对齐 ONNX/TensorRT {1,3,H,W}
};

// ============================================================================
// PipelineConfig — 管道配置参数
// ============================================================================
struct PipelineConfig {
  // 处理模式（默认张量模式，直连推理引擎）
  ProcessMode mode{ProcessMode::kTensor};

  // 推理引擎目标输入尺寸（kTensor 模式）
  int dst_w{640};
  int dst_h{640};

  // Letterbox 填充色（kTensor 模式，默认 YOLOv5/v8 灰边）
  cv::Scalar pad_color{114.0, 114.0, 114.0};

  // 线程数（0 = std::thread::hardware_concurrency，下限夹为 1）
  size_t num_threads{0};
};

// ============================================================================
// FrameResult — 单帧处理结果
// ============================================================================
// data 字段类型由 ProcessMode 决定：
//   kGray   → cv::Mat（CV_8U 单通道灰度图）
//   kTensor → std::vector<float>（CHW float32，大小 = 3 × dst_h × dst_w）
//
// 使用 std::variant 而非虚函数：零堆分配开销，编译期类型检查。
struct FrameResult {
  // 帧序号（SubmitBatch 内按输入顺序分配，从 0 开始）
  size_t frame_index{0};

  // 处理后的数据载体
  std::variant<cv::Mat, std::vector<float>> data;

  // Letterbox 元信息（用于后处理阶段将检测框坐标反算回原始图像）
  // kGray 模式下 scale=1.0, pad_left=0, pad_top=0（无意义，仅占位）
  w10::LetterboxInfo letterbox_info{1.0f, 0, 0};

  // 本帧处理耗时（worker 线程内计时，微秒精度）
  std::chrono::microseconds processing_time{0};

  // --------------------------------------------------------------------------
  // 便捷访问器
  // --------------------------------------------------------------------------

  [[nodiscard]] bool IsGray() const noexcept {
    return std::holds_alternative<cv::Mat>(data);
  }

  [[nodiscard]] bool IsTensor() const noexcept {
    return std::holds_alternative<std::vector<float>>(data);
  }

  // 获取灰度图；类型不匹配时抛 std::bad_variant_access
  [[nodiscard]] const cv::Mat& GetGray() const {
    return std::get<cv::Mat>(data);
  }

  // 获取推理张量；类型不匹配时抛 std::bad_variant_access
  [[nodiscard]] const std::vector<float>& GetTensor() const {
    return std::get<std::vector<float>>(data);
  }
};

// ============================================================================
// TimingReport — 吞吐量与延迟统计报告
// ============================================================================
// 设计原则：多线程并发写入用无锁原子累加，汇总报告在调用方线程生成（无额外锁）。
struct TimingReport {
  // 已处理总帧数
  size_t total_frames{0};

  // 所有帧的累计处理时间（微秒）
  double total_us{0.0};

  // 单帧最小/最大/平均耗时（微秒）
  double min_us{0.0};
  double max_us{0.0};
  double avg_us{0.0};

  // 吞吐量（帧/秒），基于墙钟时间（wall clock）而非累计时间，反映真实并发吞吐
  double throughput_fps{0.0};

  // 记录此次使用的线程数（便于对比不同配置）
  size_t thread_count{0};

  // 格式化输出（用 std::format 生成表格，适合重定向至 docs/benchmarks）
  [[nodiscard]] std::string ToString() const;
};

// ============================================================================
// ImagePipeline — 核心管道类
// ============================================================================
//
// 职责：
//   1. 持有 W5 ThreadPool（线程生命周期由 RAII 管理）
//   2. SubmitBatch() 将 N 帧并发分派到线程池，每帧独立执行
//   3. 每帧在 worker 线程内调用 W9::BgrToGrayV4 或 W10::LetterboxToTensor
//   4. 通过无锁原子计数器累积 TimingReport（消除伪共享）
//
// 线程安全性：
//   - SubmitBatch() 可从任意线程调用（ThreadPool 内部持锁）
//   - Shutdown() 调用后再 SubmitBatch() 会抛 std::runtime_error（由 W5 抛出）
//   - GetTimingReport() 应在所有 future
//   就绪后调用（无内部屏障，调用方负责同步）
//
class ImagePipeline {
 public:
  // 显式构造，禁止隐式转换（PipelineConfig 参数含默认值，可零参数构造）
  explicit ImagePipeline(const PipelineConfig& config = PipelineConfig{});

  // 析构时调用 Shutdown()（jthread RAII 会自动 join，但显式关闭更清晰）
  ~ImagePipeline();

  // 禁止拷贝和移动（持有 non-copyable 的 atomic + 线程池）
  ImagePipeline(const ImagePipeline&) = delete;
  ImagePipeline& operator=(const ImagePipeline&) = delete;
  ImagePipeline(ImagePipeline&&) = delete;
  ImagePipeline& operator=(ImagePipeline&&) = delete;

  // --------------------------------------------------------------------------
  // 核心接口：批量提交
  // --------------------------------------------------------------------------
  //
  // 将 span 内每一帧异步提交到线程池，立即返回 future 向量。
  // 调用方通过 futures[i].get() 获取第 i 帧的 FrameResult。
  //
  // 生命周期约定（重要）：
  //   传入 frames 所指向的 cv::Mat 对象必须在所有 future 就绪之前保持有效。
  //   cv::Mat 内部是引用计数的浅拷贝，只要调用方持有原始 vector/array 不释放，
  //   像素数据不会被回收，实践中几乎不构成问题。
  //
  [[nodiscard]] std::vector<std::future<FrameResult>> SubmitBatch(
      std::span<const cv::Mat> frames);

  // 单帧便捷接口（内部委托 SubmitBatch，保持接口一致性）
  [[nodiscard]] std::future<FrameResult> Submit(const cv::Mat& frame);

  // --------------------------------------------------------------------------
  // 控制接口
  // --------------------------------------------------------------------------

  // 阻塞直到所有已提交任务完成（委托 W5::ThreadPool::WaitForAll）
  void WaitForAll();

  // 优雅关闭：停止接受新任务，等待所有 worker 退出（幂等，可重复调用）
  void Shutdown();

  // 重置计时统计数据（不影响线程池状态，可用于分段 benchmark）
  void ResetStats() noexcept;

  // --------------------------------------------------------------------------
  // 查询接口
  // --------------------------------------------------------------------------

  // 生成并返回当前计时报告（建议在 WaitForAll() 后调用以获得完整数据）
  [[nodiscard]] TimingReport GetTimingReport() const;

  [[nodiscard]] size_t GetThreadCount() const noexcept;
  [[nodiscard]] bool IsStopped() const noexcept;

  [[nodiscard]] const PipelineConfig& GetConfig() const noexcept {
    return config_;
  }

 private:
  // --------------------------------------------------------------------------
  // 内部处理函数（在 worker 线程中执行）
  // --------------------------------------------------------------------------

  // 灰度模式：调用 w9::BgrToGrayV4，计时后填充 FrameResult
  [[nodiscard]] FrameResult ProcessGray(const cv::Mat& frame,
                                        size_t frame_index) const;

  // 张量模式：调用 w10::LetterboxToTensor，计时后填充 FrameResult
  [[nodiscard]] FrameResult ProcessTensor(const cv::Mat& frame,
                                          size_t frame_index) const;

  // 将单帧耗时写入原子统计计数器（无锁，worker 线程调用）
  // const：修改的是 mutable 统计成员，不改变帧处理语义（逻辑 const）
  void RecordTiming(std::chrono::microseconds elapsed_us) const noexcept;

  // --------------------------------------------------------------------------
  // 成员变量（按访问热度分组，热点变量独占缓存行）
  // --------------------------------------------------------------------------

  // 配置参数（只读，构造后不变，无需对齐）
  PipelineConfig config_;

  // 线程池（RAII 管理 jthread，析构时自动 join）
  w5::ThreadPool pool_;

  // 构造时缓存线程数：Shutdown() 后 pool_.GetThreadCount() 返回 0（workers_
  // 已清空）， 缓存确保 TimingReport 中始终记录实际工作线程数
  size_t thread_count_cache_;

  // ── 热点：多线程并发写入的原子统计计数器
  // ──────────────────────────────────── 每个 alignas(64) 变量独占一条 64
  // 字节缓存行，消除 false sharing。 四个计数器分开对齐，即使 worker
  // 线程并发写入也不会互相干扰。

  // mutable：统计数据属于"逻辑 const"不破坏的可观测状态，
  // 允许在 const 方法（ProcessGray/ProcessTensor/RecordTiming）中写入，
  // 避免对 const this 使用 const_cast（UB 隐患）。
  // alignas 必须在 mutable 之前（C++ 属性语法规则）
  alignas(64) mutable std::atomic<uint64_t> total_frames_{0};
  alignas(64) mutable std::atomic<uint64_t> total_us_{0};
  // min 初始化为最大值，第一帧写入后才有意义（CAS 循环更新）
  alignas(64) mutable std::atomic<uint64_t> min_us_{UINT64_MAX};
  alignas(64) mutable std::atomic<uint64_t> max_us_{0};

  // ── 墙钟起始时间（用于吞吐量计算，仅第一帧处理时写入一次）────────────────
  // wall_started_ 用双重检查锁定（DCLP）保护 wall_start_ 的首次写入。
  // acquire/release 配对确保写入对其他线程可见。
  alignas(64) mutable std::atomic<bool> wall_started_{false};
  mutable std::chrono::steady_clock::time_point wall_start_;
  mutable std::mutex wall_mutex_;  // 仅用于 wall_start_ 初始化的互斥
};

}  // namespace w13

#endif  // W13_IMAGE_PIPELINE_HPP_
