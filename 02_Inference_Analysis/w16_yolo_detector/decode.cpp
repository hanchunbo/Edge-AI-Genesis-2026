// SPDX-License-Identifier: MIT
//
// 文件功能：W16 YOLOv8 检测头解析实现 —— 转置遍历、阈值过滤、坐标反算。

#include "decode.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <stdexcept>

namespace w16 {

std::vector<Detection> DecodeYolov8(std::span<const float> out, int num_classes,
                                    int num_anchors,
                                    const DecodeOptions& options, float scale,
                                    int pad_left, int pad_top, int img_w,
                                    int img_h) {
  if (num_classes <= 0) {
    throw std::invalid_argument("DecodeYolov8: num_classes 必须 > 0");
  }
  if (num_anchors <= 0) {
    throw std::invalid_argument("DecodeYolov8: num_anchors 必须 > 0");
  }
  if (!std::isfinite(options.conf_thresh) || options.conf_thresh < 0.0f ||
      options.conf_thresh > 1.0f) {
    throw std::invalid_argument("DecodeYolov8: conf_thresh 必须在 [0,1] 内");
  }
  if (options.reserve_hint < 0) {
    throw std::invalid_argument("DecodeYolov8: reserve_hint 必须 >= 0");
  }
  if (!std::isfinite(scale) || scale <= 0.0f) {
    throw std::invalid_argument("DecodeYolov8: scale 必须是正有限数");
  }
  if (img_w <= 0 || img_h <= 0) {
    throw std::invalid_argument("DecodeYolov8: img_w/img_h 必须 > 0");
  }

  const std::size_t expected =
      static_cast<std::size_t>(4 + num_classes) * num_anchors;
  if (out.size() != expected) {
    throw std::invalid_argument(
        std::format("DecodeYolov8: 张量大小 {} 与期望 (4+{})*{}={} 不符",
                    out.size(), num_classes, num_anchors, expected));
  }

  const int a_count = num_anchors;
  std::vector<Detection> dets;
  if (options.reserve_hint > 0) {
    dets.reserve(
        static_cast<std::size_t>(std::min(options.reserve_hint, num_anchors)));
  }

  // 通道主序遍历：value(ch, a) = out[ch * num_anchors + a]。
  // 内层对每个 anchor 扫一遍类别通道，挑最大概率。
  for (int a = 0; a < a_count; ++a) {
    int best_cls = 0;
    float best_score = out[(4 + 0) * a_count + a];
    bool has_non_finite = !std::isfinite(best_score);
    for (int c = 1; c < num_classes; ++c) {
      const float s = out[(4 + c) * a_count + a];
      if (!std::isfinite(s)) {
        has_non_finite = true;
      }
      if (s > best_score) {
        best_score = s;
        best_cls = c;
      }
    }
    if (best_score < options.conf_thresh) {
      continue;
    }

    // 前 4 通道：cx, cy, w, h（letterbox 输入像素坐标）。
    const float cx = out[0 * a_count + a];
    const float cy = out[1 * a_count + a];
    const float w = out[2 * a_count + a];
    const float h = out[3 * a_count + a];
    if (options.skip_non_finite &&
        (has_non_finite || !std::isfinite(cx) || !std::isfinite(cy) ||
         !std::isfinite(w) || !std::isfinite(h))) {
      continue;
    }

    // cxcywh -> xyxy（仍是 letterbox 坐标）。
    const float lx1 = cx - w * 0.5f;
    const float ly1 = cy - h * 0.5f;
    const float lx2 = cx + w * 0.5f;
    const float ly2 = cy + h * 0.5f;

    // letterbox 坐标反算回原图：orig = (lb - pad) / scale。
    const float inv = 1.0f / scale;
    const float fw = static_cast<float>(img_w);
    const float fh = static_cast<float>(img_h);
    // 反算后 clamp 到原图边界，避免贴边/出界目标产生负坐标或超图框。
    dets.push_back(Detection{std::clamp((lx1 - pad_left) * inv, 0.0f, fw),
                             std::clamp((ly1 - pad_top) * inv, 0.0f, fh),
                             std::clamp((lx2 - pad_left) * inv, 0.0f, fw),
                             std::clamp((ly2 - pad_top) * inv, 0.0f, fh),
                             best_score, best_cls});
  }
  return dets;
}

std::vector<Detection> DecodeYolov8(std::span<const float> out, int num_classes,
                                    int num_anchors, float conf_thresh,
                                    float scale, int pad_left, int pad_top,
                                    int img_w, int img_h) {
  return DecodeYolov8(out, num_classes, num_anchors,
                      DecodeOptions{.conf_thresh = conf_thresh}, scale,
                      pad_left, pad_top, img_w, img_h);
}

}  // namespace w16
