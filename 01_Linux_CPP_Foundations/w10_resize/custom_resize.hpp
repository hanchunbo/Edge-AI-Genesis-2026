// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：自定义图像 Resize 算子接口声明
//           V1 最近邻 / V2 双线性（定点加速）/ V3 Letterbox（YOLO 预处理）
// ============================================================================

#ifndef LINUX_CPP_FOUNDATIONS_W10_RESIZE_CUSTOM_RESIZE_HPP_
#define LINUX_CPP_FOUNDATIONS_W10_RESIZE_CUSTOM_RESIZE_HPP_

#include <opencv2/core.hpp>
#include <stdexcept>
#include <vector>

namespace w10 {

// 坐标对齐模式：影响输入像素坐标到输出像素坐标的映射公式
// - Asymmetric：src_x = dst_x * (src_w / dst_w)
//   ONNX Runtime 默认，边角像素严格对齐，适合大多数分类/检测模型
// - HalfPixel：src_x = (dst_x + 0.5) * (src_w / dst_w) - 0.5
//   TensorRT / PyTorch 默认，中心对齐，缩放后边缘更平滑
enum class CoordMode {
  kAsymmetric,  // ONNX 默认
  kHalfPixel,   // TensorRT/PyTorch 默认
};

// Letterbox 输出信息：调用方需要此信息才能把检测框映射回原始坐标
struct LetterboxInfo {
  float scale;   // 等比缩放比例（= min(dst_w/src_w, dst_h/src_h)）
  int pad_left;  // 左侧填充像素数
  int pad_top;   // 顶部填充像素数
};

// ============================================================================
// V1：最近邻插值（Nearest Neighbor）
// ----------------------------------------------------------------------------
// 速度最快，质量最低；常用于语义分割上采样（不引入新灰度值，保留类别 ID）。
// coord_mode 决定坐标映射策略（见 CoordMode 注释）。
// 异常：src 为空或类型非 CV_8UC3 时抛 std::invalid_argument。
// ============================================================================
[[nodiscard]] cv::Mat ResizeNearest(
    const cv::Mat& src, int dst_w, int dst_h,
    CoordMode coord_mode = CoordMode::kAsymmetric);

// ============================================================================
// V2：双线性插值（Bilinear Interpolation，定点加速版）
// ----------------------------------------------------------------------------
// 权重用 Q8 定点（乘以 256 取整）代替 float，在 int32 中累加后右移 16 位还原，
// 避免每像素 4 次 float 乘法，适合没有 FPU 的嵌入式核心。
// coord_mode 决定坐标映射策略（见 CoordMode 注释）。
// 异常：src 为空或类型非 CV_8UC3 时抛 std::invalid_argument。
// ============================================================================
[[nodiscard]] cv::Mat ResizeBilinear(
    const cv::Mat& src, int dst_w, int dst_h,
    CoordMode coord_mode = CoordMode::kAsymmetric);

// ============================================================================
// V3：Letterbox 缩放（YOLO 系列标准预处理）
// ----------------------------------------------------------------------------
// 等比缩放后四周填充灰色边框（默认 (114,114,114)），保证长宽比不变。
// 返回 dst_h×dst_w 的 BGR uint8 Mat 和 LetterboxInfo（供后处理坐标反算）。
// 异常：src 为空或类型非 CV_8UC3 时抛 std::invalid_argument。
// ============================================================================
[[nodiscard]] cv::Mat Letterbox(const cv::Mat& src, int dst_w, int dst_h,
                                LetterboxInfo& info,
                                cv::Scalar pad_color = cv::Scalar(114, 114,
                                                                  114));

// ============================================================================
// V4：Letterbox + 归一化 + CHW 转置（推理引擎直连版）
// ----------------------------------------------------------------------------
// 在 V3 基础上，将 BGR uint8 输出转换为推理引擎所需格式：
//   uint8 BGR HWC  →  ÷255  →  float32 [0,1]  →  CHW (channel-first)
// 输出 vector 尺寸 = 3 * dst_h * dst_w，layout：
//   [B0..B_n, G0..G_n, R0..R_n]（与 ONNX Runtime / TensorRT {1,3,H,W} 对齐）
// info 同 V3，用于检测框坐标反算。
// 异常：src 为空或类型非 CV_8UC3 时抛 std::invalid_argument。
// ============================================================================
[[nodiscard]] std::vector<float> LetterboxToTensor(
    const cv::Mat& src, int dst_w, int dst_h, LetterboxInfo& info,
    cv::Scalar pad_color = cv::Scalar(114, 114, 114));

}  // namespace w10

#endif  // LINUX_CPP_FOUNDATIONS_W10_RESIZE_CUSTOM_RESIZE_HPP_
