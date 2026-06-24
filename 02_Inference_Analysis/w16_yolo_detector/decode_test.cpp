// SPDX-License-Identifier: MIT
//
// 文件功能：W16 YOLOv8 解码 + 坐标反算单元测试（合成张量，无需模型）。

#include "decode.hpp"

#include <gtest/gtest.h>
#include <vector>

namespace {

// 按通道主序 [C, A] 写入合成张量：out[ch * A + a] = v。
void Set(std::vector<float>& out, int num_anchors, int ch, int a, float v) {
  out[ch * num_anchors + a] = v;
}

// 构造 num_classes 类、num_anchors 锚的零张量。
std::vector<float> MakeTensor(int num_classes, int num_anchors) {
  return std::vector<float>(
      static_cast<std::size_t>(4 + num_classes) * num_anchors, 0.0f);
}

TEST(W16Decode, ParsesBoxesAndPicksMaxClass) {
  const int nc = 3, na = 2;
  std::vector<float> out = MakeTensor(nc, na);
  // anchor0：中心 (10,10) 宽高 4x4，类别概率 [0.1,0.9,0.2] -> class 1 / 0.9
  Set(out, na, 0, 0, 10);
  Set(out, na, 1, 0, 10);
  Set(out, na, 2, 0, 4);
  Set(out, na, 3, 0, 4);
  Set(out, na, 4, 0, 0.1f);
  Set(out, na, 5, 0, 0.9f);
  Set(out, na, 6, 0, 0.2f);
  // anchor1：中心 (100,100) 宽高 20x20，类别 [0.05,0.1,0.8] -> class 2 / 0.8
  Set(out, na, 0, 1, 100);
  Set(out, na, 1, 1, 100);
  Set(out, na, 2, 1, 20);
  Set(out, na, 3, 1, 20);
  Set(out, na, 4, 1, 0.05f);
  Set(out, na, 5, 1, 0.1f);
  Set(out, na, 6, 1, 0.8f);

  // scale=1 / pad=0：坐标反算为恒等，专测解码本身。
  std::vector<w16::Detection> dets =
      w16::DecodeYolov8(out, nc, na, 0.25f, 1.0f, 0, 0, 10000, 10000);
  ASSERT_EQ(dets.size(), 2u);

  EXPECT_EQ(dets[0].class_id, 1);
  EXPECT_FLOAT_EQ(dets[0].score, 0.9f);
  EXPECT_FLOAT_EQ(dets[0].x1, 8);  // 10 - 4/2
  EXPECT_FLOAT_EQ(dets[0].y1, 8);
  EXPECT_FLOAT_EQ(dets[0].x2, 12);  // 10 + 4/2
  EXPECT_FLOAT_EQ(dets[0].y2, 12);

  EXPECT_EQ(dets[1].class_id, 2);
  EXPECT_FLOAT_EQ(dets[1].score, 0.8f);
  EXPECT_FLOAT_EQ(dets[1].x1, 90);
  EXPECT_FLOAT_EQ(dets[1].x2, 110);
}

TEST(W16Decode, DropsBelowConfThreshold) {
  const int nc = 3, na = 1;
  std::vector<float> out = MakeTensor(nc, na);
  Set(out, na, 0, 0, 50);
  Set(out, na, 1, 0, 50);
  Set(out, na, 2, 0, 10);
  Set(out, na, 3, 0, 10);
  Set(out, na, 4, 0, 0.1f);  // 最高 0.1 < 0.25
  Set(out, na, 5, 0, 0.05f);
  Set(out, na, 6, 0, 0.2f);

  std::vector<w16::Detection> dets =
      w16::DecodeYolov8(out, nc, na, 0.25f, 1.0f, 0, 0, 10000, 10000);
  EXPECT_TRUE(dets.empty());
}

// 一等成功指标：letterbox 坐标反算精确（误差 < 1px）。
TEST(W16Decode, LetterboxCoordRemapWithinOnePixel) {
  const int nc = 1, na = 1;
  std::vector<float> out = MakeTensor(nc, na);
  // letterbox 坐标系：中心 (120,110) 宽高 40x20 -> x1,y1,x2,y2 =
  // 100,100,140,120
  Set(out, na, 0, 0, 120);
  Set(out, na, 1, 0, 110);
  Set(out, na, 2, 0, 40);
  Set(out, na, 3, 0, 20);
  Set(out, na, 4, 0, 0.9f);

  // scale=0.5, pad_left=20, pad_top=10 -> orig = (lb - pad) / scale
  std::vector<w16::Detection> dets =
      w16::DecodeYolov8(out, nc, na, 0.25f, 0.5f, 20, 10, 10000, 10000);
  ASSERT_EQ(dets.size(), 1u);
  EXPECT_NEAR(dets[0].x1, (100 - 20) / 0.5f, 1.0f);  // 160
  EXPECT_NEAR(dets[0].y1, (100 - 10) / 0.5f, 1.0f);  // 180
  EXPECT_NEAR(dets[0].x2, (140 - 20) / 0.5f, 1.0f);  // 240
  EXPECT_NEAR(dets[0].y2, (120 - 10) / 0.5f, 1.0f);  // 220
}

// 框反算后超出原图边界要被 clamp 到 [0,img_w]×[0,img_h]（与 ultralytics
// 一致）。
TEST(W16Decode, ClampsBoxToImageBounds) {
  const int nc = 1, na = 1;
  std::vector<float> out = MakeTensor(nc, na);
  // 中心 (50,50) 宽高 200x200 -> xyxy = -50,-50,150,150（左上出界）
  Set(out, na, 0, 0, 50);
  Set(out, na, 1, 0, 50);
  Set(out, na, 2, 0, 200);
  Set(out, na, 3, 0, 200);
  Set(out, na, 4, 0, 0.9f);

  // scale=1/pad=0，原图 100x100：左上 clamp 到 0，右下 clamp 到 100。
  std::vector<w16::Detection> dets =
      w16::DecodeYolov8(out, nc, na, 0.25f, 1.0f, 0, 0, 100, 100);
  ASSERT_EQ(dets.size(), 1u);
  EXPECT_FLOAT_EQ(dets[0].x1, 0.0f);
  EXPECT_FLOAT_EQ(dets[0].y1, 0.0f);
  EXPECT_FLOAT_EQ(dets[0].x2, 100.0f);
  EXPECT_FLOAT_EQ(dets[0].y2, 100.0f);
}

TEST(W16Decode, ThrowsOnWrongTensorSize) {
  std::vector<float> out(10, 0.0f);  // 不等于 (4+3)*2
  EXPECT_THROW(w16::DecodeYolov8(out, 3, 2, 0.25f, 1.0f, 0, 0, 100, 100),
               std::invalid_argument);
}

}  // namespace
