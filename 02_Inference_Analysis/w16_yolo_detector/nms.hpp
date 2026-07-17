// SPDX-License-Identifier: MIT
//
// 文件功能：W16 非极大值抑制（NMS）—— IoU 计算 + 逐类贪心抑制。

#ifndef EDGE_AI_GENESIS_2026_W16_NMS_HPP_
#define EDGE_AI_GENESIS_2026_W16_NMS_HPP_

#include "detection.hpp"

#include <vector>

namespace w16 {

struct NmsOptions {
  float iou_thresh = 0.45f;
  int max_det = 0;  // 0 表示不限制，保持 W16 历史行为。
};

// 两个轴对齐框的 IoU（交并比）。任一框面积为 0 或无交集时返回 0。
[[nodiscard]] float IoU(const Detection& a, const Detection& b);

// 逐类 NMS：仅在「同 class_id」内部互相抑制（不同类别的重叠框互不影响，
// 例如同一位置可同时是「人」和「背包」）。
//
// 算法：按类别分组 -> 组内按 score 降序 -> 贪心保留最高分、抑制与之
// IoU>阈值的框。 返回保留下来的检测框，按 score
// 全局降序排列（结果确定，便于对拍）。
//
// 入参按值传递：内部需排序，复制一份避免改动调用方数据。
// @throws std::invalid_argument iou_thresh 越界或 max_det < 0
[[nodiscard]] std::vector<Detection> Nms(std::vector<Detection> dets,
                                         const NmsOptions& options);

// 旧入口保留给 W16 历史调用方；内部委托到 NmsOptions 版本。
[[nodiscard]] std::vector<Detection> Nms(std::vector<Detection> dets,
                                         float iou_thresh);

}  // namespace w16

#endif  // EDGE_AI_GENESIS_2026_W16_NMS_HPP_
