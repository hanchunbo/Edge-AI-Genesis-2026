// SPDX-License-Identifier: MIT
//
// 文件功能：W16 YOLODetector 演示 —— 读图、检测、画框、存图，并打印检测结果。

#include "yolo_detector.hpp"

#include <cstdio>
#include <fstream>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <string>
#include <vector>

namespace {
// 从文本文件按行读取类名（每行一个，行号即类别 id）。
std::vector<std::string> LoadLabels(const std::string& path) {
  std::ifstream f(path);
  std::vector<std::string> labels;
  std::string line;
  while (std::getline(f, line)) {
    labels.push_back(line);
  }
  return labels;
}
}  // namespace

int main(int argc, char** argv) {
  const std::string image_path = (argc > 1) ? argv[1] : W16_IMAGE_PATH;
  const std::string model_path = (argc > 2) ? argv[2] : W16_MODEL_PATH;
  const std::string labels_path = W16_LABELS_PATH;
  const std::string out_path = "w16_output.jpg";

  std::vector<std::string> labels = LoadLabels(labels_path);

  w16::YOLODetector det(model_path);
  cv::Mat bgr = cv::imread(image_path, cv::IMREAD_COLOR);
  if (bgr.empty()) {
    std::fprintf(stderr, "图片读取失败: %s\n", image_path.c_str());
    return 1;
  }

  std::vector<w16::Detection> dets = det.Detect(bgr);
  std::printf("EP=%s 检测到 %zu 个目标:\n",
              det.ActiveEp() == w14::Ep::kCuda ? "CUDA" : "CPU", dets.size());

  for (const w16::Detection& d : dets) {
    const std::string name =
        (d.class_id >= 0 && d.class_id < static_cast<int>(labels.size()))
            ? labels[d.class_id]
            : std::to_string(d.class_id);
    std::printf("  %-12s %.3f  [%.1f, %.1f, %.1f, %.1f]\n", name.c_str(),
                d.score, d.x1, d.y1, d.x2, d.y2);

    cv::rectangle(bgr,
                  cv::Point(static_cast<int>(d.x1), static_cast<int>(d.y1)),
                  cv::Point(static_cast<int>(d.x2), static_cast<int>(d.y2)),
                  cv::Scalar(0, 255, 0), 2);
    cv::putText(bgr, name,
                cv::Point(static_cast<int>(d.x1), static_cast<int>(d.y1) - 5),
                cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 255, 0), 2);
  }

  cv::imwrite(out_path, bgr);
  std::printf("已保存可视化结果: %s\n", out_path.c_str());
  return 0;
}
