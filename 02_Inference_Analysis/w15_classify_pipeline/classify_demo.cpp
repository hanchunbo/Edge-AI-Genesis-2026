// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：W15 分类 demo —— 命令行传入图片路径，打印 Top-5 分类结果。
//           用法：./w15_classify_demo <image_path>
// ============================================================================

#include "classifier.hpp"

#include <cstdlib>
#include <exception>
#include <format>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "用法: " << argv[0] << " <image_path>\n";
    return EXIT_FAILURE;
  }
  try {
    w15::Classifier clf(W15_MODEL_PATH, W15_LABELS_PATH);
    const std::vector<w15::TopK> top = clf.Classify(std::string(argv[1]), 5);
    std::cout << "[W15] 分类结果 Top-5:\n";
    for (size_t i = 0; i < top.size(); ++i) {
      std::cout << std::format("  Top-{}: {} ({:.4f})\n", i + 1, top[i].label,
                               top[i].score);
    }
  } catch (const std::exception& e) {
    std::cerr << "[W15] 错误: " << e.what() << "\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
