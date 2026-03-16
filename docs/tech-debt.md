# 技术债记录

> 最后更新：2026-03-16（W9 实现评估，新增 W9 技术债）

---

## [FIXED] W1-W5 clang-format 不通过

**发现**：2026-02-26（W6 开发期间）
**修复**：2026-02-26，执行 `clang-format -i` 后全部通过，编译和测试均未受影响。

---

## [FIXED] W1-W4 缺少 GTest 单元测试

**修复**：2026-02-26

| 模块 | 测试文件 | 用例数 |
|------|---------|--------|
| W1 | `w1_memory_safety_test.cpp` | 17 |
| W2 | `w2_move_semantics_test.cpp` | 21 |
| W3 | `w3_filesystem_test.cpp`（附带提取 `model_scanner.hpp`） | 14 |
| W4 | `w4_threading_test.cpp`（附带提取 `producer_consumer.hpp`） | 12 |

全部 6 个测试套件 `ctest` 通过（共 84 个用例）。

---

## [FIXED] g++-15 升级 + W8 覆盖率流水线适配

**修复**：2026-03-16（W9 准备期）

1. **g++-15 (15.2.0) 安装**：通过 `ppa:ubuntu-toolchain-r/test` 在本机 WSL2 Ubuntu 24.04 安装，与 VPS 环境统一。
2. **W7 模块演示 CMake 版本检查**：`import std;` 通过 `-fmodule-mapper=` 运行时不回退 `gcm.cache/`，需要 CMake 3.30+ 的 `CMAKE_CXX_MODULE_STD ON`。在 CMake < 3.30 时跳过演示，文档说明原因。
3. **W8 gcov 版本匹配**：lcov 默认使用 gcov-13，但代码用 GCC 15 编译，格式不兼容。通过 `--gcov-tool gcov-15` 修复。
4. **多线程 gcov 竞态**：W4/W5 多线程测试覆盖率计数器出现负数。加 `-fprofile-update=atomic` 后稳定。

**当前覆盖率（g++-15 编译）**：行 98.6%，函数 100.0%（W1-W7 合计）

---

## [OPEN] W9 预处理 API 与推理管道对接存在多项不足

**发现**：2026-03-16（W9 AI 部署专家评估）
**严重度**：🔴 高（直接影响生产部署可行性）

### P0 — 输入无校验 + API 强制堆分配

`BgrToGrayV1/V2/V3` 三个函数均无 `src.empty()` / `src.type()` 校验，空帧或非 BGR 图传入会导致 UB 或崩溃。边缘设备摄像头断流是常态，必须在系统边界防御。

此外，函数返回 `cv::Mat`（每次调用内部 new），无法给调用方传入预分配缓冲区。生产管道应提供：
```cpp
void BgrToGrayV2(const cv::Mat& src, cv::Mat& dst);  // 零额外分配
```

### P1 — 缺少 SIMD 实现，与 cv::cvtColor 差距 2.6×

`bgr2gray.cpp:134-141` 的 AVX2 注释从未落地：
```
V3 mdspan:   ~2.9 ms/frame
cv::cvtColor: ~1.1 ms/frame  ← 差距 2.6×
```
边缘设备上预处理通常是推理管道瓶颈，缺 SIMD 会拖慢整条流水线。
应实现 V4（AVX2/NEON），使用 `_mm256_maddubs_epi16` shuffle 通道分离，预期提升 3-5×。

### P1 — 缺少 float32 归一化 + CHW layout 输出

推理引擎（ONNX Runtime / TensorRT）的标准输入格式为：
```
uint8 BGR → float32 归一化 [0,1] → CHW layout → 模型 tensor
```
W9 只完成第一步，输出是 HWC uint8，与实际推理管道差两步转换，缺少端到端演示。

### P2 — 基准测试设计存在缺陷

- 纯色图像是最优 cache 场景，与真实摄像头帧差异大
- 缺少 warmup 轮次（前几帧有 TLB miss / branch predictor cold start）
- 只测平均延迟，缺 P99（实时推理更关心尾延迟）
- 未测量内存带宽占用（1080P BGR = 6.2 MB/帧，是边缘设备真实瓶颈）

### P2 — 色彩标准硬编码 BT.601 但未显式声明

若模型训练预处理用的是 BT.709，高饱和像素误差可达 15 gray level，属系统性偏差。
当前测试 `max_diff=2` 无法覆盖此类问题。应在 API 文档注释和 notes.md 中明确声明所用标准。

### P3 — 测试集缺少边界场景

| 缺少的测试场景 | 为什么重要 |
|---|---|
| 奇数宽高（如 1921×1081） | SIMD 对齐边界处理 |
| 从磁盘加载的真实图片 | `step[0]` 可能带对齐 padding |
| 并发调用（多线程同时处理） | 静态/全局状态线程安全 |

**暂缓原因**：W9 属于底层技术演示阶段，P0/P1 项在进入实际推理流水线集成（Q2 规划）前修复即可。

---

## [OPEN] W8 CI 缺少覆盖率门禁

**发现**：2026-03-16（W8 验收评审）
**严重度**：🟡 中（缺少自动质量保障，覆盖率下降不会阻断 PR 合并）

**现状**：`.github/workflows/ci.yml` 只执行 build + ctest，不检查覆盖率阈值。
开发者提交新代码后，若行覆盖率低于基线（98.7%），CI 不会自动拒绝。

**期望行为**：CI 在覆盖率环节执行：
```bash
cmake -B build -S . -DCMAKE_CXX_COMPILER=g++-15 -G Ninja -DW8_COVERAGE=ON
cmake --build build -j$(nproc)
cmake --build build --target w8_coverage
lcov --summary build/w8_filtered.info | grep "lines" | awk '{if ($2+0 < 90) exit 1}'
```
覆盖率低于 90% 时 CI job 以非零退出码失败，阻断 PR。

**暂缓原因**：W8 阶段 VPS 上运行完整覆盖率构建约需 30s，CI 时间可接受；
但 lcov 尚未集成到 CI，需要在 ci.yml 中新增覆盖率 job 并处理 lcov 安装。
优先级低于 W9 开发推进，待 Q1 末或 W13 阶段项目时一并完善。

---

## [OPEN] W1-W5 缺少 `[[nodiscard]]` 标注

**严重度**：🟡 中（规范要求所有非 void 函数必须标注，漏标会导致忽略返回值的 bug 无法被编译器捕获）

共计约 **26 处**遗漏，按模块：

| 模块 | 遗漏函数（举例） | 数量 |
|------|----------------|------|
| W1 | `Create()`、`View()`、`At()`、`MakeTensorBuffer()` | 5 |
| W2 | `Data()`、`Size()`、`Valid()`、`View()`、`RowView()`、`CalculateAveragePixel()` | 8 |
| W3 | `IsValidPath()`、`Scan()`、`GetHumanReadableSize()` | 3 |
| W4 | `Pop()`、`IsStopped()` 及多个 getter | 5 |
| W5 | `Submit()`、`SubmitWithToken()`、`GetThreadCount()` 等 getter | 5 |

---

## [OPEN] W4 使用 `std::thread` 而非 `std::jthread`

**严重度**：🟡 中（违反项目规范，需手动 join，有线程泄漏风险）

`producer_consumer.cpp` 中 `ImageProducer` 和 `ImageConsumer` 的实现：

```cpp
// 当前（C++11 风格）
std::thread thread_;
void Start() { thread_ = std::thread(&ImageProducer::ProducerLoop, this, n); }
void Join()  { if (thread_.joinable()) thread_.join(); }

// 应改为（C++20）
std::jthread thread_;  // RAII 自动 join，析构时无需手动调用
void Start() { thread_ = std::jthread(&ImageProducer::ProducerLoop, this, n); }
```

---

## [FIXED] W3 ModelScanner 完成 hpp/cpp 分离

**修复**：2026-02-26（随 W1-W4 测试补齐一起完成）

已拆分为：
- `model_scanner.hpp`：类声明、`ModelFileInfo` 结构体
- `model_scanner.cpp`：实现 + `main()` 演示

---

## [FIXED] W4 `.cpp` 文件不当头文件保护已移除

**修复**：2026-02-26

`producer_consumer.cpp` 中历史遗留的 `#ifndef` / `#endif` 保护宏已删除，
目前仅在头文件 `producer_consumer.hpp` 保留 include guard。

---

## [FIXED] W2-W4 CMakeLists.txt 已显式设置 `CXX_STANDARD`

**修复**：2026-02-26

W2、W3、W4 的 `CMakeLists.txt` 已补充：
```cmake
set_target_properties(<target> PROPERTIES CXX_STANDARD 20)
```
（若将来引入 `std::expected` 则按目标需要升级到 23，与 W1/W6 局部设置保持一致）
