// SPDX-License-Identifier: MIT
//
// 文件功能：W15 分类预处理实现 —— 详见 preprocess.hpp。

#include "preprocess.hpp"

#include <algorithm>
#include <cmath>
#include <opencv2/imgproc.hpp>

namespace w15 {

std::vector<float> Preprocess(const cv::Mat& bgr, const PreprocConfig& cfg) {
  // 1) 短边 resize 到 cfg.resize_short，保持长宽比（分类标准，避免拉伸失真）
  const int h = bgr.rows;
  const int w = bgr.cols;
  const float scale =
      static_cast<float>(cfg.resize_short) / static_cast<float>(std::min(h, w));
  const int nh = static_cast<int>(std::lround(h * scale));
  const int nw = static_cast<int>(std::lround(w * scale));
  cv::Mat resized;
  cv::resize(bgr, resized, cv::Size(nw, nh), 0, 0, cv::INTER_LINEAR);

  // 2) center-crop 到 cfg.crop×cfg.crop
  const int x = (nw - cfg.crop) / 2;
  const int y = (nh - cfg.crop) / 2;
  const cv::Mat cropped = resized(cv::Rect(x, y, cfg.crop, cfg.crop)).clone();

  // 3) BGR2RGB + /255 + 归一化 + HWC2CHW，一次循环写入连续 CHW buffer
  const size_t hw = static_cast<size_t>(cfg.crop) * cfg.crop;
  std::vector<float> out(3 * hw);
  for (int yy = 0; yy < cfg.crop; ++yy) {
    for (int xx = 0; xx < cfg.crop; ++xx) {
      const cv::Vec3b px = cropped.at<cv::Vec3b>(yy, xx);  // BGR
      const size_t pos = static_cast<size_t>(yy) * cfg.crop + xx;
      for (int c = 0; c < 3; ++c) {
        // 输出通道 c 为 RGB 序：c=0->R, 1->G, 2->B。
        // OpenCV 像素是 BGR：R=px[2], G=px[1], B=px[0]，故 to_rgb 时 src=2-c。
        const int src_c = cfg.to_rgb ? (2 - c) : c;
        float v = static_cast<float>(px[src_c]) / 255.0f;
        v = (v - cfg.mean[c]) / cfg.std[c];
        out[c * hw + pos] = v;
      }
    }
  }
  return out;
}

}  // namespace w15
