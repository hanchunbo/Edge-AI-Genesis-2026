// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：BGR→Gray 三版实现的函数声明
// ============================================================================
#ifndef W9_BGR2GRAY_HPP_
#define W9_BGR2GRAY_HPP_

#include <opencv2/core.hpp>

namespace w9 {

/// @brief 版本1：双层循环 + .ptr<T>()，演示 step/stride 内存布局
/// @param src CV_8UC3 格式 BGR 图像
/// @return CV_8UC1 灰度图
[[nodiscard]] cv::Mat BgrToGrayV1(const cv::Mat& src);

/// @brief 版本2：isContinuous() 单层展开，连续内存优化
/// @param src CV_8UC3 格式 BGR 图像（非连续时自动降级到 V1）
[[nodiscard]] cv::Mat BgrToGrayV2(const cv::Mat& src);

/// @brief 版本3：std::mdspan (C++23) 多维视图封装
/// @param src CV_8UC3 格式 BGR 图像（非连续时自动降级到 V1）
[[nodiscard]] cv::Mat BgrToGrayV3(const cv::Mat& src);

}  // namespace w9

#endif  // W9_BGR2GRAY_HPP_
