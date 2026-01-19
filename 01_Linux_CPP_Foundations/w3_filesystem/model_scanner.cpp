// Copyright 2026 Edge-AI-Genesis-2026
//
// AI 模型自动扫描器
// 功能：递归扫描指定目录，筛选 .onnx / .engine 模型文件，
//       返回包含文件名与大小的元数据。
//
// 核心技术要点 (C++17)：
// - std::filesystem：跨平台路径管理与目录遍历
// - std::optional：优雅处理可能失败的操作
// - std::string_view：视图优化，避免不必要的字符串拷贝
// - 结构化绑定：简化多值返回的处理

#ifndef MODEL_SCANNER_CPP_
#define MODEL_SCANNER_CPP_

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <chrono>
#include <iomanip>

namespace fs = std::filesystem;

// =============================================================================
// 知识点笔记：std::filesystem (C++17)
// =============================================================================
// std::filesystem 提供了跨平台的文件系统操作接口，统一了 Linux/Windows/macOS
// 的路径处理逻辑。
//
// 核心类型：
// - fs::path：表示文件系统路径，支持 / 运算符拼接
// - fs::directory_entry：表示目录中的一个条目
// - fs::directory_iterator：非递归遍历目录
// - fs::recursive_directory_iterator：递归遍历目录
//
// 常用操作：
// - fs::exists(path)：检查路径是否存在
// - fs::is_regular_file(path)：检查是否为普通文件
// - fs::is_directory(path)：检查是否为目录
// - fs::file_size(path)：获取文件大小（字节）
// - path.extension()：获取文件扩展名
// - path.filename()：获取文件名
// - path.stem()：获取不含扩展名的文件名
// =============================================================================

// =============================================================================
// 知识点笔记：std::optional (C++17)
// =============================================================================
// std::optional<T> 是一个可能包含值也可能为空的容器，用于替代：
// - 返回指针 + nullptr 表示失败
// - 返回 bool + 引用参数输出
// - 抛出异常表示非异常情况的失败
//
// 核心操作：
// - std::nullopt：表示空值
// - optional.has_value() 或 if (optional)：检查是否有值
// - optional.value()：获取值（若为空则抛 bad_optional_access）
// - optional.value_or(default)：获取值或返回默认值
// - *optional：解引用获取值（需确保有值）
//
// 在 AI 部署场景中的典型应用：
// - 解析配置文件中的可选字段
// - 尝试加载模型文件（可能不存在）
// - 查找设备（GPU 可能不可用）
// =============================================================================

// =============================================================================
// 知识点笔记：std::string_view (C++17)
// =============================================================================
// std::string_view 是字符串的"视图"，不拥有数据、不分配内存。
// 它只是指向现有字符串数据的指针 + 长度。
//
// 性能优势（关键！）：
// - 传递 string_view 比传递 std::string 快（无拷贝）
// - 从 const char* 或 std::string 创建 string_view 是 O(1)
// - 非常适合只读字符串操作（如解析、搜索、比较）
//
// 注意事项：
// - string_view 不保证以 null 结尾
// - 必须确保底层数据的生命周期 > string_view 的生命周期
// - 不要返回指向局部变量的 string_view
//
// 在 AI 部署场景中的典型应用：
// - 解析日志/配置文件中的字段
// - 处理模型元数据中的字符串
// - 高频路径字符串操作
// =============================================================================

// 模型文件元数据结构
struct ModelFileInfo {
  std::string path;        // 完整路径
  std::string filename;    // 文件名（含扩展名）
  std::string extension;   // 扩展名
  std::uintmax_t size;     // 文件大小（字节）

  // 返回人类可读的文件大小
  std::string GetHumanReadableSize() const {
    constexpr double kKiB = 1024.0;
    constexpr double kMiB = kKiB * 1024.0;
    constexpr double kGiB = kMiB * 1024.0;

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);

    if (size >= kGiB) {
      oss << (size / kGiB) << " GiB";
    } else if (size >= kMiB) {
      oss << (size / kMiB) << " MiB";
    } else if (size >= kKiB) {
      oss << (size / kKiB) << " KiB";
    } else {
      oss << size << " B";
    }
    return oss.str();
  }
};

// =============================================================================
// 知识点笔记：结构化绑定 (Structured Bindings, C++17)
// =============================================================================
// 结构化绑定允许将元组、pair、数组或具有公共成员的结构体
// 的多个元素绑定到独立的变量中。
//
// 语法：auto [var1, var2, ...] = expression;
//
// 典型用例：
// 1. 遍历 map 时同时获取 key 和 value：
//    for (const auto& [key, value] : my_map) { ... }
//
// 2. 函数返回多个值：
//    auto [result, error_code] = ParseFile(path);
//
// 3. 解构自定义结构体：
//    auto [x, y, z] = GetPoint3D();
// =============================================================================

// AI 模型扫描器类
class ModelScanner {
 public:
  // 支持的模型文件扩展名
  // 使用 string_view 避免运行时字符串分配
  static constexpr std::string_view kOnnxExtension = ".onnx";
  static constexpr std::string_view kEngineExtension = ".engine";
  static constexpr std::string_view kTrtExtension = ".trt";
  static constexpr std::string_view kPtExtension = ".pt";

  // 构造函数：设置要扫描的根目录
  explicit ModelScanner(std::string_view root_path)
      : root_path_(root_path) {}

  // 检查路径是否有效
  bool IsValidPath() const {
    return fs::exists(root_path_) && fs::is_directory(root_path_);
  }

  // 扫描目录，返回找到的模型文件列表
  // 使用 optional 表示可能的失败（路径无效时）
  std::optional<std::vector<ModelFileInfo>> Scan() const {
    if (!IsValidPath()) {
      return std::nullopt;  // 路径无效，返回空
    }

    std::vector<ModelFileInfo> models;

    // 递归遍历目录
    for (const auto& entry : fs::recursive_directory_iterator(root_path_)) {
      // 跳过非文件
      if (!entry.is_regular_file()) {
        continue;
      }

      // 获取扩展名并检查是否为模型文件
      // 使用 string_view 进行比较，避免创建临时 string
      const auto& path = entry.path();
      std::string ext = path.extension().string();

      if (IsModelExtension(ext)) {
        // 使用 C++17 兼容的聚合初始化（按顺序）
        ModelFileInfo info{
            path.string(),           // path
            path.filename().string(),  // filename
            ext,                      // extension
            entry.file_size()          // size
        };
        models.push_back(std::move(info));
      }
    }

    return models;  // 隐式转换为 optional<vector>
  }

  // 扫描并打印结果到控制台
  void ScanAndPrint() const {
    std::cout << "========================================\n";
    std::cout << "       AI Model Scanner (C++17)\n";
    std::cout << "========================================\n";
    std::cout << "Scanning: " << root_path_ << "\n\n";

    // 使用 optional 检查扫描结果
    auto result = Scan();

    if (!result) {
      std::cerr << "[ERROR] Invalid path or directory does not exist!\n";
      return;
    }

    // 使用引用避免拷贝
    const auto& models = result.value();

    if (models.empty()) {
      std::cout << "[INFO] No model files found.\n";
      return;
    }

    std::cout << "Found " << models.size() << " model file(s):\n";
    std::cout << "----------------------------------------\n";

    // 使用结构化绑定遍历（演示）
    // 虽然这里是简单遍历，但展示了结构化绑定的用法
    size_t index = 0;
    std::uintmax_t total_size = 0;

    for (const auto& [path, filename, extension, size] : models) {
      ++index;
      total_size += size;

      std::cout << "[" << index << "] " << filename << "\n";
      std::cout << "    Extension: " << extension << "\n";
      std::cout << "    Size: " << ModelFileInfo{path, filename, extension, size}
                                      .GetHumanReadableSize() << "\n";
      std::cout << "    Path: " << path << "\n\n";
    }

    std::cout << "----------------------------------------\n";
    std::cout << "Total: " << models.size() << " files, ";

    // 打印总大小
    ModelFileInfo dummy{"", "", "", total_size};
    std::cout << dummy.GetHumanReadableSize() << "\n";
  }

 private:
  fs::path root_path_;

  // 检查扩展名是否为支持的模型格式
  // 参数使用 string_view 避免拷贝
  static bool IsModelExtension(std::string_view ext) {
    return ext == kOnnxExtension ||
           ext == kEngineExtension ||
           ext == kTrtExtension ||
           ext == kPtExtension;
  }
};

// =============================================================================
// 性能优化说明
// =============================================================================
// 本程序通过以下方式避免冗余字符串分配：
//
// 1. IsModelExtension 函数参数使用 string_view
//    - 传入 ext 时无需创建临时 std::string
//    - 比较操作直接使用字符指针，O(n) 时间复杂度
//
// 2. 常量扩展名使用 constexpr string_view
//    - 编译期确定，零运行时开销
//    - 存储在只读数据段
//
// 3. ModelFileInfo 使用 std::move 入栈
//    - 避免拷贝整个结构体
//
// 4. 结果使用 const 引用接收
//    - const auto& models = result.value();
//    - 避免拷贝整个 vector
// =============================================================================

// 演示用：创建测试目录结构
void CreateTestDirectory(const fs::path& base) {
  // 创建 models 目录
  fs::path models_dir = base / "models";
  fs::create_directories(models_dir);
  fs::create_directories(models_dir / "detection");
  fs::create_directories(models_dir / "segmentation");

  // 创建一些模拟的模型文件
  auto CreateDummyFile = [](const fs::path& path, size_t size) {
    std::ofstream file(path, std::ios::binary);
    if (file) {
      std::vector<char> data(size, 'x');
      file.write(data.data(), static_cast<std::streamsize>(data.size()));
    }
  };

  // 创建不同大小的测试文件
  CreateDummyFile(models_dir / "yolov5s.onnx", 28 * 1024 * 1024);  // 28 MiB
  CreateDummyFile(models_dir / "yolov5s.engine", 35 * 1024 * 1024);  // 35 MiB
  CreateDummyFile(models_dir / "detection" / "yolov8n.onnx", 6 * 1024 * 1024);
  CreateDummyFile(models_dir / "detection" / "yolov8n.engine", 10 * 1024 * 1024);
  CreateDummyFile(models_dir / "segmentation" / "sam_vit_b.pt", 375 * 1024 * 1024);
  CreateDummyFile(models_dir / "readme.txt", 1024);  // 非模型文件

  std::cout << "[SETUP] Created test directory structure at: " << models_dir
            << "\n\n";
}

// 清理测试目录
void CleanupTestDirectory(const fs::path& base) {
  fs::path models_dir = base / "models";
  if (fs::exists(models_dir)) {
    fs::remove_all(models_dir);
    std::cout << "\n[CLEANUP] Removed test directory: " << models_dir << "\n";
  }
}

int main() {
  std::cout << "=================================================\n";
  std::cout << "   W3: C++17 Filesystem Model Scanner Demo\n";
  std::cout << "=================================================\n\n";

  // 获取当前工作目录作为测试基础路径
  fs::path current_dir = fs::current_path();

  // ===== 测试 1: 创建测试环境并扫描 =====
  std::cout << "[TEST 1] Creating test directory and scanning models...\n\n";
  CreateTestDirectory(current_dir);

  fs::path models_dir = current_dir / "models";
  ModelScanner scanner(models_dir.string());
  scanner.ScanAndPrint();

  // ===== 测试 2: 测试 optional 处理无效路径 =====
  std::cout << "\n[TEST 2] Testing invalid path handling...\n\n";
  ModelScanner invalid_scanner("/nonexistent/path/to/models");
  invalid_scanner.ScanAndPrint();

  // ===== 测试 3: 测试空目录 =====
  std::cout << "\n[TEST 3] Testing empty directory...\n\n";
  fs::path empty_dir = current_dir / "empty_models";
  fs::create_directories(empty_dir);
  ModelScanner empty_scanner(empty_dir.string());
  empty_scanner.ScanAndPrint();
  fs::remove_all(empty_dir);

  // ===== 测试 4: 演示结构化绑定 =====
  std::cout << "\n[TEST 4] Demonstrating structured bindings...\n\n";

  if (auto result = scanner.Scan(); result) {
    std::cout << "Using structured binding in range-for:\n";
    for (const auto& [path, filename, ext, size] : *result) {
      // 这里的 path, filename, ext, size 是通过结构化绑定解构的
      std::cout << "  - " << filename << " (" << ext << "): " << size
                << " bytes\n";
    }
  }

  // ===== 测试 5: 演示 string_view 性能 =====
  std::cout << "\n[TEST 5] String view performance note...\n";
  std::cout << "  - IsModelExtension() uses string_view parameter\n";
  std::cout << "  - No temporary std::string created during comparison\n";
  std::cout << "  - constexpr string_view for extensions = zero runtime alloc\n";

  // 清理测试文件
  CleanupTestDirectory(current_dir);

  std::cout << "\n=================================================\n";
  std::cout << "   All tests completed!\n";
  std::cout << "=================================================\n";

  return 0;
}

#endif  // MODEL_SCANNER_CPP_
