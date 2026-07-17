// SPDX-License-Identifier: MIT
//
// 文件功能：quant batch 一致性测试 —— 验证 batch=4 输出切片与 batch=1 对齐。

#include "custom_resize.hpp"
#include "decode.hpp"
#include "inference_engine.hpp"
#include "nms.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <gtest/gtest.h>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <span>
#include <utility>
#include <vector>

namespace {

constexpr int kInput = 640;
constexpr int kBatch = 4;
// 同一份输入在 batch=1 与 batch=4 下应逐位可比：差异只可能来自 ORT 内部的
// 并行归约顺序，故容差取浮点级而非精度级——超出即说明 batch 切片错位。
constexpr float kScoreTol = 1e-5f;
constexpr float kCoordTol = 1e-4f;

/// 缺模型或测试图时跳过用例——CI 与裸克隆环境没有这些产物。
void SkipIfAssetsMissing() {
  if (!std::filesystem::exists(QUANT_W16_MODEL_PATH) ||
      !std::filesystem::exists(QUANT_W16_IMAGE_PATH)) {
    GTEST_SKIP() << "缺少 YOLOv8n 模型或测试图";
  }
}

/// 把单图张量复制 batch 份，拼成 [batch,3,H,W] 输入。
std::vector<float> TileBatch(const std::vector<float>& one, int batch) {
  std::vector<float> out;
  out.reserve(one.size() * static_cast<std::size_t>(batch));
  for (int i = 0; i < batch; ++i) {
    out.insert(out.end(), one.begin(), one.end());
  }
  return out;
}

std::vector<w16::Detection> DecodeAndNms(std::span<const float> output,
                                         int num_classes, int num_anchors,
                                         const w10::LetterboxInfo& info,
                                         int img_w, int img_h) {
  std::vector<w16::Detection> dets = w16::DecodeYolov8(
      output, num_classes, num_anchors,
      w16::DecodeOptions{
          .conf_thresh = 0.25f, .skip_non_finite = true, .reserve_hint = 512},
      info.scale, info.pad_left, info.pad_top, img_w, img_h);
  return w16::Nms(std::move(dets),
                  w16::NmsOptions{.iou_thresh = 0.45f, .max_det = 300});
}

void ExpectSameDetections(const std::vector<w16::Detection>& expected,
                          const std::vector<w16::Detection>& actual) {
  ASSERT_EQ(actual.size(), expected.size());
  for (std::size_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ(actual[i].class_id, expected[i].class_id);
    EXPECT_NEAR(actual[i].score, expected[i].score, kScoreTol);
    EXPECT_NEAR(actual[i].x1, expected[i].x1, kCoordTol);
    EXPECT_NEAR(actual[i].y1, expected[i].y1, kCoordTol);
    EXPECT_NEAR(actual[i].x2, expected[i].x2, kCoordTol);
    EXPECT_NEAR(actual[i].y2, expected[i].y2, kCoordTol);
  }
}

TEST(QuantBatchConsistency, BatchFourSlicesMatchBatchOne) {
  SkipIfAssetsMissing();

  const cv::Mat bgr = cv::imread(QUANT_W16_IMAGE_PATH, cv::IMREAD_COLOR);
  ASSERT_FALSE(bgr.empty());
  cv::Mat rgb;
  cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
  w10::LetterboxInfo info{};
  const std::vector<float> one =
      w10::LetterboxToTensor(rgb, kInput, kInput, info);

  w14::InferenceEngine engine1(QUANT_W16_MODEL_PATH,
                               w14::SessionConfig{.ep = w14::Ep::kCpu});
  w14::InferenceEngine engine4(QUANT_W16_MODEL_PATH,
                               w14::SessionConfig{.ep = w14::Ep::kCpu});
  const std::vector<int64_t> shape1{1, 3, kInput, kInput};
  const std::vector<int64_t> shape4{kBatch, 3, kInput, kInput};

  std::vector<Ort::Value> out1 = engine1.Run(one, shape1);
  const std::vector<float> batch = TileBatch(one, kBatch);
  std::vector<Ort::Value> out4 = engine4.Run(batch, shape4);

  const std::vector<int64_t> shape_out1 =
      out1[0].GetTensorTypeAndShapeInfo().GetShape();
  const std::vector<int64_t> shape_out4 =
      out4[0].GetTensorTypeAndShapeInfo().GetShape();
  ASSERT_EQ(shape_out1.size(), 3u);
  ASSERT_EQ(shape_out4.size(), 3u);
  ASSERT_EQ(shape_out1[0], 1);
  ASSERT_EQ(shape_out4[0], kBatch);
  ASSERT_EQ(shape_out1[1], shape_out4[1]);
  ASSERT_EQ(shape_out1[2], shape_out4[2]);

  const int num_classes = static_cast<int>(shape_out1[1]) - 4;
  const int num_anchors = static_cast<int>(shape_out1[2]);
  const std::size_t one_output_count = static_cast<std::size_t>(shape_out1[1]) *
                                       static_cast<std::size_t>(shape_out1[2]);

  const float* data1 = out1[0].GetTensorData<float>();
  const float* data4 = out4[0].GetTensorData<float>();
  const std::vector<w16::Detection> expected =
      DecodeAndNms(std::span<const float>(data1, one_output_count), num_classes,
                   num_anchors, info, bgr.cols, bgr.rows);
  ASSERT_FALSE(expected.empty());

  for (int b = 0; b < kBatch; ++b) {
    const std::span<const float> slice(data4 + one_output_count * b,
                                       one_output_count);
    const std::vector<w16::Detection> actual =
        DecodeAndNms(slice, num_classes, num_anchors, info, bgr.cols, bgr.rows);
    ExpectSameDetections(expected, actual);
  }
}

}  // namespace
