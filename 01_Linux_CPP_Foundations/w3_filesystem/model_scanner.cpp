// Copyright 2026 Edge-AI-Genesis-2026
//
// ============================================================================
// AI 模型自动扫描器 (C++20 版本)
// ============================================================================
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

#include <chrono>
#include <filesystem>
#include <format> // C++20 std::format
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

// ============================================================================
// 知识点：std::format 格式说明符
// ============================================================================
//
// 基本语法：{[arg_id][:format_spec]}
//
// 常用格式说明符：
// - {:d}    - 整数（十进制）
// - {:x}    - 整数（十六进制）
// - {:f}    - 浮点数（固定小数点）
// - {:.2f}  - 浮点数，保留 2 位小数
// - {:>10}  - 右对齐，宽度 10
// - {:<10}  - 左对齐，宽度 10
// - {:^10}  - 居中对齐，宽度 10
// - {:*>10} - 右对齐，宽度 10，填充字符 *
//
// 示例：
// std::format("{:>8.2f} MB", 123.456);  // "  123.46 MB"
// std::format("{:0>5d}", 42);            // "00042"
// ============================================================================

// ============================================================================
// 演进式重构：std::format vs iostream
// ============================================================================
// [Legacy C++11/17]: 使用 iostream 拼接字符串
//   std::cout << "File: " << filename << ", size: " << size << " bytes\n";
// [Pain Point]: 格式修饰符（如 std::setw）有状态，类型不匹配可能不报错，
//   难以国际化（字符串顺序硬编码），代码冗长可读性差
// [Modern C++20]: 使用 std::format 类型安全格式化
//   std::format("File: {}, size: {} bytes\n", filename, size);
//   优势：编译期类型检查、无状态、支持位置参数、类似 Python f-string
// ============================================================================

// 模型文件元数据结构
struct ModelFileInfo {
  std::string path;      // 完整路径
  std::string filename;  // 文件名（含扩展名）
  std::string extension; // 扩展名
  std::uintmax_t size;   // 文件大小（字节）

  /**
   * @brief 返回人类可读的文件大小（使用 std::format）
   */
  std::string GetHumanReadableSize() const {
    constexpr double kKiB = 1024.0;
    constexpr double kMiB = kKiB * 1024.0;
    constexpr double kGiB = kMiB * 1024.0;

    if (size >= kGiB) {
      return std::format("{:.2f} GiB", static_cast<double>(size) / kGiB);
    } else if (size >= kMiB) {
      return std::format("{:.2f} MiB", static_cast<double>(size) / kMiB);
    } else if (size >= kKiB) {
      return std::format("{:.2f} KiB", static_cast<double>(size) / kKiB);
    } else {
      return std::format("{} B", size);
    }
  }
};

// ============================================================================
// AI 模型扫描器类 (C++20)
// ============================================================================
class ModelScanner {
public:
  // 支持的模型文件扩展名（使用 string_view 避免运行时分配）
  static constexpr std::string_view kOnnxExtension = ".onnx";
  static constexpr std::string_view kEngineExtension = ".engine";
  static constexpr std::string_view kTrtExtension = ".trt";
  static constexpr std::string_view kPtExtension = ".pt";

  // 构造函数
  explicit ModelScanner(std::string_view root_path) : root_path_(root_path) {}

  // 检查路径是否有效
  bool IsValidPath() const {
    return fs::exists(root_path_) && fs::is_directory(root_path_);
  }

  // ==========================================================================
  // 演进式重构：std::optional vs nullptr/bool 模式
  // ==========================================================================
  // [Legacy C++11/17]: 返回 nullptr 或 bool + 引用参数表示失败
  //   bool Scan(std::vector<ModelFileInfo>* out) { if (!ok) return false; }
  // [Pain Point]: 语义不清晰，调用者可能忘记检查返回值，
  //   需要额外的输出参数，代码意图不明确
  // [Modern C++20]: 使用 std::optional 表示可能失败的返回值
  //   std::optional<std::vector<ModelFileInfo>> Scan() { return std::nullopt; }
  //   优势：语义清晰（有值/无值），支持 value_or 默认值，链式调用友好
  // ==========================================================================
  /**
   * @brief 扫描目录，返回找到的模型文件列表
   * @return std::optional<std::vector<ModelFileInfo>> 失败返回 nullopt
   */
  std::optional<std::vector<ModelFileInfo>> Scan() const {
    if (!IsValidPath()) {
      return std::nullopt;
    }

    std::vector<ModelFileInfo> models;

    for (const auto &entry : fs::recursive_directory_iterator(root_path_)) {
      if (!entry.is_regular_file()) {
        continue;
      }

      const auto &path = entry.path();
      std::string ext = path.extension().string();

      if (IsModelExtension(ext)) {
        ModelFileInfo info{path.string(), path.filename().string(), ext,
                           entry.file_size()};
        models.push_back(std::move(info));
      }
    }

    return models;
  }

  /**
   * @brief 扫描并打印结果（使用 std::format）
   */
  void ScanAndPrint() const {
    std::cout << std::format("{:=^50}\n", " AI Model Scanner (C++20) ");
    std::cout << std::format("Scanning: {}\n\n", root_path_.string());

    auto result = Scan();

    if (!result) {
      std::cerr << std::format(
          "[ERROR] Invalid path or directory does not exist!\n");
      return;
    }

    const auto &models = result.value();

    if (models.empty()) {
      std::cout << std::format("[INFO] No model files found.\n");
      return;
    }

    std::cout << std::format("Found {} model file(s):\n", models.size());
    std::cout << std::format("{:-^50}\n", "");

    size_t index = 0;
    std::uintmax_t total_size = 0;

    // 使用结构化绑定遍历
    for (const auto &[path, filename, extension, size] : models) {
      ++index;
      total_size += size;

      // 使用 std::format 格式化输出
      std::cout << std::format("[{:>2}] {}\n", index, filename);
      std::cout << std::format("     Extension: {}\n", extension);
      std::cout << std::format("     Size: {:>10}\n",
                               ModelFileInfo{path, filename, extension, size}
                                   .GetHumanReadableSize());
      std::cout << std::format("     Path: {}\n\n", path);
    }

    std::cout << std::format("{:-^50}\n", "");
    std::cout << std::format(
        "Total: {} files, {}\n", models.size(),
        ModelFileInfo{"", "", "", total_size}.GetHumanReadableSize());
  }

private:
  fs::path root_path_;

  // ==========================================================================
  // 演进式重构：std::string_view vs const std::string&
  // ==========================================================================
  // [Legacy C++11/17]: 使用 const std::string& 传递只读字符串
  //   static bool IsModelExtension(const std::string& ext)
  // [Pain Point]: 传入 const char* 时会隐式构造临时 std::string（堆分配），
  //   函数无法接受子串视图（需要再次分配）
  // [Modern C++20]: 使用 std::string_view 零拷贝视图
  //   static bool IsModelExtension(std::string_view ext)
  //   优势：无堆分配、可接受任意字符串来源、constexpr 友好
  // ==========================================================================
  /**
   * @brief 检查扩展名是否为支持的模型格式
   * @param ext 扩展名 (string_view 零拷贝)
   */
  static bool IsModelExtension(std::string_view ext) {
    return ext == kOnnxExtension || ext == kEngineExtension ||
           ext == kTrtExtension || ext == kPtExtension;
  }
};

// ============================================================================
// 测试辅助函数
// ============================================================================

/// 创建测试目录结构
void CreateTestDirectory(const fs::path &base) {
  fs::path models_dir = base / "models";
  fs::create_directories(models_dir);
  fs::create_directories(models_dir / "detection");
  fs::create_directories(models_dir / "segmentation");

  auto CreateDummyFile = [](const fs::path &path, size_t size) {
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
  CreateDummyFile(models_dir / "readme.txt", 1024); // 非模型文件

  std::cout << std::format("[SETUP] Created test directory at: {}\n\n",
                           models_dir.string());
}

/// 清理测试目录
void CleanupTestDirectory(const fs::path &base) {
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
