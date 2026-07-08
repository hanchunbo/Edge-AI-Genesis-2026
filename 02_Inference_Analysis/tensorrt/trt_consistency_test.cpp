// SPDX-License-Identifier: MIT
//
// 文件功能：trt 一致性测试 —— TRT FP16 检测结果 vs W16 ultralytics 参考对拍。
//           前后处理复用 w10 letterbox + w16 decode/NMS，只有 infer 段换 TRT。

#include "custom_resize.hpp"  // w10::LetterboxToTensor
#include "decode.hpp"         // w16::DecodeYolov8
#include "engine_builder.hpp"
#include "nms.hpp"  // w16::Nms / w16::IoU
#include "trt_engine.hpp"

#include <cmath>
#include <cuda_runtime_api.h>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <span>
#include <vector>

namespace {

bool CudaAvailable() {
  int count = 0;
  return cudaGetDeviceCount(&count) == cudaSuccess && count > 0;
}

// 与 w16_yolo_detector/yolo_detector_test.cpp 相同的参考文件格式：
// 每行 "class_id score x1 y1 x2 y2"（原图坐标，xyxy）。
std::vector<w16::Detection> LoadReference(const std::string& path) {
  std::ifstream f(path);
  std::vector<w16::Detection> refs;
  int cls = 0;
  float score = 0.0f, x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f;
  while (f >> cls >> score >> x1 >> y1 >> x2 >> y2) {
    refs.push_back(w16::Detection{x1, y1, x2, y2, score, cls});
  }
  return refs;
}

TEST(TrtConsistency, Fp16MatchesUltralyticsReference) {
  if (!CudaAvailable()) {
    GTEST_SKIP() << "无 CUDA 设备（CI/VPS），跳过";
  }
  if (!std::filesystem::exists(TRT_MODEL_PATH) ||
      !std::filesystem::exists(TRT_IMAGE_PATH) ||
      !std::filesystem::exists(TRT_REFERENCE_PATH)) {
    GTEST_SKIP() << "缺少模型/图片/参考文件（W16 资产）";
  }

  // 前处理：与 W16 完全一致（BGR→RGB→letterbox /255 CHW）。
  const cv::Mat bgr = cv::imread(TRT_IMAGE_PATH, cv::IMREAD_COLOR);
  ASSERT_FALSE(bgr.empty());
  cv::Mat rgb;
  cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
  w10::LetterboxInfo info{};
  const std::vector<float> input = w10::LetterboxToTensor(rgb, 640, 640, info);

  // infer 段换 TRT FP16。
  const std::string engine_path = trt::BuildOrLoadEngine(trt::BuildConfig{
      .onnx_path = TRT_MODEL_PATH,
      .cache_dir = TRT_ENGINE_CACHE_DIR,
      .precision = trt::Precision::kFp16,
  });
  trt::TrtEngine engine(engine_path);
  const std::vector<float> output = engine.Infer(input);

  // 后处理：复用 w16 decode + NMS（阈值与 W16 默认一致）。
  ASSERT_EQ(engine.OutputShape().size(), 3u);
  const int num_channels = static_cast<int>(engine.OutputShape()[1]);
  const int num_anchors = static_cast<int>(engine.OutputShape()[2]);
  std::vector<w16::Detection> got = w16::DecodeYolov8(
      std::span<const float>(output.data(), output.size()), num_channels - 4,
      num_anchors, w16::DecodeOptions{.conf_thresh = 0.25f}, info.scale,
      info.pad_left, info.pad_top, bgr.cols, bgr.rows);
  got = w16::Nms(std::move(got), w16::NmsOptions{.iou_thresh = 0.45f});

  const std::vector<w16::Detection> refs = LoadReference(TRT_REFERENCE_PATH);
  ASSERT_FALSE(refs.empty()) << "参考文件为空";
  EXPECT_EQ(got.size(), refs.size());

  // 匹配规则与 W16 测试一致：FP16 量化误差远小于 INT8（quant 已验证 INT8
  // 都能过同源对拍），阈值不放宽。若 FP16 过不了 score±0.05，先记录实际
  // 偏差进 notes.md 再议，不悄悄放宽。
  for (const w16::Detection& r : refs) {
    bool matched = false;
    for (const w16::Detection& g : got) {
      if (g.class_id == r.class_id && w16::IoU(g, r) > 0.9f &&
          std::abs(g.score - r.score) < 0.05f) {
        matched = true;
        break;
      }
    }
    EXPECT_TRUE(matched) << "参考框未匹配: class=" << r.class_id
                         << " score=" << r.score << " box=[" << r.x1 << ","
                         << r.y1 << "," << r.x2 << "," << r.y2 << "]";
  }
}

}  // namespace
