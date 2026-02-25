// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：MmapLoader 单元测试
// ============================================================================

#include "mmap_loader.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

namespace {

// ============================================================================
// 测试辅助：将数据写入临时文件，返回文件路径
// ============================================================================
// 使用 std::filesystem::temp_directory_path() 获取系统临时目录，
// 避免在仓库目录写入，也避免硬编码 /tmp（跨平台兼容）
template <typename T>
std::filesystem::path WriteTempFile(const std::vector<T>& data,
                                    const std::string& name) {
  auto path = std::filesystem::temp_directory_path() / name;
  std::ofstream ofs(path, std::ios::binary);
  ofs.write(reinterpret_cast<const char*>(data.data()),
            static_cast<std::streamsize>(data.size() * sizeof(T)));
  return path;
}

// ============================================================================
// 测试套件：正常加载路径
// ============================================================================

TEST(MmapLoaderTest, LoadFloatArraySuccess) {
  // 准备已知内容的测试文件
  const std::vector<float> expected = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
  auto path = WriteTempFile(expected, "test_floats.bin");

  auto result = w6::MmapLoader<float>::Load(path);

  ASSERT_TRUE(result.has_value()) << "Load 应成功";
  EXPECT_TRUE(result->Valid());
  EXPECT_EQ(result->Size(), expected.size());
  EXPECT_EQ(result->SizeBytes(), expected.size() * sizeof(float));

  // 验证每个元素值正确（mmap 零拷贝，值应与写入完全一致）
  auto span = result->Span();
  for (size_t i = 0; i < expected.size(); ++i) {
    EXPECT_FLOAT_EQ(span[i], expected[i]) << "index=" << i;
  }

  std::filesystem::remove(path);
}

TEST(MmapLoaderTest, LoadInt32ArraySuccess) {
  // 验证模板对不同类型的正确性（int32_t 在量化模型中常见）
  const std::vector<int32_t> expected = {0, -1, 42, 0x7FFFFFFF, -2147483648};
  auto path = WriteTempFile(expected, "test_int32.bin");

  auto result = w6::MmapLoader<int32_t>::Load(path);

  ASSERT_TRUE(result.has_value());
  auto span = result->Span();
  for (size_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ(span[i], expected[i]);
  }

  std::filesystem::remove(path);
}

// ============================================================================
// 测试套件：std::span 视图接口
// ============================================================================

TEST(MmapLoaderTest, SpanSubspanWorks) {
  // 验证 subspan 切片不触发拷贝，仍能正确访问数据
  const std::vector<float> data = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f};
  auto path = WriteTempFile(data, "test_subspan.bin");

  auto result = w6::MmapLoader<float>::Load(path);
  ASSERT_TRUE(result.has_value());

  auto span = result->Span();

  // 切片 [2, 5)
  auto sub = span.subspan(2, 3);
  ASSERT_EQ(sub.size(), 3u);
  EXPECT_FLOAT_EQ(sub[0], 0.3f);
  EXPECT_FLOAT_EQ(sub[1], 0.4f);
  EXPECT_FLOAT_EQ(sub[2], 0.5f);

  // first/last
  EXPECT_FLOAT_EQ(span.first(1)[0], 0.1f);
  EXPECT_FLOAT_EQ(span.last(1)[0], 0.6f);

  std::filesystem::remove(path);
}

TEST(MmapLoaderTest, ImplicitConversionToSpan) {
  // 验证隐式 operator span_type() 正常工作
  // 场景：推理引擎接受 std::span<const float>，可直接传入 MmapLoader
  const std::vector<float> data = {3.14f, 2.71f};
  auto path = WriteTempFile(data, "test_implicit.bin");

  auto result = w6::MmapLoader<float>::Load(path);
  ASSERT_TRUE(result.has_value());

  // 隐式转换
  std::span<const float> span = *result;
  EXPECT_EQ(span.size(), 2u);
  EXPECT_FLOAT_EQ(span[0], 3.14f);

  std::filesystem::remove(path);
}

TEST(MmapLoaderTest, RangeForWorks) {
  // std::span 支持范围 for，验证迭代器行为正确
  const std::vector<float> data = {1.0f, 2.0f, 3.0f};
  auto path = WriteTempFile(data, "test_range_for.bin");

  auto result = w6::MmapLoader<float>::Load(path);
  ASSERT_TRUE(result.has_value());

  float sum = 0.0f;
  for (float v : result->Span()) {
    sum += v;
  }
  EXPECT_FLOAT_EQ(sum, 6.0f);

  std::filesystem::remove(path);
}

// ============================================================================
// 测试套件：错误处理路径
// ============================================================================

TEST(MmapLoaderTest, ErrorFileNotFound) {
  auto result = w6::MmapLoader<float>::Load("/nonexistent/path/to/weights.bin");

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), w6::MmapError::kOpenFailed);
}

TEST(MmapLoaderTest, ErrorEmptyFile) {
  // 写入空文件（0 字节）
  auto path = std::filesystem::temp_directory_path() / "test_empty.bin";
  std::ofstream(path, std::ios::binary);  // 创建空文件

  auto result = w6::MmapLoader<float>::Load(path);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), w6::MmapError::kEmptyFile);

  std::filesystem::remove(path);
}

TEST(MmapLoaderTest, ErrorSizeMismatch) {
  // 写入 7 字节（不是 sizeof(float)=4 的整数倍），应报 kSizeMismatch
  auto path = std::filesystem::temp_directory_path() / "test_mismatch.bin";
  {
    std::ofstream ofs(path, std::ios::binary);
    const char buf[7] = {1, 2, 3, 4, 5, 6, 7};
    ofs.write(buf, 7);
  }

  auto result = w6::MmapLoader<float>::Load(path);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), w6::MmapError::kSizeMismatch);

  std::filesystem::remove(path);
}

// ============================================================================
// 测试套件：移动语义
// ============================================================================

TEST(MmapLoaderTest, MoveConstructor) {
  const std::vector<float> data = {9.0f, 8.0f, 7.0f};
  auto path = WriteTempFile(data, "test_move_ctor.bin");

  auto result = w6::MmapLoader<float>::Load(path);
  ASSERT_TRUE(result.has_value());

  // 移动构造：新对象接管映射，源对象置为无效
  w6::MmapLoader<float> moved = std::move(*result);

  EXPECT_TRUE(moved.Valid());
  EXPECT_EQ(moved.Size(), 3u);
  EXPECT_FLOAT_EQ(moved.Span()[0], 9.0f);

  // 移动后源对象无效
  EXPECT_FALSE(result->Valid());
  EXPECT_EQ(result->Size(), 0u);

  std::filesystem::remove(path);
}

TEST(MmapLoaderTest, MoveAssignment) {
  const std::vector<float> data = {1.1f, 2.2f};
  auto path = WriteTempFile(data, "test_move_assign.bin");

  auto result = w6::MmapLoader<float>::Load(path);
  ASSERT_TRUE(result.has_value());

  w6::MmapLoader<float> target;
  EXPECT_FALSE(target.Valid());

  target = std::move(*result);
  EXPECT_TRUE(target.Valid());
  EXPECT_EQ(target.Size(), 2u);
  EXPECT_FLOAT_EQ(target.Span()[1], 2.2f);

  std::filesystem::remove(path);
}

TEST(MmapLoaderTest, DefaultConstructedIsInvalid) {
  w6::MmapLoader<float> loader;

  EXPECT_FALSE(loader.Valid());
  EXPECT_FALSE(static_cast<bool>(loader));
  EXPECT_EQ(loader.Size(), 0u);
  EXPECT_EQ(loader.SizeBytes(), 0u);
  EXPECT_TRUE(loader.Span().empty());
}

// ============================================================================
// 测试套件：madvise 接口（调用不崩溃即通过）
// ============================================================================

TEST(MmapLoaderTest, MadviseCallsDoNotCrash) {
  const std::vector<float> data(1024, 1.0f);
  auto path = WriteTempFile(data, "test_madvise.bin");

  auto result = w6::MmapLoader<float>::Load(path);
  ASSERT_TRUE(result.has_value());

  // 以下调用必须正常完成，不崩溃
  result->AdviseSequential();
  result->Prefetch();
  result->AdviseRandom();

  std::filesystem::remove(path);
}

// ============================================================================
// 测试套件：Concept 约束（编译期验证）
// ============================================================================

TEST(MmapLoaderTest, ConceptAcceptsValidTypes) {
  // 验证 TriviallyMappable 约束允许合法类型（编译通过即通过）
  static_assert(w6::TriviallyMappable<float>);
  static_assert(w6::TriviallyMappable<double>);
  static_assert(w6::TriviallyMappable<int8_t>);
  static_assert(w6::TriviallyMappable<int32_t>);
  static_assert(w6::TriviallyMappable<uint8_t>);

  // 自定义 POD 结构体（模拟量化权重元数据）
  struct WeightMeta {
    int32_t layer_id;
    float scale;
    float zero_point;
  };
  static_assert(w6::TriviallyMappable<WeightMeta>);

  // 空类应被拒绝
  struct Empty {};
  static_assert(!w6::TriviallyMappable<Empty>);

  SUCCEED();  // 以上 static_assert 能编译到此处即为通过
}

// ============================================================================
// 测试套件：MmapErrorToString 覆盖
// ============================================================================

TEST(MmapErrorTest, AllErrorsHaveDescription) {
  using E = w6::MmapError;
  // 每种错误都应有非空描述字符串
  EXPECT_FALSE(w6::MmapErrorToString(E::kOpenFailed).empty());
  EXPECT_FALSE(w6::MmapErrorToString(E::kStatFailed).empty());
  EXPECT_FALSE(w6::MmapErrorToString(E::kEmptyFile).empty());
  EXPECT_FALSE(w6::MmapErrorToString(E::kSizeMismatch).empty());
  EXPECT_FALSE(w6::MmapErrorToString(E::kMmapFailed).empty());
}

}  // namespace
