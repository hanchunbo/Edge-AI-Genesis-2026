// SPDX-License-Identifier: MIT
//
// 文件功能：ModelScanner 单元测试

#include "model_scanner.hpp"

#include <fstream>
#include <gtest/gtest.h>

namespace {

// ============================================================================
// 测试辅助：在系统临时目录创建/清理测试文件树
// ============================================================================
class TempDirFixture : public ::testing::Test {
 protected:
  void SetUp() override {
    base_ = std::filesystem::temp_directory_path() / "w3_test";
    std::filesystem::create_directories(base_);
  }

  void TearDown() override { std::filesystem::remove_all(base_); }

  // 创建指定大小的空文件
  void CreateFile(const std::filesystem::path& rel_path, size_t size = 1024) {
    auto full = base_ / rel_path;
    std::filesystem::create_directories(full.parent_path());
    std::ofstream ofs(full, std::ios::binary);
    std::vector<char> data(size, 'x');
    ofs.write(data.data(), static_cast<std::streamsize>(data.size()));
  }

  std::filesystem::path base_;
};

// ============================================================================
// 测试套件：路径有效性
// ============================================================================

TEST_F(TempDirFixture, ValidPathReturnsTrue) {
  ModelScanner scanner(base_.string());
  EXPECT_TRUE(scanner.IsValidPath());
}

TEST_F(TempDirFixture, InvalidPathReturnsFalse) {
  ModelScanner scanner("/nonexistent/path/to/models");
  EXPECT_FALSE(scanner.IsValidPath());
}

TEST_F(TempDirFixture, FilePathIsNotDirectory) {
  CreateFile("some_file.txt");
  ModelScanner scanner((base_ / "some_file.txt").string());
  EXPECT_FALSE(scanner.IsValidPath());
}

// ============================================================================
// 测试套件：Scan() 返回值
// ============================================================================

TEST_F(TempDirFixture, ScanInvalidPathReturnsNullopt) {
  ModelScanner scanner("/no/such/dir");
  auto result = scanner.Scan();
  EXPECT_FALSE(result.has_value());
}

TEST_F(TempDirFixture, ScanEmptyDirReturnsEmptyVector) {
  ModelScanner scanner(base_.string());
  auto result = scanner.Scan();
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->empty());
}

TEST_F(TempDirFixture, ScanFindsOnnxFile) {
  CreateFile("yolov5s.onnx", 1024);
  ModelScanner scanner(base_.string());
  auto result = scanner.Scan();
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->size(), 1u);
  EXPECT_EQ(result->at(0).extension, ".onnx");
  EXPECT_EQ(result->at(0).filename, "yolov5s.onnx");
}

TEST_F(TempDirFixture, ScanFindsAllSupportedExtensions) {
  CreateFile("model.onnx");
  CreateFile("model.engine");
  CreateFile("model.trt");
  CreateFile("model.pt");

  ModelScanner scanner(base_.string());
  auto result = scanner.Scan();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->size(), 4u);
}

TEST_F(TempDirFixture, ScanIgnoresNonModelFiles) {
  CreateFile("readme.txt");
  CreateFile("config.json");
  CreateFile("weights.bin");
  CreateFile("model.onnx");  // 只有这个应被找到

  ModelScanner scanner(base_.string());
  auto result = scanner.Scan();
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->size(), 1u);
  EXPECT_EQ(result->at(0).extension, ".onnx");
}

TEST_F(TempDirFixture, ScanIsRecursive) {
  // 在子目录中放模型文件
  CreateFile("subdir/detection/yolov8n.onnx");
  CreateFile("subdir/segmentation/sam.pt");
  CreateFile("top.engine");

  ModelScanner scanner(base_.string());
  auto result = scanner.Scan();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->size(), 3u);
}

TEST_F(TempDirFixture, ScanReturnsCorrectFileSize) {
  constexpr size_t kSize = 4096;
  CreateFile("model.onnx", kSize);

  ModelScanner scanner(base_.string());
  auto result = scanner.Scan();
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->size(), 1u);
  EXPECT_EQ(result->at(0).size, kSize);
}

// ============================================================================
// 测试套件：ModelFileInfo::GetHumanReadableSize()
// ============================================================================

TEST(ModelFileInfoTest, SizeInBytes) {
  ModelFileInfo info{"", "", "", 512};
  EXPECT_EQ(info.GetHumanReadableSize(), "512 B");
}

TEST(ModelFileInfoTest, SizeInKiB) {
  ModelFileInfo info{"", "", "", 2048};
  EXPECT_EQ(info.GetHumanReadableSize(), "2.00 KiB");
}

TEST(ModelFileInfoTest, SizeInMiB) {
  ModelFileInfo info{"", "", "", 1024 * 1024 * 3};
  EXPECT_EQ(info.GetHumanReadableSize(), "3.00 MiB");
}

TEST(ModelFileInfoTest, SizeInGiB) {
  ModelFileInfo info{"", "", "",
                     static_cast<std::uintmax_t>(1024) * 1024 * 1024 * 2};
  EXPECT_EQ(info.GetHumanReadableSize(), "2.00 GiB");
}

}  // namespace
