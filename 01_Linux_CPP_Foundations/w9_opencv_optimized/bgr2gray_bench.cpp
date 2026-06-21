// SPDX-License-Identifier: MIT
//
// 文件功能：BGR→Gray 多版实现 benchmark（main 入口）
//   对比 V1/V2/V3/V4(AVX2) 与 cv::cvtColor 在 1080P 图像上的耗时

#include "bgr2gray.hpp"

#include <chrono>
#include <format>
#include <iostream>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

int main() {
  constexpr int kHeight = 1080;
  constexpr int kWidth = 1920;
  constexpr int kRounds = 100;

  // 合成测试图像（填充固定值以确保结果可验证）
  cv::Mat test_img(kHeight, kWidth, CV_8UC3);
  test_img.setTo(cv::Scalar(100, 150, 200));

  auto bench = [&](const char* name, auto fn) {
    auto t0 = std::chrono::steady_clock::now();
    cv::Mat result;
    for (int i = 0; i < kRounds; ++i) {
      result = fn(test_img);
    }
    auto t1 = std::chrono::steady_clock::now();
    double ms =
        std::chrono::duration<double, std::milli>(t1 - t0).count() / kRounds;
    // 输出中心像素灰度值用于交叉验证
    uint8_t center = result.at<uint8_t>(kHeight / 2, kWidth / 2);
    std::cout << std::format("[{:<14}] {:6.3f} ms/frame  center_gray={}\n",
                             name, ms, center);
    return result;
  };

  std::cout << std::format("=== W9 BGR→Gray Benchmark ({}×{}, {} rounds) ===\n",
                           kWidth, kHeight, kRounds);

  // 各版本均有 void 重载，需显式指定返回 cv::Mat 的函数指针类型以消除歧义
  using FnPtr = cv::Mat (*)(const cv::Mat&);
  auto r1 = bench("V1 ptr", static_cast<FnPtr>(w9::BgrToGrayV1));
  auto r2 = bench("V2 span", static_cast<FnPtr>(w9::BgrToGrayV2));
  auto r3 = bench("V3 mdspan", static_cast<FnPtr>(w9::BgrToGrayV3));
  auto r4 = bench("V4 AVX2", static_cast<FnPtr>(w9::BgrToGrayV4));
  auto ref = bench("cvtColor", [](const cv::Mat& src) {
    cv::Mat dst;
    cv::cvtColor(src, dst, cv::COLOR_BGR2GRAY);
    return dst;
  });

  // 一致性验证
  std::cout << std::format(
      "\n一致性：V1↔V2 max_diff={:.0f}, V1↔V3 max_diff={:.0f}, "
      "V1↔V4 max_diff={:.0f}, V1↔cvtColor max_diff={:.0f}\n",
      cv::norm(r1, r2, cv::NORM_INF), cv::norm(r1, r3, cv::NORM_INF),
      cv::norm(r1, r4, cv::NORM_INF), cv::norm(r1, ref, cv::NORM_INF));

  return 0;
}
