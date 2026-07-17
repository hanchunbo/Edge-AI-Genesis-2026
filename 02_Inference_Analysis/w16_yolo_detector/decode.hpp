// SPDX-License-Identifier: MIT
//
// 文件功能：W16 YOLOv8 检测头解析 —— 转置输出、阈值过滤、letterbox 坐标反算。

#ifndef EDGE_AI_GENESIS_2026_W16_DECODE_HPP_
#define EDGE_AI_GENESIS_2026_W16_DECODE_HPP_

#include "detection.hpp"

#include <span>
#include <vector>

namespace w16 {

/// 解码可选项：置信度阈值 + 部署硬化项（NaN/Inf 跳过、候选预留）。
struct DecodeOptions {
  float conf_thresh = 0.25f;
  bool skip_non_finite = false;
  int reserve_hint = 0;
};

// 解析 YOLOv8 单 batch 输出张量为候选检测框（未做 NMS）。
//
// 输入张量布局（ultralytics 导出的 ONNX 输出 [1, C, A]，行主序展平）：
//   - C = 4 + num_classes（YOLOv8 检测头**无 objectness**，与 v5 不同）
//   - A = num_anchors（如 640 输入对应 8400）
//   - 通道主序：value(ch, a) = out[ch * A + a]
//     前 4 通道 = cx, cy, w, h（letterbox 输入像素坐标，已由模型内置解码）
//     后 num_classes 通道 = 各类别概率（导出时已含 sigmoid）
//
// 处理：逐 anchor 取类别最大概率为 score，score < conf_thresh 丢弃；
//       cxcywh -> xyxy；再做 letterbox 坐标反算回**原图**坐标：
//         orig = (lb_coord - pad) / scale
//
// scale / pad_left / pad_top 即 w10::LetterboxInfo 的三个字段（此处用裸值，
// 让解码核心不依赖 OpenCV/w10，可独立单测——坐标反算是最高 bug 风险点）。
//
// img_w / img_h 为**原图尺寸**：反算后把框 clamp 到 [0,img_w]×[0,img_h]，
// 与 ultralytics 一致——贴边/出界目标的框不会出现负坐标或超图，避免下游画框 /
// 裁 ROI / 算 IoU 出错。
//
// out.size() 必须等于 (4 + num_classes) * num_anchors，否则抛
// std::invalid_argument。
// @throws std::invalid_argument out.size() 与 (4+num_classes)*num_anchors
//         不符，或 num_classes/num_anchors/img_w/img_h 非正、reserve_hint
//         为负、conf_thresh 越界、scale 非正有限数
[[nodiscard]] std::vector<Detection> DecodeYolov8(
    std::span<const float> out, int num_classes, int num_anchors,
    const DecodeOptions& options, float scale, int pad_left, int pad_top,
    int img_w, int img_h);

// 旧入口保留给 W16 历史调用方；内部委托到 DecodeOptions 版本。
[[nodiscard]] std::vector<Detection> DecodeYolov8(
    std::span<const float> out, int num_classes, int num_anchors,
    float conf_thresh, float scale, int pad_left, int pad_top, int img_w,
    int img_h);

}  // namespace w16

#endif  // EDGE_AI_GENESIS_2026_W16_DECODE_HPP_
