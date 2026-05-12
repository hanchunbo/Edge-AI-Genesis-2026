# 技术债记录

> 最后更新：2026-04-08（Q2 计划校准，新增 2 条 OPEN 项）

---

## [FIXED] W10 高/中优先级技术债修复

**发现**：2026-03-22（W10 AI 部署专家评估）
**修复**：2026-03-22（dev-W10-Resize-chunbo 分支）

### [FIXED] 中优 — assert 在 Release 下被 NDEBUG 消除

**问题**：`ResizeNearest` / `ResizeBilinear` / `Letterbox` 用 `assert` 做输入校验，
Release 模式下 `NDEBUG` 宏使 assert 完全消失，传入空图或灰度图会导致静默 UB。

**修复**：
- 提取公共 `ValidateSrc(const cv::Mat&)` 辅助函数，检查 `src.empty()` 和 `src.type() != CV_8UC3`
- 不合法时抛 `std::invalid_argument`（含实际类型信息），Release 下同样生效
- 同步将 `dst_w/dst_h ≤ 0` 的参数校验也改为抛异常
- 测试覆盖：`InputValidation.EmptyInputThrows`、`InputValidation.WrongTypeThrows`（共 6 个 EXPECT_THROW）

### [FIXED] 高优 — 输出硬编码 uint8 BGR，推理引擎无法直接使用

**问题**：V1/V2/V3 输出均为 `CV_8UC3 HWC`，ONNX Runtime / TensorRT
接受的 tensor layout 为 `float32 CHW {1,3,H,W}`，需外部手写两步转换，链路不完整。

**修复**：
- 新增 `LetterboxToTensor(src, dst_w, dst_h, info) → std::vector<float>`
  转换链：`uint8 BGR HWC → Letterbox → ÷255 → float32 [0,1] → CHW`
- 输出 layout：`[B面, G面, R面]`，与 ONNX Runtime / TensorRT `{1,3,H,W}` 完全对齐
- `LetterboxInfo` 同步返回，用于后处理坐标反算，无额外开销
- 测试覆盖：`OutputSize`、`ValueRangeIsZeroToOne`、`CHWLayoutCorrect`

**测试结果**：18/18 全部通过（原 13 + 新增 5），零编译警告。

---

## [OPEN] Q2 第一批投递窗口过紧（中优）

**发现**：2026-04-08（Q2 计划校准评估）
**严重度**：🟡 中（不影响代码质量，影响求职节奏）

**现状**：`docs/Q2.md` 执行策略中第一批投递窗口设定为 2026-04-15，
但 W13 综合项目尚未收尾（dev 分支进行中），W14 ORT 环境搭建也未启动。
1 周内完成 W13 收尾 + W14 最小推理闭环的概率较低，强行投递可能暴露项目不完整。

**修复方向**：将第一批投递硬门槛由 4月15日推迟至 **4月25日**：
- 前提：W13 PR 合入 main + W14 能演示加载模型并打印 I/O 节点
- 对应更新 `docs/Q2.md` 「投递触发条件」节和 `docs/career_assessment.md` 黄金时间表

**暂缓原因**：求职时机由用户根据实际进度决策，不强制修改文档中的日期。

---

## [OPEN] Q2 Qt 开发周期（W22-W25）完成度风险（低优）

**发现**：2026-04-08（Q2 计划校准评估）
**严重度**：🟢 低（属于 P1/P2 范围，不阻塞第一批投递）

**现状**：Qt6 对用户而言是新技术栈，W22-W25 共 4 周需完成：
Qt 机制学习 + Worker Thread + 60FPS 渲染 + 双平台打包，
Partner B 可协助但不能代替核心推理线程设计。

**修复方向**：建议弹性执行策略：
1. W22-W23 优先出可演示最小 GUI（摄像头画面 + 检测框叠加），作为面试演示物
2. W24-W25 根据剩余时间决定做深（复杂 UI / 打包）还是转向简历完善
3. EdgeSight 完整版（W26）定位为 bonus，不作为投递硬门槛

**暂缓原因**：Qt 进度弹性已内建在 P1/P2 分级中，无需改动现有计划文档。

---

## [OPEN] W13 kTensor 缺少 ImageNet mean/std 归一化（高优）

**发现**：2026-04-02（W13 AI 部署专家评估）
**严重度**：🔴 高（Q2 W15 接分类模型时直接暴露，影响推理精度）

**现状**：`LetterboxToTensor` 输出只做 `÷255` 缩放到 `[0,1]`，没有 ImageNet 标准归一化。
YOLO v8 只需 `/255` 没有问题，但 ResNet / MobileNet 等分类模型的预训练期望输入为：
- mean = [0.485, 0.456, 0.406]（BGR 顺序对应 R/G/B 各通道）
- std  = [0.229, 0.224, 0.225]

若不归一化，模型输入分布与训练分布不匹配，分类结果完全不可信。

**修复方向**：在 `PipelineConfig` 中增加可选字段：
```cpp
std::optional<std::array<float, 3>> normalize_mean;  // 默认空 = 不归一化
std::optional<std::array<float, 3>> normalize_std;
```
`LetterboxToTensor` 在 `/255` 之后按配置追加 `(x - mean) / std`，YOLO 路径不设此字段保持原行为。

**暂缓原因**：Q1 目标是预处理引擎框架，Q2 W15 接推理管线时一并实现。

---

## [OPEN] W13 kTensor 输出缺少 batch 维度描述（中优）

**发现**：2026-04-02（W13 AI 部署专家评估）
**严重度**：🟡 中（数据内容正确，shape 语义需在 Q2 对接 ORT 时显式处理）

**现状**：`FrameResult.data`（`vector<float>`）的内存布局是 `[C, H, W]`（CHW），
但 ONNX Runtime `Ort::Value::CreateTensor` 需要传入 `shape = {1, 3, H, W}`（NCHW with batch=1）。

**修复方向**：Q2 W14 在 `InferenceEngine` 封装层传入正确 shape，不需要改 W13 代码。
建议在 `FrameResult` 注释或 `notes.md` 中明确标注"输出为 CHW，调用方需在创建 ORT Tensor 时补 batch 维度"。

---

## [OPEN] W13 VPS 单核 Benchmark 不具代表性（中优）

**发现**：2026-04-02（W13 AI 部署专家评估）
**严重度**：🟡 中（简历/面试数据可信度问题，不影响代码正确性）

**现状**：`docs/benchmarks/Q1_week_13.md` 的加速比数据在 VPS 单核环境下测得：
- 2 线程：1.51× 加速
- 4 线程：1.32×（反而退化，单核调度抖动所致）

在简历或面试中直接引用此数据会被有经验的面试官质疑。

**修复方向**：在 Q2 阶段找 4 核以上机器（或开发板）补测，记录真实多核加速比。
现有数据文件需注明"VPS 单核环境，仅供功能验证，不代表多核性能"。

---

## [OPEN] W13 P99 延迟尖刺，缺少绑核/实时调度配置（低优）

**发现**：2026-04-02（W13 AI 部署专家评估）
**严重度**：🟢 低（当前 VPS 环境固有，生产部署时需处理）

**现状**：kGray 模式 P50=510µs，P99=2354µs（4.6× 尖刺）；
kTensor 模式 P99=5744µs vs P50=2554µs（2.2× 尖刺）。
根源是单核 VPS 的 OS 调度抖动（上下文切换导致某帧延迟突增）。

**修复方向**：生产/边缘设备部署时：
- `pthread_setaffinity_np` 绑定 worker 线程到固定核心
- 或使用 `SCHED_FIFO` 实时调度策略

Q2 W18 Profiling 报告中需专门分析此问题，Nsight Timeline 里会看到对应的毛刺。

---

## [OPEN] W10 V2 双线性性能优化

**发现**：2026-03-22（从 W10 notes.md 技术债迁移）
**严重度**：🟡 中（自定义实现比 OpenCV 慢 6×，MCU 等无 OpenCV 平台上有实际影响）

**现状**：V2 双线性内循环为朴素双层嵌套，每像素随机访问 4 个源像素，
编译器无法自动向量化（非连续访存）。实测 1920×1080→640×640 耗时约 **5.85 ms**，
而 OpenCV INTER_LINEAR 仅 **0.97 ms**（6× 差距）。

**优化方向**（参考 OpenCV 内部实现）：
1. **行预计算**：对每个目标行 `dy`，先计算所有源行 `sy` 的水平插值结果（行缓冲），
   访存从随机变为顺序，充分利用 L1 cache line
2. **垂直方向 SIMD**：行缓冲已连续，可用 SSE2/AVX2 批量做垂直加权

**暂缓原因**：推理预处理主路径走 V3 Letterbox（内部调用 `cv::resize`）性能已足够；
V2 自定义实现的价值在于算法理解和无 OpenCV 平台移植，Q2 ONNX Runtime 集成阶段按需优化。

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

## [FIXED] W9 预处理 API P0/P1 技术债修复

**发现**：2026-03-16（W9 AI 部署专家评估）
**修复**：2026-03-17（dev-W9-OpenCV-chunbo 分支）

### [FIXED] P0 — 输入无校验 + API 强制堆分配

**修复内容**：
- 提取公共 `ValidateSrc(src)` 辅助函数，统一检查 `src.empty()` 和 `src.type() != CV_8UC3`，任意版本收到空帧或非 BGR 输入均抛出 `std::invalid_argument`
- 为 V1/V2/V3/V4 全部新增 `void BgrToGrayVx(const cv::Mat& src, cv::Mat& dst)` 重载；`dst.create()` 在尺寸/类型已匹配时是 no-op，生产管道可复用帧缓冲区实现零额外分配
- 测试覆盖：EmptyInputThrows、WrongTypeThrows、VoidOverloadReuseBuffer（共 3 个测试用例）

### [FIXED] P1 — 缺少 SIMD 实现

**修复内容**：
- 新增 `BgrToGrayV4`（AVX2，`-mavx2 -mfma` 编译）：每批处理 8 像素，
  用 `_mm_shuffle_epi8` 直接从 BGR 字节流提取各通道，
  `_mm256_mullo_epi16` + `_mm256_packus_epi16` 完成定点乘加和打包
- 无 `__AVX2__` 时编译期回退 V2，行为等价
- 缓冲区安全：循环条件 `i+10<=total` 保证 hi 段 128-bit 加载不越界
- 测试覆盖：V4ConsistentWithV1（1080P 梯度图）、V4TailPixelsCorrect（17 像素奇数尺寸）

### [FIXED] P1 — 缺少 float32 归一化 + CHW layout 输出

**修复内容**：
- 新增 `BgrToNormCHW(const cv::Mat& src) → std::vector<float>`，
  转换链：`uint8 BGR HWC → ÷255 → float32 [0,1] → CHW (channel-first)`，
  输出格式与 ONNX Runtime / TensorRT `{1,3,H,W}` 输入对齐
- 测试覆盖：ValueRangeIsZeroToOne、CHWLayoutCorrect、OutputSizeCorrect

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

### [FIXED] AI 部署内存初识化冗余及算法语义优化
**详情**：在 W11 `fixed_lab.cpp` 中发现 AI 部署的反模式：使用 `std::vector` 作为张量预分配缓冲区时自带强制清零动作，浪费显存带宽。已重构为 `std::make_unique_for_overwrite`；并将组合式查找更新为语义精确的 `std::binary_search`。
**日期**：2026-03-22

---

## [FIXED] W11 规范违反与文档不一致修复

**发现**：2026-03-29（W11/W12 完成情况评审）
**修复**：2026-03-29

### [FIXED] P0 — `fixed_lab.cpp` TransferData 使用 `std::thread`

`fixed_lab.cpp` 是规范参考版本，TransferData 仍用 `std::thread` 违反项目强制规范。
已替换为 `std::jthread` + 移除手动 join（RAII 自动析构）：

```cpp
// 修复后
void TransferData() {
  // [FIXED] std::jthread RAII 自动 join，析构时无需手动调用，防止异常路径线程泄漏
  std::jthread ta(ThreadA);
  std::jthread tb(ThreadB);
}
```

### [FIXED] P1 — `notes.md` Bug 3 修复说明与代码实现不一致

`notes.md` Bug 一览表中写的是 `std::lower_bound`，但实现已改为 `std::binary_search`（上次技术债修复）。
已同步更新文档，与实现保持一致。

### [FIXED] P1 — `buggy_lab.cpp` Bug 2 注释缺少规范说明

在 `buggy_lab.cpp` Bug 2 的 TransferData 中补充注释，明确说明 `std::thread` 同时违反项目规范，
引导读者参考 `fixed_lab.cpp` 了解正确的 C++20 并发实践。

**验证结果**：`ctest -R W11` 5/5 用例通过，编译零警告。
