// SPDX-License-Identifier: MIT
//
// 文件功能：W16 NMS 单元测试（纯函数，无需模型）。

#include "nms.hpp"

#include <chrono>
#include <cstdint>
#include <gtest/gtest.h>
#include <random>
#include <stdexcept>
#include <vector>

namespace {

// 构造一个框的便捷函数。
w16::Detection Box(float x1, float y1, float x2, float y2, float score,
                   int cls) {
  return w16::Detection{x1, y1, x2, y2, score, cls};
}

void ExpectSameDetections(const std::vector<w16::Detection>& lhs,
                          const std::vector<w16::Detection>& rhs) {
  ASSERT_EQ(lhs.size(), rhs.size());
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    EXPECT_FLOAT_EQ(lhs[i].x1, rhs[i].x1);
    EXPECT_FLOAT_EQ(lhs[i].y1, rhs[i].y1);
    EXPECT_FLOAT_EQ(lhs[i].x2, rhs[i].x2);
    EXPECT_FLOAT_EQ(lhs[i].y2, rhs[i].y2);
    EXPECT_FLOAT_EQ(lhs[i].score, rhs[i].score);
    EXPECT_EQ(lhs[i].class_id, rhs[i].class_id);
  }
}

TEST(W16Nms, IoUMatchesHandComputed) {
  // 两个 10x10 框，错开 5 像素：交集 5x5=25，并集 100+100-25=175。
  w16::Detection a = Box(0, 0, 10, 10, 0.9f, 0);
  w16::Detection b = Box(5, 5, 15, 15, 0.8f, 0);
  EXPECT_NEAR(w16::IoU(a, b), 25.0f / 175.0f, 1e-5);
}

TEST(W16Nms, IoUZeroWhenDisjoint) {
  w16::Detection a = Box(0, 0, 10, 10, 0.9f, 0);
  w16::Detection b = Box(20, 20, 30, 30, 0.8f, 0);
  EXPECT_FLOAT_EQ(w16::IoU(a, b), 0.0f);
}

TEST(W16Nms, SuppressesLowerScoreOverlap) {
  // 两个高度重叠同类框，应只保留高分那个。
  std::vector<w16::Detection> dets{
      Box(0, 0, 10, 10, 0.9f, 0),
      Box(1, 1, 11, 11, 0.6f, 0),  // 与上面 IoU 很高，被抑制
  };
  std::vector<w16::Detection> kept = w16::Nms(std::move(dets), 0.45f);
  ASSERT_EQ(kept.size(), 1u);
  EXPECT_FLOAT_EQ(kept[0].score, 0.9f);
}

TEST(W16Nms, DifferentClassesNotSuppressed) {
  // 同一位置不同类别：不应互相抑制。
  std::vector<w16::Detection> dets{
      Box(0, 0, 10, 10, 0.9f, 0),
      Box(0, 0, 10, 10, 0.8f, 1),
  };
  std::vector<w16::Detection> kept = w16::Nms(std::move(dets), 0.45f);
  EXPECT_EQ(kept.size(), 2u);
}

TEST(W16Nms, ResultSortedByScoreDesc) {
  std::vector<w16::Detection> dets{
      Box(0, 0, 10, 10, 0.5f, 0),
      Box(100, 100, 110, 110, 0.9f, 0),
      Box(200, 200, 210, 210, 0.7f, 1),
  };
  std::vector<w16::Detection> kept = w16::Nms(std::move(dets), 0.45f);
  ASSERT_EQ(kept.size(), 3u);
  EXPECT_GE(kept[0].score, kept[1].score);
  EXPECT_GE(kept[1].score, kept[2].score);
}

TEST(W16Nms, OptionsOverloadMatchesLegacyOverload) {
  std::vector<w16::Detection> dets{
      Box(0, 0, 10, 10, 0.9f, 0),
      Box(1, 1, 11, 11, 0.6f, 0),
      Box(100, 100, 110, 110, 0.7f, 1),
  };

  std::vector<w16::Detection> legacy = w16::Nms(dets, 0.45f);
  std::vector<w16::Detection> options =
      w16::Nms(dets, w16::NmsOptions{.iou_thresh = 0.45f});

  ExpectSameDetections(legacy, options);
}

TEST(W16Nms, MaxDetZeroDoesNotLimitResults) {
  std::vector<w16::Detection> dets{
      Box(0, 0, 10, 10, 0.9f, 0),
      Box(20, 20, 30, 30, 0.8f, 0),
      Box(40, 40, 50, 50, 0.7f, 0),
  };

  std::vector<w16::Detection> kept =
      w16::Nms(std::move(dets), w16::NmsOptions{.iou_thresh = 0.45f});

  EXPECT_EQ(kept.size(), 3u);
}

TEST(W16Nms, MaxDetTruncatesAfterGlobalScoreSort) {
  std::vector<w16::Detection> dets{
      Box(0, 0, 10, 10, 0.3f, 0),
      Box(20, 20, 30, 30, 0.9f, 0),
      Box(40, 40, 50, 50, 0.7f, 1),
  };

  std::vector<w16::Detection> kept = w16::Nms(
      std::move(dets), w16::NmsOptions{.iou_thresh = 0.45f, .max_det = 2});

  ASSERT_EQ(kept.size(), 2u);
  EXPECT_FLOAT_EQ(kept[0].score, 0.9f);
  EXPECT_FLOAT_EQ(kept[1].score, 0.7f);
}

TEST(W16Nms, ThrowsOnInvalidOptionsArguments) {
  std::vector<w16::Detection> dets{Box(0, 0, 10, 10, 0.9f, 0)};

  EXPECT_THROW(
      static_cast<void>(w16::Nms(dets, w16::NmsOptions{.iou_thresh = -0.01f})),
      std::invalid_argument);
  EXPECT_THROW(
      static_cast<void>(w16::Nms(dets, w16::NmsOptions{.iou_thresh = 1.01f})),
      std::invalid_argument);
  EXPECT_THROW(
      static_cast<void>(w16::Nms(dets, w16::NmsOptions{.max_det = -1})),
      std::invalid_argument);
}

// stress test（非真实负载——YOLOv8n 过 conf 阈值后通常只剩数十~数百框；
// 这里用 1000 个分散框压一下，验证逐类 NMS 不退化）。
TEST(W16Nms, StressThousandBoxesUnderOneMs) {
  std::mt19937 rng(42);
  std::uniform_real_distribution<float> pos(0.0f, 2000.0f);
  std::uniform_real_distribution<float> score(0.1f, 1.0f);
  std::uniform_int_distribution<int> cls(0, 79);

  std::vector<w16::Detection> dets;
  dets.reserve(1000);
  for (int i = 0; i < 1000; ++i) {
    float x = pos(rng);
    float y = pos(rng);
    dets.push_back(Box(x, y, x + 20.0f, y + 20.0f, score(rng), cls(rng)));
  }

  auto t0 = std::chrono::steady_clock::now();
  std::vector<w16::Detection> kept = w16::Nms(std::move(dets), 0.45f);
  auto t1 = std::chrono::steady_clock::now();

  double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
  EXPECT_FALSE(kept.empty());
  // <1ms 是 Release 成功指标；Debug/ASAN 下放宽，避免无优化带来的假阴性。
#ifdef NDEBUG
  EXPECT_LT(ms, 1.0) << "NMS 1000 框耗时 " << ms
                     << "ms，超过 1ms 预算（Release）";
#else
  EXPECT_LT(ms, 20.0) << "NMS 1000 框耗时 " << ms << "ms（Debug 宽松上限）";
#endif
}

}  // namespace
