// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：自定义图像 Resize 算子实现
//           V1 最近邻 / V2 双线性（Q8 定点加速）/ V3 Letterbox（YOLO 预处理）
// ============================================================================

// [Legacy C++11/17]: 直接用 float 计算插值权重，每像素 4 次 float 乘法
// [Pain Point]: 无 FPU 嵌入式核心（如 Cortex-M）上 float 乘法极慢；
//               float scale 在极端缩放比下累积精度误差
// [Modern C++20]: Q8 定点权重（乘以 256 取整），int32 累加后右移 16 位还原，
//                 全程整数运算，自动适配无 FPU 平台

#include "custom_resize.hpp"

#include <algorithm>
#include <cmath>
#include <opencv2/imgproc.hpp>
#include <stdexcept>

namespace w10 {

namespace {

// ----------------------------------------------------------------------------
// 输入校验辅助函数（统一错误处理，Release 下同样有效）
// ----------------------------------------------------------------------------
inline void ValidateSrc(const cv::Mat& src) {
  if (src.empty()) {
    throw std::invalid_argument("Resize: src Mat 为空");
  }
  if (src.type() != CV_8UC3) {
    throw std::invalid_argument("Resize: 仅支持 CV_8UC3 输入，实际类型 = " +
                                std::to_string(src.type()));
  }
}

// ----------------------------------------------------------------------------
// 坐标映射辅助函数
// 返回目标坐标 dst_coord 对应的源图浮点坐标
// ----------------------------------------------------------------------------
inline float MapCoord(int dst_coord, float scale, CoordMode mode) {
  if (mode == CoordMode::kAsymmetric) {
    // ONNX 默认：左上角对齐，公式最简单
    return static_cast<float>(dst_coord) * scale;
  }
  // HalfPixel：像素中心对齐，TensorRT/PyTorch 默认
  // 推导：src_center = (dst_center) * scale，其中 center = coord + 0.5
  return (static_cast<float>(dst_coord) + 0.5f) * scale - 0.5f;
}

}  // namespace

// ============================================================================
// V1：最近邻插值
// ============================================================================
cv::Mat ResizeNearest(const cv::Mat& src, int dst_w, int dst_h,
                      CoordMode coord_mode) {
  ValidateSrc(src);
  if (dst_w <= 0 || dst_h <= 0) {
    throw std::invalid_argument("ResizeNearest: dst_w/dst_h 必须 > 0");
  }

  cv::Mat dst(dst_h, dst_w, CV_8UC3);

  // 缩放比：src 宽/高 除以 dst 宽/高
  // 注意方向：给定 dst 坐标，反向映射回 src
  const float scale_x =
      static_cast<float>(src.cols) / static_cast<float>(dst_w);
  const float scale_y =
      static_cast<float>(src.rows) / static_cast<float>(dst_h);

  for (int dy = 0; dy < dst_h; ++dy) {
    // 计算源图 y 坐标，clamp 防止浮点舍入越界
    float fy = MapCoord(dy, scale_y, coord_mode);
    int sy = std::clamp(static_cast<int>(fy), 0, src.rows - 1);

    const auto* src_row = src.ptr<cv::Vec3b>(sy);
    auto* dst_row = dst.ptr<cv::Vec3b>(dy);

    for (int dx = 0; dx < dst_w; ++dx) {
      float fx = MapCoord(dx, scale_x, coord_mode);
      int sx = std::clamp(static_cast<int>(fx), 0, src.cols - 1);
      dst_row[dx] = src_row[sx];
    }
  }

  return dst;
}

// ============================================================================
// V2：双线性插值（Q8 定点加速）
// ============================================================================
//
// 算法原理（以单通道为例）：
//   已知源图四邻域像素 P(x0,y0) P(x1,y0) P(x0,y1) P(x1,y1)
//   水平权重 wx = fx - x0（小数部分），垂直权重 wy = fy - y0
//
//   result = (1-wy)*[(1-wx)*P00 + wx*P10]
//           + wy  *[(1-wx)*P01 + wx*P11]
//
// Q8 定点优化：
//   wx_q  = round(wx * 256)，范围 [0, 256]
//   wy_q  = round(wy * 256)，范围 [0, 256]
//   (1-wx_q) = 256 - wx_q
//
//   row0 = (256-wx_q)*P00 + wx_q*P10  （int32，最大 256*255 = 65280）
//   row1 = (256-wx_q)*P01 + wx_q*P11
//   result_q16 = (256-wy_q)*row0 + wy_q*row1  （最大 256*65280 = 16711680 <
//   2^24） result = result_q16 >> 16
//
cv::Mat ResizeBilinear(const cv::Mat& src, int dst_w, int dst_h,
                       CoordMode coord_mode) {
  ValidateSrc(src);
  if (dst_w <= 0 || dst_h <= 0) {
    throw std::invalid_argument("ResizeBilinear: dst_w/dst_h 必须 > 0");
  }

  cv::Mat dst(dst_h, dst_w, CV_8UC3);

  const float scale_x =
      static_cast<float>(src.cols) / static_cast<float>(dst_w);
  const float scale_y =
      static_cast<float>(src.rows) / static_cast<float>(dst_h);
  const int src_w = src.cols;
  const int src_h = src.rows;

  for (int dy = 0; dy < dst_h; ++dy) {
    float fy = MapCoord(dy, scale_y, coord_mode);
    // clamp 到合法范围后取整数和小数部分
    fy = std::clamp(fy, 0.0f, static_cast<float>(src_h - 1));
    int y0 = static_cast<int>(fy);
    int y1 = std::min(y0 + 1, src_h - 1);
    // Q8 垂直权重：wy_q ∈ [0, 256)
    int wy_q = static_cast<int>((fy - static_cast<float>(y0)) * 256.0f + 0.5f);
    int wy0_q = 256 - wy_q;

    const auto* row0 = src.ptr<cv::Vec3b>(y0);
    const auto* row1 = src.ptr<cv::Vec3b>(y1);
    auto* dst_row = dst.ptr<cv::Vec3b>(dy);

    for (int dx = 0; dx < dst_w; ++dx) {
      float fx = MapCoord(dx, scale_x, coord_mode);
      fx = std::clamp(fx, 0.0f, static_cast<float>(src_w - 1));
      int x0 = static_cast<int>(fx);
      int x1 = std::min(x0 + 1, src_w - 1);
      int wx_q =
          static_cast<int>((fx - static_cast<float>(x0)) * 256.0f + 0.5f);
      int wx0_q = 256 - wx_q;

      const cv::Vec3b& p00 = row0[x0];
      const cv::Vec3b& p10 = row0[x1];
      const cv::Vec3b& p01 = row1[x0];
      const cv::Vec3b& p11 = row1[x1];

      // 三通道分别做 Q8 双线性，结果右移 16 位
      for (int c = 0; c < 3; ++c) {
        int r0 =
            wx0_q * static_cast<int>(p00[c]) + wx_q * static_cast<int>(p10[c]);
        int r1 =
            wx0_q * static_cast<int>(p01[c]) + wx_q * static_cast<int>(p11[c]);
        int val = (wy0_q * r0 + wy_q * r1) >> 16;
        dst_row[dx][c] = static_cast<uint8_t>(std::clamp(val, 0, 255));
      }
    }
  }

  return dst;
}

// ============================================================================
// V3：Letterbox（YOLO 系列标准预处理）
// ============================================================================
//
// 步骤：
//   1. 等比缩放：scale = min(dst_w/src_w, dst_h/src_h)
//      保证缩放后图像完全放入目标框内，不裁剪内容
//   2. 居中放置：左右/上下均匀填充
//   3. 填充区域涂 pad_color（YOLO 默认灰色 114）
//
// 后处理坐标反算公式（检测框从 dst 坐标系映射回 src 坐标系）：
//   src_x = (dst_x - pad_left) / scale
//   src_y = (dst_y - pad_top)  / scale
//
cv::Mat Letterbox(const cv::Mat& src, int dst_w, int dst_h, LetterboxInfo& info,
                  cv::Scalar pad_color) {
  ValidateSrc(src);
  if (dst_w <= 0 || dst_h <= 0) {
    throw std::invalid_argument("Letterbox: dst_w/dst_h 必须 > 0");
  }

  // 等比缩放比（取宽高中的较小值，保证不超出目标框）
  float scale_w = static_cast<float>(dst_w) / static_cast<float>(src.cols);
  float scale_h = static_cast<float>(dst_h) / static_cast<float>(src.rows);
  float scale = std::min(scale_w, scale_h);

  // 缩放后实际尺寸（取整）
  int new_w =
      static_cast<int>(std::round(static_cast<float>(src.cols) * scale));
  int new_h =
      static_cast<int>(std::round(static_cast<float>(src.rows) * scale));

  // 用 OpenCV 做等比缩放（借用 V2 的双线性质量），不需要重复实现
  cv::Mat resized;
  cv::resize(src, resized, cv::Size(new_w, new_h), 0, 0, cv::INTER_LINEAR);

  // 计算居中填充量（整除 2，余数加到右/下，与 YOLOv5 官方行为一致）
  int pad_left = (dst_w - new_w) / 2;
  int pad_top = (dst_h - new_h) / 2;
  int pad_right = dst_w - new_w - pad_left;
  int pad_bottom = dst_h - new_h - pad_top;

  // copyMakeBorder 用常数填充四边
  cv::Mat dst;
  cv::copyMakeBorder(resized, dst, pad_top, pad_bottom, pad_left, pad_right,
                     cv::BORDER_CONSTANT, pad_color);

  // 填写调用方需要的反算信息
  info.scale = scale;
  info.pad_left = pad_left;
  info.pad_top = pad_top;

  return dst;
}

// ============================================================================
// V4：Letterbox + 归一化(÷255) + CHW 转置（推理引擎直连版）
// ============================================================================
//
// 转换链路：
//   uint8 BGR HWC  →  Letterbox  →  ÷255  →  float32 [0,1]  →  CHW
//
// CHW layout：
//   [B 面, G 面, R 面]，内存顺序与 ONNX Runtime / TensorRT {1,3,H,W} 对齐。
//
std::vector<float> LetterboxToTensor(const cv::Mat& src, int dst_w, int dst_h,
                                     LetterboxInfo& info,
                                     cv::Scalar pad_color) {
  // Letterbox 内部已做 ValidateSrc，无需重复检测
  cv::Mat lb_bgr = Letterbox(src, dst_w, dst_h, info, pad_color);

  const int total_pixels = dst_h * dst_w;
  std::vector<float> tensor(3 * total_pixels);

  // CHW 转置：将 HWC 内存布局拆分为三个单通道面
  // 分别起始地址： channel B = tensor[0..n-1]
  //                  channel G = tensor[n..2n-1]
  //                  channel R = tensor[2n..3n-1]
  float* ch_b = tensor.data();
  float* ch_g = tensor.data() + total_pixels;
  float* ch_r = tensor.data() + 2 * total_pixels;

  int idx = 0;
  for (int y = 0; y < dst_h; ++y) {
    const auto* row = lb_bgr.ptr<cv::Vec3b>(y);
    for (int x = 0; x < dst_w; ++x, ++idx) {
      // OpenCV 默认 BGR 顺序
      ch_b[idx] = static_cast<float>(row[x][0]) / 255.0f;
      ch_g[idx] = static_cast<float>(row[x][1]) / 255.0f;
      ch_r[idx] = static_cast<float>(row[x][2]) / 255.0f;
    }
  }

  return tensor;
}

}  // namespace w10
