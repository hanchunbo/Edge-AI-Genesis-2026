// SPDX-License-Identifier: MIT
//
// 文件功能：W16 NMS 实现（IoU + 逐类贪心抑制）。

#include "nms.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace w16 {

float IoU(const Detection& a, const Detection& b) {
  // 交集矩形：取两框左上角的较大者、右下角的较小者。
  const float ix1 = std::max(a.x1, b.x1);
  const float iy1 = std::max(a.y1, b.y1);
  const float ix2 = std::min(a.x2, b.x2);
  const float iy2 = std::min(a.y2, b.y2);

  // 无交集时宽或高为负，clamp 到 0。
  const float iw = std::max(0.0f, ix2 - ix1);
  const float ih = std::max(0.0f, iy2 - iy1);
  const float inter = iw * ih;
  if (inter <= 0.0f) {
    return 0.0f;
  }

  const float area_a =
      std::max(0.0f, a.x2 - a.x1) * std::max(0.0f, a.y2 - a.y1);
  const float area_b =
      std::max(0.0f, b.x2 - b.x1) * std::max(0.0f, b.y2 - b.y1);
  const float uni = area_a + area_b - inter;
  if (uni <= 0.0f) {
    return 0.0f;
  }
  return inter / uni;
}

std::vector<Detection> Nms(std::vector<Detection> dets,
                           const NmsOptions& options) {
  if (!std::isfinite(options.iou_thresh) || options.iou_thresh < 0.0f ||
      options.iou_thresh > 1.0f) {
    throw std::invalid_argument("Nms: iou_thresh 必须在 [0,1] 内");
  }
  if (options.max_det < 0) {
    throw std::invalid_argument("Nms: max_det 必须 >= 0");
  }

  // 全局按 score 降序：贪心保留高分框，结果天然有序，便于对拍。
  std::sort(
      dets.begin(), dets.end(),
      [](const Detection& a, const Detection& b) { return a.score > b.score; });

  std::vector<Detection> kept;
  kept.reserve(dets.size());
  std::vector<bool> suppressed(dets.size(), false);

  for (std::size_t i = 0; i < dets.size(); ++i) {
    if (suppressed[i]) {
      continue;
    }
    kept.push_back(dets[i]);
    // 仅抑制「同类别」且与当前框 IoU 超阈值的后续（更低分）框。
    // 逐类语义：不同类别的重叠不互相抑制。
    for (std::size_t j = i + 1; j < dets.size(); ++j) {
      if (suppressed[j] || dets[j].class_id != dets[i].class_id) {
        continue;
      }
      if (IoU(dets[i], dets[j]) > options.iou_thresh) {
        suppressed[j] = true;
      }
    }
  }
  if (options.max_det > 0 &&
      kept.size() > static_cast<std::size_t>(options.max_det)) {
    kept.resize(static_cast<std::size_t>(options.max_det));
  }
  return kept;
}

std::vector<Detection> Nms(std::vector<Detection> dets, float iou_thresh) {
  return Nms(std::move(dets), NmsOptions{.iou_thresh = iou_thresh});
}

}  // namespace w16
