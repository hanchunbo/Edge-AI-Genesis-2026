// SPDX-License-Identifier: MIT
//
// 文件功能：AI 模型文件扫描器类声明（从 model_scanner.cpp 提取）

#ifndef W3_FILESYSTEM_MODEL_SCANNER_HPP_
#define W3_FILESYSTEM_MODEL_SCANNER_HPP_

#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// ============================================================================
// 模型文件元数据
// ============================================================================
struct ModelFileInfo {
  std::string path;
  std::string filename;
  std::string extension;
  std::uintmax_t size;

  // 返回人类可读的文件大小（B / KiB / MiB / GiB）
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
// ModelScanner —— 递归扫描目录，筛选 AI 模型文件
// ============================================================================
class ModelScanner {
 public:
  static constexpr std::string_view kOnnxExtension = ".onnx";
  static constexpr std::string_view kEngineExtension = ".engine";
  static constexpr std::string_view kTrtExtension = ".trt";
  static constexpr std::string_view kPtExtension = ".pt";

  explicit ModelScanner(std::string_view root_path) : root_path_(root_path) {}

  // 检查路径是否为有效目录
  bool IsValidPath() const {
    return std::filesystem::exists(root_path_) &&
           std::filesystem::is_directory(root_path_);
  }

  // 扫描目录，返回所有模型文件的元数据列表；路径无效时返回 nullopt
  std::optional<std::vector<ModelFileInfo>> Scan() const {
    if (!IsValidPath()) {
      return std::nullopt;
    }

    std::vector<ModelFileInfo> models;
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(root_path_)) {
      if (!entry.is_regular_file()) continue;

      const auto& path = entry.path();
      std::string ext = path.extension().string();
      if (IsModelExtension(ext)) {
        models.push_back(
            {path.string(), path.filename().string(), ext, entry.file_size()});
      }
    }
    return models;
  }

  void ScanAndPrint() const;

 private:
  static bool IsModelExtension(std::string_view ext) {
    return ext == kOnnxExtension || ext == kEngineExtension ||
           ext == kTrtExtension || ext == kPtExtension;
  }

  std::filesystem::path root_path_;
};

#endif  // W3_FILESYSTEM_MODEL_SCANNER_HPP_
