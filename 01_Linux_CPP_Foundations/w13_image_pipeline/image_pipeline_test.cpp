// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：ImagePipeline 单元测试（GTest）
//   覆盖构造/查询、Gray/Tensor 输出格式、LetterboxInfo、并发完整性、
//   TimingReport 统计、生命周期控制（Shutdown/Reset/WaitForAll）等核心路径。
// ============================================================================

#include "image_pipeline.hpp"

#include <chrono>
#include <future>
#include <gtest/gtest.h>
#include <opencv2/core.hpp>
#include <span>
#include <stdexcept>
#include <vector>

namespace {

// 生成随机 BGR 帧（辅助函数）
cv::Mat MakeFrame(int rows = 480, int cols = 640) {
  cv::Mat frame(rows, cols, CV_8UC3);
  cv::randu(frame, cv::Scalar(0, 0, 0), cv::Scalar(255, 255, 255));
  return frame;
}

// ============================================================================
// TC-01：默认构造，基本查询
// ============================================================================
TEST(ImagePipelineTest, DefaultConstructGetThreadCount) {
  w13::ImagePipeline pipeline;
  EXPECT_GT(pipeline.GetThreadCount(), 0u);
  EXPECT_FALSE(pipeline.IsStopped());
  EXPECT_EQ(pipeline.GetConfig().mode, w13::ProcessMode::kTensor);
}

// ============================================================================
// TC-02：自定义线程数
// ============================================================================
TEST(ImagePipelineTest, CustomThreadCount) {
  w13::PipelineConfig cfg{.num_threads = 2};
  w13::ImagePipeline pipeline(cfg);
  EXPECT_EQ(pipeline.GetThreadCount(), 2u);
}

// ============================================================================
// TC-03：kGray 模式输出格式
// ============================================================================
TEST(ImagePipelineTest, GrayModeOutputFormat) {
  w13::PipelineConfig cfg{.mode = w13::ProcessMode::kGray};
  w13::ImagePipeline pipeline(cfg);

  cv::Mat frame = MakeFrame(480, 640);
  auto fut = pipeline.Submit(frame);
  auto result = fut.get();

  EXPECT_TRUE(result.IsGray());
  EXPECT_FALSE(result.IsTensor());
  EXPECT_EQ(result.GetGray().type(), CV_8U);
  EXPECT_EQ(result.GetGray().rows, 480);
  EXPECT_EQ(result.GetGray().cols, 640);
}

// ============================================================================
// TC-04：kTensor 模式输出格式与值域
// ============================================================================
TEST(ImagePipelineTest, TensorModeOutputFormat) {
  w13::PipelineConfig cfg{
      .mode = w13::ProcessMode::kTensor,
      .dst_w = 640,
      .dst_h = 640,
  };
  w13::ImagePipeline pipeline(cfg);

  cv::Mat frame = MakeFrame(1080, 1920);
  auto fut = pipeline.Submit(frame);
  auto result = fut.get();

  EXPECT_FALSE(result.IsGray());
  EXPECT_TRUE(result.IsTensor());

  const auto& tensor = result.GetTensor();
  EXPECT_EQ(tensor.size(), static_cast<size_t>(3 * 640 * 640));

  // 所有值必须在 [0, 1]（归一化后）
  for (float v : tensor) {
    EXPECT_GE(v, 0.0f);
    EXPECT_LE(v, 1.0f);
  }
}

// ============================================================================
// TC-05：LetterboxInfo 数值正确性（1280×720 → 640×640）
// ============================================================================
TEST(ImagePipelineTest, LetterboxInfoCorrectness) {
  w13::PipelineConfig cfg{
      .mode = w13::ProcessMode::kTensor,
      .dst_w = 640,
      .dst_h = 640,
  };
  w13::ImagePipeline pipeline(cfg);

  // 1280×720：scale = min(640/1280, 640/720) = min(0.5, 0.889) = 0.5
  // resize 后宽 640 满，高 360 → pad_top = (640-360)/2 = 140, pad_left = 0
  cv::Mat frame = MakeFrame(720, 1280);
  auto fut = pipeline.Submit(frame);
  auto result = fut.get();

  const auto& info = result.letterbox_info;
  EXPECT_NEAR(info.scale, 0.5f, 0.01f);
  EXPECT_EQ(info.pad_left, 0);
  EXPECT_EQ(info.pad_top, 140);
}

// ============================================================================
// TC-06：SubmitBatch 帧序号保序
// ============================================================================
TEST(ImagePipelineTest, SubmitBatchPreservesOrder) {
  constexpr int kCount = 10;
  w13::PipelineConfig cfg{.mode = w13::ProcessMode::kGray};
  w13::ImagePipeline pipeline(cfg);

  std::vector<cv::Mat> frames;
  frames.reserve(kCount);
  for (int i = 0; i < kCount; ++i) {
    frames.push_back(MakeFrame());
  }

  auto futures = pipeline.SubmitBatch(std::span<const cv::Mat>{frames});
  ASSERT_EQ(futures.size(), static_cast<size_t>(kCount));

  for (size_t i = 0; i < futures.size(); ++i) {
    auto result = futures[i].get();
    EXPECT_EQ(result.frame_index, i);
  }
}

// ============================================================================
// TC-07：50 帧并发，所有 future 均成功，tensor 尺寸正确
// ============================================================================
TEST(ImagePipelineTest, ConcurrentBatchAllSucceed) {
  constexpr int kCount = 50;
  w13::PipelineConfig cfg{
      .mode = w13::ProcessMode::kTensor,
      .dst_w = 640,
      .dst_h = 640,
      .num_threads = 4,
  };
  w13::ImagePipeline pipeline(cfg);

  std::vector<cv::Mat> frames;
  frames.reserve(kCount);
  for (int i = 0; i < kCount; ++i) {
    frames.push_back(MakeFrame(720, 1280));
  }

  auto futures = pipeline.SubmitBatch(std::span<const cv::Mat>{frames});
  ASSERT_EQ(futures.size(), static_cast<size_t>(kCount));

  for (auto& fut : futures) {
    // get() 不应抛异常，且 tensor 尺寸正确
    ASSERT_NO_THROW({
      auto result = fut.get();
      EXPECT_TRUE(result.IsTensor());
      EXPECT_EQ(result.GetTensor().size(), static_cast<size_t>(3 * 640 * 640));
    });
  }
}

// ============================================================================
// TC-08：TimingReport 基本正确性
// ============================================================================
TEST(ImagePipelineTest, TimingReportBasicCorrectness) {
  constexpr int kCount = 20;
  w13::PipelineConfig cfg{.mode = w13::ProcessMode::kGray};
  w13::ImagePipeline pipeline(cfg);

  std::vector<cv::Mat> frames(kCount, MakeFrame());
  auto futures = pipeline.SubmitBatch(std::span<const cv::Mat>{frames});
  for (auto& f : futures)
    f.get();

  auto report = pipeline.GetTimingReport();
  EXPECT_EQ(report.total_frames, static_cast<size_t>(kCount));
  EXPECT_LE(report.min_us, report.avg_us + 0.01);
  EXPECT_GE(report.max_us, report.avg_us - 0.01);
  EXPECT_GT(report.throughput_fps, 0.0);
  EXPECT_GT(report.thread_count, 0u);
}

// ============================================================================
// TC-09：ResetStats 效果验证
// ============================================================================
TEST(ImagePipelineTest, ResetStatsWorks) {
  w13::PipelineConfig cfg{.mode = w13::ProcessMode::kGray};
  w13::ImagePipeline pipeline(cfg);

  // 先处理 10 帧
  std::vector<cv::Mat> batch10(10, MakeFrame());
  auto f10 = pipeline.SubmitBatch(std::span<const cv::Mat>{batch10});
  for (auto& f : f10)
    f.get();

  pipeline.ResetStats();

  // 再处理 5 帧
  std::vector<cv::Mat> batch5(5, MakeFrame());
  auto f5 = pipeline.SubmitBatch(std::span<const cv::Mat>{batch5});
  for (auto& f : f5)
    f.get();

  auto report = pipeline.GetTimingReport();
  // Reset 后计数从头，应为 5，不是 15
  EXPECT_EQ(report.total_frames, 5u);
}

// ============================================================================
// TC-10：Shutdown 后提交抛异常
// ============================================================================
TEST(ImagePipelineTest, SubmitAfterShutdownThrows) {
  w13::PipelineConfig cfg{.mode = w13::ProcessMode::kGray};
  w13::ImagePipeline pipeline(cfg);
  pipeline.Shutdown();

  cv::Mat frame = MakeFrame();
  // 用 lambda 包裹，绕过 [[nodiscard]] 警告（异常在 Submit 内部抛出，不在
  // get()）
  EXPECT_THROW(
      { [[maybe_unused]] auto f = pipeline.Submit(frame); },
      std::runtime_error);
}

// ============================================================================
// TC-11：空 span 提交
// ============================================================================
TEST(ImagePipelineTest, EmptyBatchReturnsEmpty) {
  w13::ImagePipeline pipeline;
  std::span<const cv::Mat> empty_span;

  std::vector<std::future<w13::FrameResult>> futures;
  ASSERT_NO_THROW({ futures = pipeline.SubmitBatch(empty_span); });
  EXPECT_TRUE(futures.empty());

  auto report = pipeline.GetTimingReport();
  EXPECT_EQ(report.total_frames, 0u);
}

// ============================================================================
// TC-12：WaitForAll 后所有 future 均就绪
// ============================================================================
TEST(ImagePipelineTest, WaitForAllMakesFuturesReady) {
  constexpr int kCount = 20;
  w13::PipelineConfig cfg{.mode = w13::ProcessMode::kGray};
  w13::ImagePipeline pipeline(cfg);

  std::vector<cv::Mat> frames(kCount, MakeFrame());
  auto futures = pipeline.SubmitBatch(std::span<const cv::Mat>{frames});
  pipeline.WaitForAll();

  for (auto& fut : futures) {
    // WaitForAll 后 future 应已就绪，wait_for(0) 不应阻塞
    EXPECT_EQ(fut.wait_for(std::chrono::seconds(0)), std::future_status::ready);
  }
}

// ============================================================================
// TC-13：空帧输入的异常传播
// ============================================================================
TEST(ImagePipelineTest, EmptyFramePropagatesException) {
  w13::PipelineConfig cfg{.mode = w13::ProcessMode::kGray};
  w13::ImagePipeline pipeline(cfg);

  cv::Mat empty_frame;  // empty() == true
  auto fut = pipeline.Submit(empty_frame);

  // W9 会抛 invalid_argument，packaged_task 捕获后存入 future，get() 重新抛出
  EXPECT_THROW(fut.get(), std::invalid_argument);
}

// ============================================================================
// TC-14：多次 Shutdown 幂等
// ============================================================================
TEST(ImagePipelineTest, MultipleShutdownIsIdempotent) {
  w13::ImagePipeline pipeline;
  ASSERT_NO_THROW({
    pipeline.Shutdown();
    pipeline.Shutdown();
    pipeline.Shutdown();
  });
  EXPECT_TRUE(pipeline.IsStopped());
}

}  // namespace
