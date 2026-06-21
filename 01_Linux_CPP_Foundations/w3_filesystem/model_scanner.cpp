// SPDX-License-Identifier: MIT
//
// AI 模型自动扫描器 (C++20 版本)
//
// 功能：递归扫描指定目录，筛选 .onnx / .engine 模型文件，
//       返回包含文件名与大小的元数据。
//
// 【C++20 特性应用】
// ┌─────────────────────────────────────────────────────────────────────────┐
// │ 特性                    │ 替代的传统方案          │ 优势              │
// ├─────────────────────────┼────────────────────────┼──────────────────┤
// │ std::format             │ std::cout / iostream   │ 类型安全、可读   │
// │ std::filesystem         │ platform-specific API  │ 跨平台、统一     │
// │ std::optional           │ 返回 nullptr/错误码    │ 语义清晰         │
// │ std::string_view        │ const std::string&     │ 零拷贝视图       │
// │ 结构化绑定              │ std::get / .first      │ 代码简洁         │
// └─────────────────────────────────────────────────────────────────────────┘
//
// 【std::format vs iostream 对比】
//
// ┌────────────────────────────────────────────────────────────────────────┐
// │ iostream (传统方式)                                                    │
// │                                                                        │
// │ std::cout << "File: " << filename << ", size: " << size << " bytes\n"; │
// │                                                                        │
// │ 问题：                                                                  │
// │ - 格式修饰符（如 std::setw, std::setprecision）是有状态的             │
// │ - 类型不匹配时编译器可能不报错                                         │
// │ - 难以国际化（字符串顺序硬编码）                                       │
// ├────────────────────────────────────────────────────────────────────────┤
// │ std::format (C++20)                                                    │
// │                                                                        │
// │ std::format("File: {}, size: {} bytes\n", filename, size);            │
// │                                                                        │
// │ 优势：                                                                  │
// │ - 编译期类型检查（类型不匹配会编译失败）                               │
// │ - 无状态，每次调用独立                                                 │
// │ - 支持位置参数，便于国际化                                             │
// │ - 格式说明符类似 Python f-string，更直观                              │
// └────────────────────────────────────────────────────────────────────────┘
//
// ============================================================================

#include "model_scanner.hpp"

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;

// ScanAndPrint 实现（依赖 iostream，留在 .cpp 中）
void ModelScanner::ScanAndPrint() const {
  std::cout << std::format("{:=^50}\n", " AI Model Scanner (C++20) ");
  std::cout << std::format("Scanning: {}\n\n", root_path_.string());

  auto result = Scan();
  if (!result) {
    std::cerr << std::format("[ERROR] Invalid path or not a directory!\n");
    return;
  }

  const auto& models = result.value();
  if (models.empty()) {
    std::cout << std::format("[INFO] No model files found.\n");
    return;
  }

  std::cout << std::format("Found {} model file(s):\n", models.size());
  size_t index = 0;
  std::uintmax_t total_size = 0;
  for (const auto& [path, filename, extension, size] : models) {
    ++index;
    total_size += size;
    std::cout << std::format(
        "[{:>2}] {}  ({})\n", index, filename,
        ModelFileInfo{path, filename, extension, size}.GetHumanReadableSize());
  }
  std::cout << std::format("Total: {} files\n", models.size());
  (void)total_size;
}

// ============================================================================
// 演示辅助函数
// ============================================================================

/// 创建测试目录结构
void CreateTestDirectory(const fs::path& base) {
  fs::path models_dir = base / "models";
  fs::create_directories(models_dir);
  fs::create_directories(models_dir / "detection");
  fs::create_directories(models_dir / "segmentation");

  auto CreateDummyFile = [](const fs::path& path, size_t size) {
    std::ofstream file(path, std::ios::binary);
    if (file) {
      std::vector<char> data(size, 'x');
      file.write(data.data(), static_cast<std::streamsize>(data.size()));
    }
  };

  CreateDummyFile(models_dir / "yolov5s.onnx", 28 * 1024 * 1024);
  CreateDummyFile(models_dir / "yolov5s.engine", 35 * 1024 * 1024);
  CreateDummyFile(models_dir / "detection" / "yolov8n.onnx", 6 * 1024 * 1024);
  CreateDummyFile(models_dir / "detection" / "yolov8n.engine",
                  10 * 1024 * 1024);
  CreateDummyFile(models_dir / "segmentation" / "sam_vit_b.pt",
                  375 * 1024 * 1024);
  CreateDummyFile(models_dir / "readme.txt", 1024);  // 非模型文件

  std::cout << std::format("[SETUP] Created test directory at: {}\n\n",
                           models_dir.string());
}

/// 清理测试目录
void CleanupTestDirectory(const fs::path& base) {
  fs::path models_dir = base / "models";
  if (fs::exists(models_dir)) {
    fs::remove_all(models_dir);
    std::cout << std::format("\n[CLEANUP] Removed: {}\n", models_dir.string());
  }
}

// ============================================================================
// 主函数
// ============================================================================
int main() {
  std::cout << std::format("{:=^60}\n", " W3: C++20 Filesystem Model Scanner ");

  fs::path current_dir = fs::current_path();

  // 测试 1: 创建测试环境并扫描
  std::cout << std::format(
      "\n[TEST 1] Creating test directory and scanning...\n\n");
  CreateTestDirectory(current_dir);

  fs::path models_dir = current_dir / "models";
  ModelScanner scanner(models_dir.string());
  scanner.ScanAndPrint();

  // 测试 2: 无效路径处理
  std::cout << std::format("\n[TEST 2] Testing invalid path handling...\n\n");
  ModelScanner invalid_scanner("/nonexistent/path/to/models");
  invalid_scanner.ScanAndPrint();

  // 测试 3: 空目录
  std::cout << std::format("\n[TEST 3] Testing empty directory...\n\n");
  fs::path empty_dir = current_dir / "empty_models";
  fs::create_directories(empty_dir);
  ModelScanner empty_scanner(empty_dir.string());
  empty_scanner.ScanAndPrint();
  fs::remove_all(empty_dir);

  // 测试 4: 演示 std::format 格式化能力
  std::cout << std::format("\n[TEST 4] std::format demonstration...\n\n");
  std::cout << std::format("  Integer:    {:>10d}\n", 42);
  std::cout << std::format("  Hex:        {:#010x}\n", 255);
  std::cout << std::format("  Float:      {:>10.2f}\n", 3.14159);
  std::cout << std::format("  Padded:     {:*^20}\n", "CENTER");
  std::cout << std::format("  Percent:    {:.1f}%\n", 0.8765 * 100);

  // 清理
  CleanupTestDirectory(current_dir);

  std::cout << std::format("\n{:=^60}\n", " All tests completed! ");

  return 0;
}
