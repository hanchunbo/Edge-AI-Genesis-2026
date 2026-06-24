// SPDX-License-Identifier: MIT
//
// 文件功能：W16 YOLODetector 编排类 —— 组合 W14 推理引擎，串起
//           BGR→RGB→Letterbox→推理→解码→NMS 的端到端检测流水线。

#ifndef EDGE_AI_GENESIS_2026_W16_YOLO_DETECTOR_HPP_
#define EDGE_AI_GENESIS_2026_W16_YOLO_DETECTOR_HPP_

#include "detection.hpp"
#include "inference_engine.hpp"  // w14::InferenceEngine（经 lib PUBLIC include 可达）

#include <opencv2/core.hpp>
#include <string>
#include <vector>

namespace w16 {

// 检测器配置。默认值对齐 ultralytics YOLOv8 推理默认。
struct DetectorConfig {
  int input_size = 640;         // 模型方形输入边长
  float conf_thresh = 0.25f;    // 候选框置信度阈值
  float iou_thresh = 0.45f;     // NMS IoU 阈值
  w14::Ep ep = w14::Ep::kCuda;  // 默认 CUDA EP（不可用时引擎内部回退 CPU）
};

// YOLODetector：**组合**（非继承）W14 InferenceEngine。
// InferenceEngine 无虚函数、非为继承设计，组合边界更清晰。
class YOLODetector {
 public:
  YOLODetector(const std::string& model_path, DetectorConfig cfg = {});

  // 从已解码 BGR 图检测，返回原图坐标系下、已 NMS 的检测框。
  [[nodiscard]] std::vector<Detection> Detect(const cv::Mat& bgr);

  // 从文件路径检测（内部 cv::imread；读取失败抛 std::runtime_error）。
  [[nodiscard]] std::vector<Detection> Detect(const std::string& image_path);

  // 实际生效的 EP（CUDA 不可用时为 kCpu）。
  [[nodiscard]] w14::Ep ActiveEp() const { return engine_.ActiveEp(); }

 private:
  w14::InferenceEngine engine_;
  DetectorConfig cfg_;
};

}  // namespace w16

#endif  // EDGE_AI_GENESIS_2026_W16_YOLO_DETECTOR_HPP_
