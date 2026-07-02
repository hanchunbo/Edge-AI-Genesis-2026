// SPDX-License-Identifier: MIT
//
// 文件功能：W16 YOLODetector 端到端测试 —— 与 ultralytics 参考输出对拍。
//           需要 yolov8n.onnx + 测试图 + 参考检测；任一缺失则 GTEST_SKIP。

#include "yolo_detector.hpp"

#include "nms.hpp"  // w16::IoU

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <vector>

namespace {

// 从 ultralytics 导出的扁平参考文件读取检测框：
// 每行 "class_id score x1 y1 x2 y2"（原图坐标，xyxy）。
std::vector<w16::Detection> LoadReference(const std::string& path) {
  std::ifstream f(path);
  std::vector<w16::Detection> refs;
  int cls;
  float score, x1, y1, x2, y2;
  while (f >> cls >> score >> x1 >> y1 >> x2 >> y2) {
    refs.push_back(w16::Detection{x1, y1, x2, y2, score, cls});
  }
  return refs;
}

bool Exists(const char* p) {
  return std::filesystem::exists(p);
}

void SkipIfAssetsMissing() {
  if (!Exists(W16_MODEL_PATH) || !Exists(W16_IMAGE_PATH)) {
    GTEST_SKIP() << "缺少模型/图片（先跑 tools/ 下的 Python 脚本）";
  }
}

TEST(W16Detector, MatchesUltralyticsReference) {
  if (!Exists(W16_MODEL_PATH) || !Exists(W16_IMAGE_PATH) ||
      !Exists(W16_REFERENCE_PATH)) {
    GTEST_SKIP() << "缺少模型/图片/参考文件（先跑 tools/ 下的 Python 脚本）";
  }

  w16::YOLODetector det(W16_MODEL_PATH);
  std::vector<w16::Detection> got = det.Detect(std::string(W16_IMAGE_PATH));
  std::vector<w16::Detection> refs = LoadReference(W16_REFERENCE_PATH);

  ASSERT_FALSE(refs.empty()) << "参考文件为空";
  // 框数一致（浮点/NMS 顺序差异不影响数量）。
  EXPECT_EQ(got.size(), refs.size());

  // 每个参考框都能被一个同类、IoU>0.9、score 接近的 C++ 框匹配。
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

TEST(W16Detector, HardenedDeploymentConfigRuns) {
  SkipIfAssetsMissing();

  w16::DetectorConfig cfg{
      .input_size = 640,
      .conf_thresh = 0.25f,
      .iou_thresh = 0.45f,
      .ep = w14::Ep::kCpu,
      .intra_op_threads = 1,
      .inter_op_threads = 1,
      .use_iobinding = true,
      .skip_non_finite = true,
      .reserve_hint = 512,
      .max_det = 300,
  };
  w16::YOLODetector det(W16_MODEL_PATH, cfg);

  const std::vector<w16::Detection> got = det.Detect(W16_IMAGE_PATH);

  EXPECT_FALSE(got.empty());
}

TEST(W16Detector, DetectWithProfileReturnsNonNegativeTiming) {
  SkipIfAssetsMissing();

  w16::YOLODetector det(W16_MODEL_PATH,
                        w16::DetectorConfig{.ep = w14::Ep::kCpu});

  const w16::DetectionResult result = det.DetectWithProfile(W16_IMAGE_PATH);

  EXPECT_FALSE(result.detections.empty());
  EXPECT_GE(result.timing.pre_ms, 0.0);
  EXPECT_GE(result.timing.infer_ms, 0.0);
  EXPECT_GE(result.timing.post_ms, 0.0);
  EXPECT_GE(result.timing.total_ms, 0.0);
}

TEST(W16Detector, CudaFallbackReasonReadableWhenCudaUnavailable) {
  SkipIfAssetsMissing();

  w16::YOLODetector det(W16_MODEL_PATH,
                        w16::DetectorConfig{.ep = w14::Ep::kCuda});

  if (det.ActiveEp() == w14::Ep::kCuda) {
    GTEST_SKIP() << "当前环境 CUDA EP 可用，不触发回退 reason";
  }
  EXPECT_FALSE(det.EpFallbackReason().empty());
}

}  // namespace
