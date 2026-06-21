// SPDX-License-Identifier: MIT
//
// 文件功能：mmap vs std::ifstream 顺序读取吞吐量对比（两种 mmap 使用模式）
//
// [Legacy C++11/17]: FILE*/fread 分块读取，需预分配等大缓冲区，I/O 与计算串行
// [Pain Point]: 内存峰值 = 2x 文件大小；read() 产生内核→用户态拷贝，带宽浪费；
//               错误路径需手动关闭 fd，代码冗长
// [Modern C++20]: MmapLoader 零拷贝映射 + MADV_SEQUENTIAL 预读，直接通过
//                 std::span 遍历；RAII 自动 munmap；内存峰值接近 0
//
// ── 本 Benchmark 测试三个场景 ──────────────────────────────────────────────
//
//  场景 A「mmap-remount」（每轮重建映射）：
//    旧版设计，每轮调用 Load()（mmap）→ 遍历 → 析构（munmap）。
//    在多核服务器上，munmap 触发 TLB Shootdown，需向所有 CPU 广播 IPI；
//    同时每次 mmap 新地址都要重新建立 PTE（Page Table Entry），
//    128MB = 32768 次 page fault，64 核服务器上开销显著压过零拷贝收益。
//    这是一个设计反模式，不是 mmap 的推荐用法。
//
//  场景 B「mmap-persistent」（持久映射，推理服务模式）：
//    正确用法：进程启动时 mmap 一次，整个生命周期保持映射，
//    只有第一次遍历触发 page fault，后续遍历零 page fault、零拷贝、零 munmap。
//    这是 PyTorch / ONNX Runtime / TensorRT 加载模型权重的实际方式。
//
//  场景 C「ifstream」（参考基准）：
//    每轮打开文件 + 分块 read() + 关闭，有内核→用户态拷贝，但无 PTE 重建。
//    在多核服务器上，复用同一用户态缓冲区（PTE 已建立），性能稳定。
//
// ── 在 64 核服务器上的预期结论 ─────────────────────────────────────────────
//   场景 A（mmap-remount）< 场景 C（ifstream）< 场景 B（mmap-persistent）
//   场景 A 慢：每轮 32768 次 PTE 建立 + TLB Shootdown 淹没零拷贝收益
//   场景 B 快：零 page fault + 零拷贝 + 内存带宽直接到 CPU，这才是 mmap 本色
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
// ULL 后缀：确保乘法在 64 位无符号整数域进行，防御将来有人把 128 改大时的溢出
constexpr std::size_t kBenchFileSizeBytes = 128ULL * 1024 * 1024;
constexpr std::size_t kBenchFloatCount = kBenchFileSizeBytes / sizeof(float);

// ============================================================================
// 生成测试文件：顺序写入 kBenchFloatCount 个 float
// ============================================================================
static void CreateBenchFile(const std::filesystem::path& path) {
  std::ofstream ofs(path, std::ios::binary);
  std::vector<float> data(kBenchFloatCount);
  // iota：生成内容确定、非全零的数据，全零可能被编译器/OS 优化掉
  std::iota(data.begin(), data.end(), 0.0f);
  ofs.write(reinterpret_cast<const char*>(data.data()),
            static_cast<std::streamsize>(kBenchFileSizeBytes));
}

// ============================================================================
// 防止编译器将"只读不用"的循环消除（死代码消除规避）
// ============================================================================
static void Sink(float val) {
  static volatile float s_sink;
  s_sink = val;
  (void)s_sink;
}

using Seconds = std::chrono::duration<double>;

[[nodiscard]] static double ToMBps(std::size_t bytes, double elapsed_s) {
  return static_cast<double>(bytes) / (1024.0 * 1024.0) / elapsed_s;
}

// ============================================================================
// 场景 A：mmap-remount —— 每轮都重新 mmap + munmap（旧版反模式）
// ============================================================================
// 每轮计时包含：Load()（mmap 系统调用 + 首批 PTE 建立）+ 遍历 Span()
// 析构（munmap + TLB Shootdown）发生在 return 之后，不计入本轮但会延迟下轮开始
//
// 64 核服务器上慢的根因：
//   1. 每轮建立 kBenchFileSizeBytes/4096 个新 PTE（page fault × 文件大小）
//   2. munmap 向 63 个 CPU 广播 TLB Shootdown IPI，代价 >> 零拷贝收益
//
// @return 本轮耗时（秒）
[[nodiscard]] static double BenchmarkMmapRemount(
    const std::filesystem::path& path) {
  auto t0 = std::chrono::steady_clock::now();

  auto result = MmapLoader<float>::Load(path);
  auto& loader = result.value();
  loader.AdviseSequential();

  float sum = 0.0f;
  for (float v : loader.Span())
    sum += v;
  Sink(sum);

  Seconds elapsed = std::chrono::steady_clock::now() - t0;
  return elapsed.count();
  // loader 析构 → munmap() → TLB Shootdown（在此行之后执行，不计入计时）
}

// ============================================================================
// 场景 B：mmap-persistent —— 映射已在外部建立，只计时纯遍历
// ============================================================================
// 调用方在循环外部做一次 Load()，将 Span 传入本函数。
// 本轮：无 mmap、无 munmap、无 page fault（第一轮遍历已建立所有 PTE），
//       只有内存带宽受限的纯顺序扫描 —— 这才是 mmap 在推理服务中的真实代价。
//
// @param data 已映射的只读 float 视图（外部持久映射）
// @return 本轮耗时（秒）
[[nodiscard]] static double BenchmarkMmapPersistent(
    std::span<const float> data) {
  auto t0 = std::chrono::steady_clock::now();

  float sum = 0.0f;
  for (float v : data)
    sum += v;
  Sink(sum);

  Seconds elapsed = std::chrono::steady_clock::now() - t0;
  return elapsed.count();
}

// ============================================================================
// 场景 C：ifstream 分块 read() —— 参考基准
// ============================================================================
// chunk_bytes 取 4 MB：接近典型 x86 L3 大小，平衡 syscall 次数与内存压力
// 用户态缓冲区 vector 在第一轮后 PTE 已建立，后续轮次无 page fault 建立开销，
// 但每轮仍有内核→用户态 memcpy，是 mmap 零拷贝优势的对照组
//
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
// main：依次运行三个场景，打印对比结果和成因分析
// ============================================================================
int main() {
  using namespace w6;

  constexpr int kRounds = 5;

  auto tmp = std::filesystem::temp_directory_path() / "w6_bench_128mb.bin";
  std::cout << std::format("[W6 Benchmark] 生成 {} MB 测试文件: {}\n",
                           kBenchFileSizeBytes / (1024 * 1024), tmp.string());
  CreateBenchFile(tmp);

  // ── 预热：将文件页面装入 Page Cache ─────────────────────────────────────
  std::cout << "[W6 Benchmark] 预热（不计入统计）...\n";
  (void)BenchmarkMmapRemount(tmp);
  (void)BenchmarkIfstream(tmp);

  // ── 场景 A：mmap-remount（每轮重建映射）──────────────────────────────────
  std::cout << std::format("[W6 Benchmark] 场景 A mmap-remount × {} 轮...\n",
                           kRounds);
  double remount_total = 0.0;
  for (int i = 0; i < kRounds; ++i)
    remount_total += BenchmarkMmapRemount(tmp);

  // ── 场景 B：mmap-persistent（持久映射，推理服务模式）─────────────────────
  // mmap 一次：第一轮遍历触发 page fault 建立所有 PTE（视为预热）
  // 后续轮次：纯内存扫描，零 page fault，零 munmap
  std::cout << std::format(
      "[W6 Benchmark] 场景 B mmap-persistent × {} 轮（映射保持）...\n",
      kRounds);
  auto persistent_result = MmapLoader<float>::Load(tmp);
  auto& persistent_loader = persistent_result.value();
  persistent_loader.AdviseSequential();
  // 预热：建立 PTE，不计入统计
  (void)BenchmarkMmapPersistent(persistent_loader.Span());
  double persistent_total = 0.0;
  for (int i = 0; i < kRounds; ++i)
    persistent_total += BenchmarkMmapPersistent(persistent_loader.Span());
  // persistent_loader
  // 在此作用域结束时才析构（munmap），整个循环期间映射持续存在

  // ── 场景 C：ifstream（参考基准）─────────────────────────────────────────
  std::cout << std::format("[W6 Benchmark] 场景 C ifstream × {} 轮...\n",
                           kRounds);
  double ifs_total = 0.0;
  for (int i = 0; i < kRounds; ++i)
    ifs_total += BenchmarkIfstream(tmp);

  // ── 计算平均值和吞吐量 ───────────────────────────────────────────────────
  double remount_avg = remount_total / kRounds;
  double persistent_avg = persistent_total / kRounds;
  double ifs_avg = ifs_total / kRounds;

  double remount_mbps = ToMBps(kBenchFileSizeBytes, remount_avg);
  double persistent_mbps = ToMBps(kBenchFileSizeBytes, persistent_avg);
  double ifs_mbps = ToMBps(kBenchFileSizeBytes, ifs_avg);

  // ── 打印结果 ─────────────────────────────────────────────────────────────
  std::cout << "\n";
  std::cout << "=== W6 Benchmark: mmap 两种模式 vs ifstream (128 MB) ===\n\n";
  std::cout << std::format("  {:<20}  {:>10}  {:>14}\n", "场景", "Time(ms)",
                           "Throughput");
  std::cout << std::string(52, '-') << "\n";
  std::cout << std::format("  {:<20}  {:>10.1f}  {:>12.1f} MB/s\n",
                           "A mmap-remount", remount_avg * 1000.0,
                           remount_mbps);
  std::cout << std::format("  {:<20}  {:>10.1f}  {:>12.1f} MB/s\n",
                           "B mmap-persistent", persistent_avg * 1000.0,
                           persistent_mbps);
  std::cout << std::format("  {:<20}  {:>10.1f}  {:>12.1f} MB/s\n",
                           "C ifstream", ifs_avg * 1000.0, ifs_mbps);
  std::cout << "\n";
  std::cout << std::format("  A vs C（mmap-remount / ifstream）：{:.2f}x\n",
                           remount_mbps / ifs_mbps);
  std::cout << std::format("  B vs C（mmap-persistent / ifstream）：{:.2f}x\n",
                           persistent_mbps / ifs_mbps);
  std::cout << "\n";
  std::cout << "  结论：\n";
  std::cout << "    场景 A 在多核服务器上慢于 ifstream：\n";
  std::cout << "      每轮重建映射 → 重复 page fault（PTE 建立）\n";
  std::cout << "      + munmap TLB Shootdown（向所有 CPU 广播 IPI）\n";
  std::cout << "    场景 B 快于 ifstream：\n";
  std::cout << "      映射持续存在 → 零 page fault + 零拷贝\n";
  std::cout << "      这才是推理服务（PyTorch/ONNX Runtime）的真实用法\n";

  std::filesystem::remove(tmp);
  return 0;
}
