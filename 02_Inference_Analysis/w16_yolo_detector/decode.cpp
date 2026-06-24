// SPDX-License-Identifier: MIT
//
// 文件功能：W16 YOLOv8 检测头解析实现 —— 转置遍历、阈值过滤、坐标反算。

#include "decode.hpp"

#include <cstddef>
#include <format>
#include <stdexcept>

namespace w16 {

std::vector<Detection> DecodeYolov8(std::span<const float> out, int num_classes,
                                    int num_anchors, float conf_thresh,
                                    float scale, int pad_left, int pad_top) {
  const std::size_t expected =
      static_cast<std::size_t>(4 + num_classes) * num_anchors;
  if (out.size() != expected) {
    throw std::invalid_argument(
        std::format("DecodeYolov8: 张量大小 {} 与期望 (4+{})*{}={} 不符",
                    out.size(), num_classes, num_anchors, expected));
  }

  const int a_count = num_anchors;
  std::vector<Detection> dets;

  // 通道主序遍历：value(ch, a) = out[ch * num_anchors + a]。
  // 内层对每个 anchor 扫一遍类别通道，挑最大概率。
  for (int a = 0; a < a_count; ++a) {
    int best_cls = 0;
    float best_score = out[(4 + 0) * a_count + a];
    for (int c = 1; c < num_classes; ++c) {
      const float s = out[(4 + c) * a_count + a];
      if (s > best_score) {
        best_score = s;
        best_cls = c;
      }
    }
    if (best_score < conf_thresh) {
      continue;
    }

    // 前 4 通道：cx, cy, w, h（letterbox 输入像素坐标）。
    const float cx = out[0 * a_count + a];
    const float cy = out[1 * a_count + a];
    const float w = out[2 * a_count + a];
    const float h = out[3 * a_count + a];

    // cxcywh -> xyxy（仍是 letterbox 坐标）。
    const float lx1 = cx - w * 0.5f;
    const float ly1 = cy - h * 0.5f;
    const float lx2 = cx + w * 0.5f;
    const float ly2 = cy + h * 0.5f;

    // letterbox 坐标反算回原图：orig = (lb - pad) / scale。
    const float inv = 1.0f / scale;
    dets.push_back(Detection{(lx1 - pad_left) * inv, (ly1 - pad_top) * inv,
                             (lx2 - pad_left) * inv, (ly2 - pad_top) * inv,
                             best_score, best_cls});
  }
  return dets;
}

}  // namespace w16
