/**
 * @file benchmark_move_vs_copy.cpp
 * @brief 移动语义 vs 深拷贝性能对比 Benchmark
 *
 * 测试场景：将 N 帧 4K 图像存入 std::vector
 * 对比：深拷贝方式 vs 移动语义方式
 *
 * 预期结果：
 * - 深拷贝：每帧复制 ~24MB，耗时较长
 * - 移动语义：仅指针转移，耗时接近 0ms
 *
 * 注意：为了准确测量拷贝/移动的差异，需要排除内存分配时间
 */

#include <chrono>
#include <iomanip>
#include <iostream>
#include <vector>

#include "custom_image.hpp"

// =============================================================================
//                          Benchmark 配置
// =============================================================================

namespace {

// 测试帧数（20 帧约需 500MB，可在大多数系统上运行）
constexpr size_t kFrameCount = 20;

// 计时器类型别名
using Clock = std::chrono::high_resolution_clock;
using Duration = std::chrono::duration<double, std::milli>;

}  // namespace

// =============================================================================
//                          精确 Benchmark 函数
// =============================================================================

/**
 * @brief 测试纯拷贝操作的性能（预先分配好源数据）
 *
 * 这个测试排除了内存分配时间，只测量拷贝操作本身
 */
Duration BenchmarkPureCopy(size_t frame_count) {
  std::cout << "\n[纯拷贝测试] 开始..." << std::endl;
  std::cout << "  帧数: " << frame_count << std::endl;

  // 步骤1：预先分配所有源图像（不计入时间）
  std::cout << "  准备阶段: 预分配 " << frame_count << " 个源图像..." << std::endl;
  std::vector<CustomImage> sources;
  sources.reserve(frame_count);
  for (size_t i = 0; i < frame_count; ++i) {
    sources.emplace_back(static_cast<uint8_t>(i % 256));
  }

  CustomImage::ResetCounters();

  // 步骤2：只测量拷贝操作的时间
  Duration elapsed;
  {
    std::vector<CustomImage> dest;
    dest.reserve(frame_count);

    auto start = Clock::now();

    for (size_t i = 0; i < frame_count; ++i) {
      // 纯拷贝操作
      dest.push_back(sources[i]);  // 触发拷贝构造
    }

    auto end = Clock::now();
    elapsed = end - start;

    std::cout << "  纯拷贝耗时: " << std::fixed << std::setprecision(2)
              << elapsed.count() << " ms" << std::endl;
    CustomImage::PrintStats();
  }

  return elapsed;
}

/**
 * @brief 测试纯移动操作的性能（预先分配好源数据）
 *
 * 这个测试排除了内存分配时间，只测量移动操作本身
 */
Duration BenchmarkPureMove(size_t frame_count) {
  std::cout << "\n[纯移动测试] 开始..." << std::endl;
  std::cout << "  帧数: " << frame_count << std::endl;

  // 步骤1：预先分配所有源图像（不计入时间）
  std::cout << "  准备阶段: 预分配 " << frame_count << " 个源图像..." << std::endl;
  std::vector<CustomImage> sources;
  sources.reserve(frame_count);
  for (size_t i = 0; i < frame_count; ++i) {
    sources.emplace_back(static_cast<uint8_t>(i % 256));
  }

  CustomImage::ResetCounters();

  // 步骤2：只测量移动操作的时间
  Duration elapsed;
  {
    std::vector<CustomImage> dest;
    dest.reserve(frame_count);

    auto start = Clock::now();

    for (size_t i = 0; i < frame_count; ++i) {
      // 纯移动操作
      dest.push_back(std::move(sources[i]));  // 触发移动构造
    }

    auto end = Clock::now();
    elapsed = end - start;

    std::cout << "  纯移动耗时: " << std::fixed << std::setprecision(2)
              << elapsed.count() << " ms" << std::endl;
    CustomImage::PrintStats();
  }

  return elapsed;
}

/**
 * @brief 常规使用场景测试：创建+存储
 *
 * 包含构造函数的内存分配时间
 */
Duration BenchmarkTypicalCopy(size_t frame_count) {
  std::cout << "\n[典型场景-拷贝] 开始..." << std::endl;
  std::cout << "  帧数: " << frame_count << std::endl;

  CustomImage::ResetCounters();

  Duration elapsed;
  {
    std::vector<CustomImage> images;
    images.reserve(frame_count);

    auto start = Clock::now();

    for (size_t i = 0; i < frame_count; ++i) {
      CustomImage source(static_cast<uint8_t>(i % 256));
      images.push_back(source);  // 拷贝
    }

    auto end = Clock::now();
    elapsed = end - start;

    std::cout << "  耗时: " << std::fixed << std::setprecision(2)
              << elapsed.count() << " ms" << std::endl;
    CustomImage::PrintStats();
  }

  return elapsed;
}

/**
 * @brief 常规使用场景测试：创建+移动存储
 */
Duration BenchmarkTypicalMove(size_t frame_count) {
  std::cout << "\n[典型场景-移动] 开始..." << std::endl;
  std::cout << "  帧数: " << frame_count << std::endl;

  CustomImage::ResetCounters();

  Duration elapsed;
  {
    std::vector<CustomImage> images;
    images.reserve(frame_count);

    auto start = Clock::now();

    for (size_t i = 0; i < frame_count; ++i) {
      CustomImage source(static_cast<uint8_t>(i % 256));
      images.push_back(std::move(source));  // 移动
    }

    auto end = Clock::now();
    elapsed = end - start;

    std::cout << "  耗时: " << std::fixed << std::setprecision(2)
              << elapsed.count() << " ms" << std::endl;
    CustomImage::PrintStats();
  }

  return elapsed;
}

/**
 * @brief emplace_back 场景测试
 */
Duration BenchmarkEmplace(size_t frame_count) {
  std::cout << "\n[emplace_back 测试] 开始..." << std::endl;
  std::cout << "  帧数: " << frame_count << std::endl;

  CustomImage::ResetCounters();

  Duration elapsed;
  {
    std::vector<CustomImage> images;
    images.reserve(frame_count);

    auto start = Clock::now();

    for (size_t i = 0; i < frame_count; ++i) {
      images.emplace_back(static_cast<uint8_t>(i % 256));
    }

    auto end = Clock::now();
    elapsed = end - start;

    std::cout << "  耗时: " << std::fixed << std::setprecision(2)
              << elapsed.count() << " ms" << std::endl;
    CustomImage::PrintStats();
  }

  return elapsed;
}

// =============================================================================
//                          结果分析
// =============================================================================

void PrintPureSummary(Duration pure_copy, Duration pure_move) {
  std::cout << "\n" << std::string(60, '=') << std::endl;
  std::cout << "              纯拷贝 vs 纯移动 对比 (核心指标)" << std::endl;
  std::cout << std::string(60, '=') << std::endl;

  double copy_ms = pure_copy.count();
  double move_ms = pure_move.count();

  std::cout << std::fixed << std::setprecision(2);
  std::cout << "\n| 操作     | 耗时 (ms)    | 性能提升      |" << std::endl;
  std::cout << "|----------|--------------|---------------|" << std::endl;
  std::cout << "| 深拷贝   | " << std::setw(12) << copy_ms << " | 基准       |" << std::endl;
  std::cout << "| 移动语义 | " << std::setw(12) << move_ms << " | "
            << std::setw(10) << (copy_ms / move_ms) << "x |" << std::endl;

  std::cout << "\n[关键发现]" << std::endl;
  std::cout << "  - 移动语义相比深拷贝快 " << (copy_ms / move_ms) << " 倍!" << std::endl;
  
  if (move_ms < 1.0) {
    std::cout << "  ✓ 移动操作耗时 < 1ms，接近零拷贝！" << std::endl;
  }

  double data_per_frame_mb = CustomImage::kImageSize / 1024.0 / 1024.0;
  double total_data_gb = kFrameCount * data_per_frame_mb / 1024.0;
  std::cout << "\n[数据量分析]" << std::endl;
  std::cout << "  - 每帧数据: " << data_per_frame_mb << " MB" << std::endl;
  std::cout << "  - 总数据量: " << total_data_gb << " GB" << std::endl;
  std::cout << "  - 深拷贝需复制: " << total_data_gb << " GB" << std::endl;
  std::cout << "  - 移动语义复制: 0 GB (仅指针赋值)" << std::endl;
}

void PrintTypicalSummary(Duration typical_copy, Duration typical_move,
                          Duration emplace) {
  std::cout << "\n" << std::string(60, '=') << std::endl;
  std::cout << "            典型使用场景对比 (包含内存分配)" << std::endl;
  std::cout << std::string(60, '=') << std::endl;

  double copy_ms = typical_copy.count();
  double move_ms = typical_move.count();
  double emplace_ms = emplace.count();

  std::cout << std::fixed << std::setprecision(2);
  std::cout << "\n| 方式          | 耗时 (ms)    | 相对性能      |" << std::endl;
  std::cout << "|---------------|--------------|---------------|" << std::endl;
  std::cout << "| 创建+拷贝     | " << std::setw(12) << copy_ms 
            << " | 1.00x (基准) |" << std::endl;
  std::cout << "| 创建+移动     | " << std::setw(12) << move_ms << " | "
            << std::setw(10) << (copy_ms / move_ms) << "x |" << std::endl;
  std::cout << "| emplace_back  | " << std::setw(12) << emplace_ms << " | "
            << std::setw(10) << (copy_ms / emplace_ms) << "x |" << std::endl;

  std::cout << "\n[说明]" << std::endl;
  std::cout << "  典型场景包含对象构造（内存分配+初始化）的时间，" << std::endl;
  std::cout << "  因此性能差距小于纯拷贝/移动测试。" << std::endl;
  std::cout << "  但在实际应用中，避免不必要的拷贝仍然至关重要！" << std::endl;
}

// =============================================================================
//                          编译期验证展示
// =============================================================================

void ShowCompileTimeVerification() {
  std::cout << "\n" << std::string(60, '=') << std::endl;
  std::cout << "              编译期类型特性验证 (static_assert)" << std::endl;
  std::cout << std::string(60, '=') << std::endl;

  std::cout << "\n以下特性已在编译期通过 static_assert 验证：" << std::endl;
  std::cout << "  ✓ std::is_move_constructible_v<CustomImage>" << std::endl;
  std::cout << "  ✓ std::is_move_assignable_v<CustomImage>" << std::endl;
  std::cout << "  ✓ std::is_nothrow_move_constructible_v<CustomImage>"
            << std::endl;
  std::cout << "  ✓ std::is_nothrow_move_assignable_v<CustomImage>"
            << std::endl;
  std::cout << "  ✓ std::is_copy_constructible_v<CustomImage>" << std::endl;
  std::cout << "  ✓ std::is_copy_assignable_v<CustomImage>" << std::endl;

  std::cout << "\n[noexcept 的重要性]" << std::endl;
  std::cout << "  移动操作标记 noexcept 后，std::vector 扩容时会优先使用移动" << std::endl;
  std::cout << "  而非保守地使用拷贝，这对容器性能至关重要。" << std::endl;
}

// =============================================================================
//                          主函数
// =============================================================================

int main() {
  std::cout << std::string(60, '=') << std::endl;
  std::cout << "     W2 实战：移动语义 vs 深拷贝 性能 Benchmark" << std::endl;
  std::cout << std::string(60, '=') << std::endl;

  std::cout << "\n[测试配置]" << std::endl;
  std::cout << "  测试帧数: " << kFrameCount << " 帧" << std::endl;
  std::cout << "  图像尺寸: " << CustomImage::Width() << " x "
            << CustomImage::Height() << " x " << CustomImage::Channels()
            << " (4K BGR)" << std::endl;
  std::cout << "  单帧大小: "
            << (CustomImage::kImageSize / 1024.0 / 1024.0) << " MB"
            << std::endl;

  // =========================================================================
  // 第一组测试：纯拷贝 vs 纯移动（排除内存分配时间）
  // =========================================================================
  std::cout << "\n" << std::string(60, '-') << std::endl;
  std::cout << "  第一组：纯操作测试（排除内存分配时间）" << std::endl;
  std::cout << std::string(60, '-') << std::endl;

  Duration pure_copy = BenchmarkPureCopy(kFrameCount);
  Duration pure_move = BenchmarkPureMove(kFrameCount);

  PrintPureSummary(pure_copy, pure_move);

  // =========================================================================
  // 第二组测试：典型使用场景（包含内存分配）
  // =========================================================================
  std::cout << "\n" << std::string(60, '-') << std::endl;
  std::cout << "  第二组：典型使用场景测试" << std::endl;
  std::cout << std::string(60, '-') << std::endl;

  Duration typical_copy = BenchmarkTypicalCopy(kFrameCount);
  Duration typical_move = BenchmarkTypicalMove(kFrameCount);
  Duration emplace = BenchmarkEmplace(kFrameCount);

  PrintTypicalSummary(typical_copy, typical_move, emplace);

  // 展示编译期验证
  ShowCompileTimeVerification();

  std::cout << "\n" << std::string(60, '=') << std::endl;
  std::cout << "                    Benchmark 完成" << std::endl;
  std::cout << std::string(60, '=') << std::endl;

  return 0;
}
