// SPDX-License-Identifier: MIT
//
// 文件功能：W16 YOLODetector 实现 —— 详见 yolo_detector.hpp。

#include "yolo_detector.hpp"

#include "custom_resize.hpp"  // w10::LetterboxToTensor / LetterboxInfo
#include "decode.hpp"
#include "nms.hpp"

#include <cstdint>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <stdexcept>

namespace w16 {

YOLODetector::YOLODetector(const std::string& model_path, DetectorConfig cfg)
    : engine_(model_path, cfg.ep), cfg_(cfg) {}

std::vector<Detection> YOLODetector::Detect(const cv::Mat& bgr) {
  if (bgr.empty()) {
    throw std::runtime_error("[W16] 输入图像为空");
  }

  // 1) BGR→RGB：YOLOv8 按 RGB 训练，而 OpenCV 解码为 BGR。
  //    w10::LetterboxToTensor 仅做 /255+CHW（无 mean/std，正合 YOLOv8），
  //    但它按输入通道顺序展平，故必须先转 RGB 再喂入。
  cv::Mat rgb;
  cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);

  // 2) Letterbox + 归一化 + CHW（复用 Q1 W10 成果，自带坐标反算信息）。
  w10::LetterboxInfo info{};
  const std::vector<float> input =
      w10::LetterboxToTensor(rgb, cfg_.input_size, cfg_.input_size, info);
  const std::vector<int64_t> shape{1, 3, cfg_.input_size, cfg_.input_size};

  // 3) 推理（零拷贝喂入）。
  std::vector<Ort::Value> outputs = engine_.Run(input, shape);
  const float* out_data = outputs[0].GetTensorData<float>();
  const std::vector<int64_t> out_shape =
      outputs[0].GetTensorTypeAndShapeInfo().GetShape();
  // 期望 [1, C, A]：C=4+num_classes，A=num_anchors。
  if (out_shape.size() != 3 || out_shape[0] != 1) {
    throw std::runtime_error("[W16] 非预期的 YOLOv8 输出形状（应为 [1,C,A]）");
  }
  const int num_channels = static_cast<int>(out_shape[1]);
  const int num_anchors = static_cast<int>(out_shape[2]);
  const int num_classes = num_channels - 4;
  const std::size_t count =
      static_cast<std::size_t>(num_channels) * num_anchors;

  // 4) 解码（含坐标反算回原图）+ 5) 逐类 NMS。
  std::vector<Detection> dets =
      DecodeYolov8(std::span<const float>(out_data, count), num_classes,
                   num_anchors, cfg_.conf_thresh, info.scale, info.pad_left,
                   info.pad_top, bgr.cols, bgr.rows);
  return Nms(std::move(dets), cfg_.iou_thresh);
}

std::vector<Detection> YOLODetector::Detect(const std::string& image_path) {
  const cv::Mat bgr = cv::imread(image_path, cv::IMREAD_COLOR);
  if (bgr.empty()) {
    throw std::runtime_error("[W16] 图片读取失败: " + image_path);
  }
  return Detect(bgr);
}

}  // namespace w16
