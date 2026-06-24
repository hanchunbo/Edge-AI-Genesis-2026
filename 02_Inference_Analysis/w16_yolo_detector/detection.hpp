// SPDX-License-Identifier: MIT
//
// 文件功能：W16 检测结果结构体 —— NMS 与解码模块共享的最小数据单元。

#ifndef EDGE_AI_GENESIS_2026_W16_DETECTION_HPP_
#define EDGE_AI_GENESIS_2026_W16_DETECTION_HPP_

namespace w16 {

// 单个检测框。坐标语义由生产方约定：解码阶段先产出 letterbox 像素坐标，
// 坐标反算后变为原图像素坐标（左上 x1,y1 / 右下 x2,y2）。
struct Detection {
  float x1;      // 左上角 x
  float y1;      // 左上角 y
  float x2;      // 右下角 x
  float y2;      // 右下角 y
  float score;   // 置信度（YOLOv8 取 80 类概率最大值）
  int class_id;  // 类别索引
};

}  // namespace w16

#endif  // EDGE_AI_GENESIS_2026_W16_DETECTION_HPP_
