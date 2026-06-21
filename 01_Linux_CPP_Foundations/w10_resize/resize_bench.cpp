// SPDX-License-Identifier: MIT
//
// 文件功能：Resize 算子 benchmark，对比 V1/V2/V3 与 cv::resize 的耗时

#include "custom_resize.hpp"

#include <chrono>
#include <format>
#include <iostream>
#include <opencv2/imgproc.hpp>
#include <string>

namespace {

using Clock = std::chrono::steady_clock;

// 运行 func 共 iters 次，返回平均耗时（毫秒）
template <typename Func>
double BenchMs(Func&& func, int warmup, int iters) {
  // 热身：排除首次调用的 cache miss / JIT 抖动
  for (int i = 0; i < warmup; ++i)
    func();

  auto t0 = Clock::now();
  for (int i = 0; i < iters; ++i)
    func();
  auto t1 = Clock::now();

  double total_us = static_cast<double>(
      std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
  return total_us / static_cast<double>(iters) / 1000.0;
}

void PrintRow(std::string_view name, double ms) {
  std::cout << std::format("  {:<30s}  {:>8.3f} ms\n", name, ms);
}

}  // namespace

int main() {
  // 测试图：模拟摄像头 1080p 输入，下采样到 640×640（YOLO 标准输入）
  const int src_w = 1920, src_h = 1080;
  const int dst_w = 640, dst_h = 640;
  const int kWarmup = 3;
  const int kIters = 50;

  // 随机填充源图（避免全零被编译器优化掉）
  cv::Mat src(src_h, src_w, CV_8UC3);
  cv::randu(src, cv::Scalar(0, 0, 0), cv::Scalar(255, 255, 255));

  std::cout << std::format(
      "\n[W10 Resize Benchmark]  src={}×{}  dst={}×{}  iters={}\n\n", src_w,
      src_h, dst_w, dst_h, kIters);

  // ---- V1 最近邻（asymmetric）----
  PrintRow("V1 Nearest (asymmetric)", BenchMs(
                                          [&] {
                                            auto r = w10::ResizeNearest(
                                                src, dst_w, dst_h,
                                                w10::CoordMode::kAsymmetric);
                                            (void)r;
                                          },
                                          kWarmup, kIters));

  // ---- V1 最近邻（half_pixel）----
  PrintRow("V1 Nearest (half_pixel)", BenchMs(
                                          [&] {
                                            auto r = w10::ResizeNearest(
                                                src, dst_w, dst_h,
                                                w10::CoordMode::kHalfPixel);
                                            (void)r;
                                          },
                                          kWarmup, kIters));

  // ---- V2 双线性（asymmetric）----
  PrintRow("V2 Bilinear (asymmetric)", BenchMs(
                                           [&] {
                                             auto r = w10::ResizeBilinear(
                                                 src, dst_w, dst_h,
                                                 w10::CoordMode::kAsymmetric);
                                             (void)r;
                                           },
                                           kWarmup, kIters));

  // ---- V2 双线性（half_pixel）----
  PrintRow("V2 Bilinear (half_pixel)", BenchMs(
                                           [&] {
                                             auto r = w10::ResizeBilinear(
                                                 src, dst_w, dst_h,
                                                 w10::CoordMode::kHalfPixel);
                                             (void)r;
                                           },
                                           kWarmup, kIters));

  // ---- V3 Letterbox ----
  PrintRow("V3 Letterbox", BenchMs(
                               [&] {
                                 w10::LetterboxInfo info{};
                                 auto r =
                                     w10::Letterbox(src, dst_w, dst_h, info);
                                 (void)r;
                               },
                               kWarmup, kIters));

  // ---- OpenCV 参考（NEAREST）----
  PrintRow("cv::resize NEAREST", BenchMs(
                                     [&] {
                                       cv::Mat r;
                                       cv::resize(src, r,
                                                  cv::Size(dst_w, dst_h), 0, 0,
                                                  cv::INTER_NEAREST);
                                     },
                                     kWarmup, kIters));

  // ---- OpenCV 参考（LINEAR）----
  PrintRow("cv::resize LINEAR", BenchMs(
                                    [&] {
                                      cv::Mat r;
                                      cv::resize(src, r, cv::Size(dst_w, dst_h),
                                                 0, 0, cv::INTER_LINEAR);
                                    },
                                    kWarmup, kIters));

  std::cout << "\n";
  return 0;
}
