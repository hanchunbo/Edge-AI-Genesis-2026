// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：mmap vs std::ifstream 顺序读取吞吐量基准测试（128 MB）
// ============================================================================
//
// [Legacy C++11/17]: FILE*/fread 分块读取，需预分配等大缓冲区，I/O 与计算串行
// [Pain Point]: 内存峰值 = 2x 文件大小；read() 产生内核→用户态拷贝，带宽浪费；
//               错误路径需手动关闭 fd，代码冗长
// [Modern C++20]: MmapLoader 零拷贝映射 + MADV_SEQUENTIAL 预读，直接通过
//                 std::span 遍历；RAII 自动 munmap；内存峰值接近 0
//
// 测试方法：
//   首轮预热（让操作系统将文件页面加载进 Page Cache），之后连续计时 N 轮，
//   取平均值。反映同进程内多次加载权重的典型场景（推理服务常驻，权重热加载）。
//   冷启动（首次读盘）的差异主要受磁盘 IOPS 限制，两者差距通常更小。
// ============================================================================

#include "mmap_loader.hpp"

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <numeric>
#include <vector>

namespace w6 {

// 128 MB 测试文件，内容为递增 float 序列（0.0f, 1.0f, 2.0f, ...）
constexpr std::size_t kBenchFileSizeBytes = 128ULL * 1024 * 1024;
constexpr std::size_t kBenchFloatCount = kBenchFileSizeBytes / sizeof(float);

// ============================================================================
// 生成测试文件：顺序写入 kBenchFloatCount 个 float
// ============================================================================
// @param path 输出文件路径（若已存在则覆盖）
static void CreateBenchFile(const std::filesystem::path& path) {
  std::ofstream ofs(path, std::ios::binary);
  std::vector<float> data(kBenchFloatCount);
  // iota 填充递增序列，确保编译器无法将后续读取优化为常量
  std::iota(data.begin(), data.end(), 0.0f);
  ofs.write(reinterpret_cast<const char*>(data.data()),
            static_cast<std::streamsize>(kBenchFileSizeBytes));
}

// ============================================================================
// 防止编译器将"只读不用"的循环消除（死代码消除规避）
// 将累加和写入 volatile 变量，产生可见副作用
// ============================================================================
static void Sink(float val) {
  static volatile float s_sink;
  s_sink = val;
  // 强制读回，抑制 GCC "set but not used" 警告（写后读保证副作用不被优化）
  (void)s_sink;
}

// ============================================================================
// 计时辅助：将字节数和耗时换算为 MB/s 吞吐量
// ============================================================================
using Seconds = std::chrono::duration<double>;

[[nodiscard]] static double ToMBps(std::size_t bytes, double elapsed_s) {
  return static_cast<double>(bytes) / (1024.0 * 1024.0) / elapsed_s;
}

// ============================================================================
// 方案 A：mmap + MADV_SEQUENTIAL + std::span 顺序遍历
// ============================================================================
// 计时范围覆盖 MmapLoader::Load()（mmap 系统调用）+ 全量顺序求和。
// mmap 本身几乎瞬时完成（仅建立页表映射），实际 I/O 由 Page Fault 惰性触发。
// MADV_SEQUENTIAL 指示内核激进预读，将 Page Fault 代价分摊到后台。
//
// @return 本轮耗时（秒）
[[nodiscard]] static double BenchmarkMmap(const std::filesystem::path& path) {
  auto t0 = std::chrono::steady_clock::now();

  auto result = MmapLoader<float>::Load(path);
  auto& loader = result.value();
  // 顺序预读提示：内核会主动预取后续页面，降低 Page Fault 停顿
  loader.AdviseSequential();

  float sum = 0.0f;
  for (float v : loader.Span())
    sum += v;
  Sink(sum);

  Seconds elapsed = std::chrono::steady_clock::now() - t0;
  return elapsed.count();
}

// ============================================================================
// 方案 B：std::ifstream 分块 read() 到 vector<float> 再顺序遍历
// ============================================================================
// chunk_bytes 取 4 MB：接近典型 x86 L3 大小，在 syscall 次数与额外内存之间
// 取得平衡；过小会导致 syscall 过多，过大会引入额外堆内存压力。
//
// @param path       测试文件路径
// @param chunk_bytes 每次 read() 读取的字节数（默认 4 MB）
// @return 本轮耗时（秒）
[[nodiscard]] static double BenchmarkIfstream(const std::filesystem::path& path,
                                              std::size_t chunk_bytes = 4ULL *
                                                                        1024 *
                                                                        1024) {
  auto t0 = std::chrono::steady_clock::now();

  std::ifstream ifs(path, std::ios::binary);
  std::vector<float> buf(chunk_bytes / sizeof(float));
  float sum = 0.0f;
  std::size_t remaining = kBenchFileSizeBytes;

  // 分块读取：每次 syscall 读 chunk_bytes 字节，内核拷贝到用户缓冲区
  while (remaining > 0) {
    std::size_t to_read = std::min(chunk_bytes, remaining);
    ifs.read(reinterpret_cast<char*>(buf.data()),
             static_cast<std::streamsize>(to_read));
    std::size_t n = to_read / sizeof(float);
    for (std::size_t i = 0; i < n; ++i)
      sum += buf[i];
    remaining -= to_read;
  }
  Sink(sum);

  Seconds elapsed = std::chrono::steady_clock::now() - t0;
  return elapsed.count();
}

}  // namespace w6

// ============================================================================
// main：运行基准并打印对比结果
// ============================================================================

int main() {
  using namespace w6;

  constexpr int kRounds = 5;  // 正式计时轮数，取平均以消除单次抖动

  // 使用系统临时目录，进程退出前手动清理
  auto tmp = std::filesystem::temp_directory_path() / "w6_bench_128mb.bin";

  std::cout << std::format("[W6 Benchmark] 生成 {} MB 测试文件: {}\n",
                           kBenchFileSizeBytes / (1024 * 1024), tmp.string());
  CreateBenchFile(tmp);

  // 预热：将文件页面装入 Page Cache，排除冷启动磁盘 I/O 的干扰
  // (void) 显式丢弃返回值，告知编译器此处有意不使用计时结果
  std::cout << "[W6 Benchmark] 预热（第 1 轮不计入统计）...\n";
  (void)BenchmarkMmap(tmp);
  (void)BenchmarkIfstream(tmp);

  // 正式计时
  std::cout << std::format("[W6 Benchmark] 正式计时 {} 轮...\n", kRounds);
  double mmap_total = 0.0;
  double ifs_total = 0.0;
  for (int i = 0; i < kRounds; ++i) {
    mmap_total += BenchmarkMmap(tmp);
    ifs_total += BenchmarkIfstream(tmp);
  }

  double mmap_avg = mmap_total / kRounds;
  double ifs_avg = ifs_total / kRounds;
  double mmap_mbps = ToMBps(kBenchFileSizeBytes, mmap_avg);
  double ifs_mbps = ToMBps(kBenchFileSizeBytes, ifs_avg);
  double speedup = mmap_mbps / ifs_mbps;

  // 打印结果
  std::cout << "\n";
  std::cout << "=== W6 Benchmark: mmap vs ifstream (128 MB, 热场景) ===\n\n";
  std::cout << std::format("  {:<10}  {:>12}  {:>14}\n", "Method", "Time(ms)",
                           "Throughput");
  std::cout << std::string(42, '-') << "\n";
  std::cout << std::format("  {:<10}  {:>12.1f}  {:>12.1f} MB/s\n", "mmap",
                           mmap_avg * 1000.0, mmap_mbps);
  std::cout << std::format("  {:<10}  {:>12.1f}  {:>12.1f} MB/s\n", "ifstream",
                           ifs_avg * 1000.0, ifs_mbps);
  std::cout << "\n";
  std::cout << std::format("  mmap 加速比：{:.2f}x\n", speedup);
  std::cout << "\n";
  std::cout << "  注：热场景（Page Cache 已预热）。mmap 优势来自省去内核→\n";
  std::cout << "      用户态拷贝；冷启动时差距受磁盘 IOPS 限制，通常更小。\n";

  std::filesystem::remove(tmp);
  return 0;
}
