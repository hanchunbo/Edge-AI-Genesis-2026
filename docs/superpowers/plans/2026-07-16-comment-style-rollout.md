# 注释规范落地与存量回填 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 W16+ 注释标准（Doxygen 轻量语法 + `@pre`/`@warning`/`@note` 坑标签 + 位置绑定原则）写进 4 个标准文件，并按新标准回填 quant 与 W16 两个模块的注释。

**Architecture:** 三阶段推进，每阶段独立 commit、可单独验收。① 标准文件先落地（后续回填有据可依）；② quant 回填（真正的重灾区，从零补）；③ W16 对齐（**范围已按实读结果大幅缩小，见下方「范围修正」**）。trt 不在本计划内，待本批收口后另起计划。

**Tech Stack:** C++20（Doxygen `///` 注释语法）、Python（Google 风格 docstring）、clang-format-21、CMake + Ninja、GoogleTest/CTest。

**依据 spec:** `docs/superpowers/specs/2026-07-16-comment-style-design.md`

---

## ⚠️ 范围修正（写计划时实读代码发现，与 spec §5 假设不符）

写计划前逐文件读了 W16 源码，两处发现必须记录：

1. **W16/quant/trt 零演进式注释**：`grep -rn "Legacy C++\|Pain Point\|Modern C++"` 三模块无命中。
   spec §1 原写「W16 仅测试文件残留 2 处」是误判——那两处命中的是**测试名**
   `OptionsOverloadMatchesLegacyOverload`（指旧**重载**，非注释格式）。spec 已修正。
   → **无演进式注释需要清理。**

2. **W16 实质已合规，缺的只是语法**：`decode.hpp` 已完整写明张量布局、YOLOv8 无
   objectness、坐标反算公式、clamp 理由、错误条件；`yolo_detector.hpp` 写明组合而非
   继承的决策、计时口径；`yolo_detector.cpp` 有 BGR→RGB 理由与 1~5 分步。
   **内容层面 W16 已达新标准**，差的仅是 Doxygen 语法与坑标签。
   → **W16 范围从「全模块回填」缩小为「4 个 hpp 的语法对齐 + 补缺失 struct brief」**
   （见 Phase 3）。既有散文**不重写**——符合 CLAUDE.md「不 refactor 没坏的东西」。

净效果：本计划工作量重心压在 quant（真缺）；W16 变成一个小任务。

---

## 文件结构

| 文件 | 责任 | 阶段 |
|---|---|---|
| `CLAUDE.md` | 注释标准**单一事实源** | Phase 1 |
| `AGENTS.md` | 给 Codex 等 agent 的副本，与 CLAUDE.md 逐字同步 | Phase 1 |
| `.agent/rules/google_style_guide.md` | §4 注释规范，指向 CLAUDE.md | Phase 1 |
| `.agent/workflows/format_project.md` | §4 演进式检查限定为 W1–W15 存档 | Phase 1 |
| `02_Inference_Analysis/quantization/*.{hpp,cpp}` | 9 个 C++ 文件，从零补注释 | Phase 2 |
| `02_Inference_Analysis/quantization/tools/*.py` | 2 个脚本，补 Google docstring | Phase 2 |
| `02_Inference_Analysis/w16_yolo_detector/*.hpp` | 4 个头文件语法对齐 | Phase 3 |

---

## Phase 0：基线确认

**目的**：先记录改动前的构建/测试状态，避免把既有问题算到注释改动头上。

### Task 0: 记录基线

**Files:** 无（只读）

- [ ] **Step 1: 确认工作树干净、在 dev 分支**

```bash
cd /home/dev/code/Edge-AI-Genesis-2026
git fetch origin && git status --short && git branch --show-current
```

Expected: 无输出（工作树干净）；分支为 `dev`。若有未提交改动，先停下询问用户。

- [ ] **Step 2: 跑一遍 quant + W16 测试，记录基线**

```bash
ctest --test-dir build -R "Quant_|W16_" --output-on-failure 2>&1 | tail -20
```

Expected: 记录哪些绿、哪些红/跳过。**已知**：`Quant_Int8ConsistencyTest` 依赖
INT8 产物（`build/02_Inference_Analysis/quantization/models/yolov8n.int8.*.onnx`），
缺失时测试内 `SkipIfMissing()` 会跳过而非失败。把本步输出存下来，Phase 2/3 结束后
逐条比对——**注释改动后此结果必须逐条一致**。

- [ ] **Step 3: 确认 clang-format-21 可用且当前无格式问题**

```bash
clang-format-21 --version && \
find . -maxdepth 3 -regex '.*0[1-4]_.*' \( -name "*.cpp" -o -name "*.hpp" \) \
  | xargs clang-format-21 --dry-run --Werror 2>&1 | tail -5
```

Expected: 版本号含 `21.`；格式检查无输出（全部通过）。若版本不是 21，**停止**——
CLAUDE.md 明确 v18/v19/v20 在中文列宽上与 CI 不一致，本地过不代表 CI 过。

---

## Phase 1：标准文件落地

### Task 1: 替换 CLAUDE.md 与 AGENTS.md 的注释两节

**Files:**
- Modify: `CLAUDE.md:79-94`（「Documentation & Comments」+「Evolutionary Comment Pattern」两节）
- Modify: `AGENTS.md:70-85`（同样两节，内容逐字相同）

**背景**：两文件当前这两节**内容完全一致**（已 diff 确认），故替换文本相同。
CLAUDE.md 是单一事实源，AGENTS.md 是给 Codex 的副本。

- [ ] **Step 1: 替换 CLAUDE.md 的两节**

把 `CLAUDE.md` 第 79–94 行（从 `### Documentation & Comments（注释与文档规范）`
到 `### File Header（文件头模板）` **之前**的全部内容）整体替换为：

```markdown
### Documentation & Comments（注释与文档规范，W16 起）

- **语言**：注释一律简体中文（英文标识符除外）；读者是几个月后的自己——
  不重读实现就能重建上下文。
- **函数头（语法用行业标准，不自造格式）**：
  - C++：Doxygen `///`。一句话 brief 必写（类 + 非平凡函数，含 cpp 内部）；
    `@param`/`@return` 仅语义不显然时补；坑用标准标签——`@pre`（前置条件）、
    `@warning`（陷阱/失效条件）、`@note`（其他注意）。
    平凡函数豁免（getter / 单行转发 / 命名即全部语义）。
  - Python：Google 风格 docstring，同样一句话起步。
- **函数体内注释只写两种**：意图（为什么这样做、魔法数字出处）与复杂算法
  关键步骤；体内的坑统一 `// 注意：` 前缀。禁止翻译代码式注释；
  改代码必须同步改注释。
- **位置绑定**：模块级叙事（整体设计、踩坑复盘、基准数据）只在 notes.md 一份，
  注释不重复正文，需要时一行指针（如「基准方法见 notes.md §噪声判定」）。
- **陷阱检索**：`grep -rnE "@(pre|warning|note)|注意：" <模块>/`
- **测试代码**：同标准；TEST 宏体不要求头注释（测试名即描述）。
- 文件头模板（SPDX + 一句话功能）不变，见下节。

### 演进式注释（W1–W15 存档专属）

`[Legacy]/[Pain Point]/[Modern]` 三段式仅存在于 W1–W15 存档模块，冻结不改、
不再新增。W16 起若确需说明新旧取舍，一行「为什么用 X 而非 Y」写进意图注释即可。
```

- [ ] **Step 2: 用同样的文本替换 AGENTS.md 第 70–85 行**

范围同样是「Documentation & Comments」+「Evolutionary Comment Pattern」两节，
替换为 Step 1 的**同一段文本**（两文件保持逐字一致）。

- [ ] **Step 3: 验证两文件该节一致**

```bash
diff <(sed -n '/^### Documentation & Comments/,/^### File Header/p' CLAUDE.md) \
     <(sed -n '/^### Documentation & Comments/,/^### File Header/p' AGENTS.md)
```

Expected: 无输出（两份逐字相同）。

- [ ] **Step 4: 确认旧格式已无残留引用**

```bash
grep -n "Evolutionary Comment Pattern" CLAUDE.md AGENTS.md
```

Expected: 无输出（小节标题已改为「演进式注释（W1–W15 存档专属）」）。

### Task 2: 对齐 google_style_guide.md 与 format_project.md

**Files:**
- Modify: `.agent/rules/google_style_guide.md:48-51`（§4 注释规范）
- Modify: `.agent/workflows/format_project.md:45-58`（§4 演进式注释检查）

- [ ] **Step 1: 追加指向 CLAUDE.md 的说明到 google_style_guide.md §4**

把 `.agent/rules/google_style_guide.md` 第 48–51 行替换为：

```markdown
## 4. 注释规范
- 仅使用 `//` 进行单行注释，不建议使用 `/* */`（Doxygen 文档注释 `///` 属单行注释，允许）。
- 每个文件开头必须包含版权声明和文件功能描述。
- 每个类和非平凡函数前必须有功能描述注释。
- **W16 起**：注释规范以 `CLAUDE.md`「Documentation & Comments」节为准——
  C++ 用 Doxygen `///` 一句话 brief，坑用 `@pre`/`@warning`/`@note` 标准标签，
  Python 用 Google 风格 docstring。演进式三段注释仅限 W1–W15 存档模块。
```

- [ ] **Step 2: 把 format_project.md §4 限定为存档场景**

把 `.agent/workflows/format_project.md` 第 45–58 行替换为下面的内容。
（**注意**：下面用四个反引号包裹，是因为内容本身含 ```` ```cpp ```` 代码块；
写进文件时只保留内层内容，不要把四反引号写进去。）

````markdown
### 4. 演进式注释检查（仅限 W1–W15 存档模块）

**适用范围**：仅 `01_Linux_CPP_Foundations/w1..w15` 与 `02_Inference_Analysis/w14`、`w15`
存档模块。W16 起的模块**跳过本节**，按 `CLAUDE.md`「Documentation & Comments」节执行
（Doxygen `///` brief + `@pre`/`@warning`/`@note` 坑标签）。

触发条件（存档模块内）：任何从旧标准（C++11/14/17）向新标准（C++20/23）转换的代码块。

在关键代码上方必须插入以下三行演进注释（用中文填写尖括号内容）：

```cpp
// [Legacy C++11/17]: <描述旧版实现方式，如：手动 join std::thread>
// [Pain Point]: <说明旧版的痛点，如：忘记 join 导致程序崩溃>
// [Modern C++20/23]: <说明新特性如何解决，如：std::jthread RAII 自动汇合>
```

格式化代码后，在对应模块的 `notes.md` 中同步"技术演进复盘"小节，
总结新旧写法的核心差异。
````

- [ ] **Step 3: 确认四个标准文件口径一致**

```bash
grep -rn "W1–W15\|W16 起" CLAUDE.md AGENTS.md .agent/rules/google_style_guide.md .agent/workflows/format_project.md | head
```

Expected: 四个文件都出现范围界定；无文件仍无条件要求演进式注释。

- [ ] **Step 4: Commit**

```bash
git add CLAUDE.md AGENTS.md .agent/rules/google_style_guide.md .agent/workflows/format_project.md
git commit --author="hanchunbo <hanchunbo@users.noreply.github.com>" -m "docs(rules): 注释标准换代——W16 起 Doxygen 轻量语法，演进式限定 W1–W15 存档

- 三类必写：函数头 brief（类+非平凡函数）、意图（魔法数字出处）、坑（@pre/@warning/@note）
- Python 用 Google 风格 docstring；位置绑定原则：叙事留 notes.md，注释只留行级事实
- 四处标准文件同步：CLAUDE.md（事实源）/ AGENTS.md / google_style_guide §4 / format_project §4

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Phase 2：quant 回填（本批重点）

**为什么 quant 是重点**：9 个 C++ 文件约 900 行仅约 30 行注释，且 `quant_benchmark.cpp`
307 行只有 3 行（SPDX 文件头）。决策密度却是全仓最高——检测头保 FP32、噪声阈值、
Entropy 退化、内存墙都在这里。

**注释事实来源**（回填时必须查证，不得自行编造理由）：
- `02_Inference_Analysis/quantization/notes.md`（尤其 §INT8 0 检测框根因与修复、§当前边界）
- `docs/benchmarks/quant_int8_report.md`、`docs/benchmarks/quant_yolo_hardening.md`
- `docs/notes/inference.md`（IOBinding 坑①~④、INT8 PTQ、MinMax/Entropy）

### Task 3: rolling_stats.{hpp,cpp}

**Files:**
- Modify: `02_Inference_Analysis/quantization/rolling_stats.hpp`
- Modify: `02_Inference_Analysis/quantization/rolling_stats.cpp`

- [ ] **Step 1: 给 rolling_stats.hpp 的类型与成员函数加 brief**

在对应声明上方插入（`Count()`/`WindowSize()` 是平凡 getter，按标准豁免，不加）：

```cpp
/// 单次检测的分段延迟（毫秒）：预处理 / 推理 / 后处理 / 端到端。
struct StageLatencyMs {

/// 单个阶段的分位数统计（毫秒）。
struct PercentileStats {

/// 四个阶段各自的 P50/P99 + 当前窗口样本数。
struct StageLatencyStats {

/// 固定窗口的滚动延迟统计：只保留最近 window_size 个样本，超出淘汰最旧的。
/// 窗口固定使长跑 benchmark 内存有界，且分位数反映近期状态而非全历史。
class RollingStats {
```

成员函数上方：

```cpp
  /// @throws std::invalid_argument window_size 为 0
  explicit RollingStats(std::size_t window_size = 128);

  /// 追加一个样本；窗口已满时先淘汰最旧的。
  void Add(const StageLatencyMs& sample);

  /// 汇总当前窗口内各阶段的 P50/P99。
  /// @note 空窗口返回全 0（count=0），不是错误——调用方靠 count 区分
  [[nodiscard]] StageLatencyStats Summary() const;
```

- [ ] **Step 2: 给 rolling_stats.cpp 的 Percentiles 加 brief + 意图**

替换 `Percentiles` 函数上方（第 17 行前）为：

```cpp
/// 用最近秩法（ceil(q*n)）取分位数。
///
/// 不做线性插值：插值会造出实际没观测到的延迟值，而基准报告要的是真实发生过的
/// 样本。代价是小样本下分辨率有限——窗口 128 时 P99 恒等于第 127 个样本。
/// @note 按值传入 values：内部要排序，不能改动调用方数据
PercentileStats Percentiles(std::vector<double> values) {
```

在函数体内 `clamp` 那行上方加：

```cpp
    // 注意：clamp 保证 rank 落在 [1, n]——q=0 或浮点误差会让 rank 取到 0，
    // 减 1 后下标回绕成巨大值（size_t 无符号）。
```

- [ ] **Step 3: 验证格式与构建**

```bash
clang-format-21 --dry-run --Werror \
  02_Inference_Analysis/quantization/rolling_stats.{hpp,cpp} && \
cmake --build build --target quant_rolling_stats_test 2>&1 | tail -3
```

Expected: 格式检查无输出；构建成功。

- [ ] **Step 4: 验证只改了注释**

```bash
git diff -U0 -- 02_Inference_Analysis/quantization/rolling_stats.hpp \
  02_Inference_Analysis/quantization/rolling_stats.cpp \
  | grep -E '^[+-]' | grep -vE '^(\+\+\+|---)' | grep -vE '^[+-]\s*(//|$)'
```

Expected: **无输出**——所有增删行都是注释行。有输出说明动到了代码，必须回退。

### Task 4: eval_harness.{hpp,cpp}

**Files:**
- Modify: `02_Inference_Analysis/quantization/eval_harness.hpp`
- Modify: `02_Inference_Analysis/quantization/eval_harness.cpp`

- [ ] **Step 1: 给 eval_harness.hpp 加 brief 与契约事实**

```cpp
/// 一个待评估模型：报告里显示的名字 + ONNX 路径。
struct ModelCase {

/// 评估参数：检测器配置 + warmup/正式迭代次数 + 统计窗口。
struct EvalConfig {

/// 单模型评估结果：EP 实况 + 最后一次检测概况 + 延迟分位数。
struct ModelResult {
```

`ModelResult` 的两组字段上方分别加（这两条是**报告正确性**的关键，不是废话）：

```cpp
  /// 实际生效的 EP 与回退原因。
  /// @warning 请求 CUDA 但环境无 GPU provider 时会静默回退 CPU——报告必须打印
  /// ep_fallback_reason，否则会把 CPU 数字当成 GPU 数字读
  w14::Ep active_ep = w14::Ep::kCpu;
  std::string ep_fallback_reason;

  /// 最后一次迭代的检测概况，仅用于 sanity check（确认模型没输出 0 框）。
  /// @warning 不是精度指标——INT8 精度看 mAP（tools/eval_map_coco128.py）与
  /// Quant_Int8ConsistencyTest 的框级比对
  std::size_t detection_count = 0;
  float top_score = 0.0f;
```

类与成员：

```cpp
/// 多模型评估 harness：把每个 ModelCase 放进同一套 W16 检测流水线，
/// 统一 warmup、迭代次数与统计口径，使 FP32/INT8 的延迟可直接对比。
class EvalHarness {
 public:
  /// @throws std::invalid_argument warmup<0 / iters<=0 / stats_window<=0
  explicit EvalHarness(EvalConfig config);

  /// 逐个模型跑 warmup + iters 次计时迭代。
  /// @return 与 cases 等长且同序的结果
  [[nodiscard]] std::vector<ModelResult> Run(
      const std::vector<ModelCase>& cases, const std::string& image_path) const;
```

- [ ] **Step 2: 给 eval_harness.cpp 加 brief**

```cpp
/// W16 分段计时 → 滚动统计样本的字段映射。
StageLatencyMs ToStageLatency(const w16::DetectionTiming& timing) {
```

构造函数与 Run 的实现上方**不重复** hpp 已有的 brief（位置绑定原则：契约写在声明处一份）。
`Run()` 内第 42 行已有的 warmup 注释保持不动——它已符合新标准。

- [ ] **Step 3: 验证**

```bash
clang-format-21 --dry-run --Werror \
  02_Inference_Analysis/quantization/eval_harness.{hpp,cpp} && \
cmake --build build --target quant_eval_harness_test 2>&1 | tail -3 && \
git diff -U0 -- 02_Inference_Analysis/quantization/eval_harness.hpp \
  02_Inference_Analysis/quantization/eval_harness.cpp \
  | grep -E '^[+-]' | grep -vE '^(\+\+\+|---)' | grep -vE '^[+-]\s*(//|$)'
```

Expected: 格式通过、构建成功、最后一条 grep 无输出。

### Task 5: quant_benchmark.cpp（注释最少、决策最多的文件）

**Files:**
- Modify: `02_Inference_Analysis/quantization/quant_benchmark.cpp`

- [ ] **Step 1: 给 4 个常量补出处（第 24–27 行）**

替换为：

```cpp
constexpr int kInput = 640;   // YOLOv8n 导出时固定的方形输入边长
constexpr int kWarmup = 5;    // 丢弃冷启动/CPU cache/CUDA autotuning 的前几次
constexpr int kIters = 20;    // 每个 (模型×EP×模式) 组合的计时迭代数

// IOBinding 收益的判定阈值（百分比）：低于此值一律报「噪声区间」而非收益。
// 依据：W16 与 quant 多轮实测中 Run 与 IOBinding 的 P50 优劣会翻转（yolov8n
// CUDA batch=1 在 5.5~6.1ms 间抖动），把抖动当优化结论是这里最大的风险。
// 详见 docs/benchmarks/w16_yolo_bench.md 与 quant_int8_report.md。
constexpr double kNoisePct = 3.0;
```

- [ ] **Step 2: 给 4 个结构体加 brief（第 29–52 行）**

```cpp
/// 一次纯 ORT infer 基准的延迟统计。
struct InferStat {

/// 纯 ORT infer 表格的一行：模型 × 请求/实际 EP × Run|IOBinding 模式。
struct InferRow {

/// 端到端流水线的一组硬化配置（W16 default vs quant hardened 对照）。
struct PipelineConfig {
```

- [ ] **Step 3: 给各函数加 brief 与坑标签**

```cpp
/// EP 枚举 → 报告用短名。
[[nodiscard]] const char* EpName(w14::Ep ep) {

/// 从模型路径取报告用名字；第一个模型固定叫 fp32（约定它是基线）。
[[nodiscard]] std::string ModelName(const std::string& path,

/// 解析命令行：argv[2..] 为模型路径列表，缺省时只跑 W16 的 FP32 基线。
[[nodiscard]] std::vector<quant::ModelCase> ParseCases(int argc, char** argv) {

/// 最近秩法分位数（与 RollingStats 同口径，见 rolling_stats.cpp）。
[[nodiscard]] double Percentile(std::vector<double> values, double q) {

/// 跑一次推理并丢弃输出，仅用于计时。
/// @throws std::runtime_error ORT 没有返回输出
void RunOnce(w14::InferenceEngine& engine, const std::vector<float>& input,

/// 对单个 engine 测纯推理延迟：kWarmup 次预热 + kIters 次计时。
/// @note 只覆盖 ORT Run 本身，不含预处理/解码——与端到端表分开看
[[nodiscard]] InferStat BenchInfer(w14::InferenceEngine& engine,

/// 预处理成 NCHW 输入张量（BGR→RGB + letterbox + /255）。
/// @note 与 W16 detector 内部同一套变换，此处直接喂 engine 是为了让「纯 ORT
/// infer」段绕开检测器编排，测的就是推理本身
[[nodiscard]] std::vector<float> PrepareInput(const cv::Mat& bgr) {

/// 打印 CUDA 被静默回退到 CPU 的行——不打印会让读者把 CPU 数字当 GPU 数字。
void PrintFallbackNotice(const std::vector<InferRow>& rows) {

/// 按 (模型, EP) 配对 Run vs IOBinding，打印差异与噪声判定。
/// @warning 依赖 rows 的排列顺序：每个 (模型×EP) 必须是连续两行、且
/// Run 在前 IOBinding 在后（见 main 的循环嵌套）。改了 main 的循环顺序，
/// 这里的配对会静默失配——有 continue 兜底，表现为少打行而不是报错
void PrintIoBindingDelta(const std::vector<InferRow>& rows) {
```

- [ ] **Step 4: 给 main 的硬化配置加意图（第 253–264 行 pipeline_configs 上方）**

```cpp
  // 两组配置的差异即「部署硬化」的全部内容：IOBinding 走 W14 RunIoBinding、
  // 解码跳过 NaN/Inf、预留候选容量、限制 NMS 输出上限。
  // 512/300 对齐 ultralytics 默认量级（max_det=300）；硬化项对精度无损，
  // 只影响稳定性与内存行为，故与 W16 default 同图对照。
  const std::vector<PipelineConfig> pipeline_configs{
```

- [ ] **Step 5: 验证**

```bash
clang-format-21 --dry-run --Werror 02_Inference_Analysis/quantization/quant_benchmark.cpp && \
cmake --build build --target quant_benchmark 2>&1 | tail -3 && \
git diff -U0 -- 02_Inference_Analysis/quantization/quant_benchmark.cpp \
  | grep -E '^[+-]' | grep -vE '^(\+\+\+|---)' | grep -vE '^[+-]\s*(//|$)'
```

Expected: 格式通过、构建成功、grep 无输出。

### Task 6: 四个测试文件

**Files:**
- Modify: `02_Inference_Analysis/quantization/rolling_stats_test.cpp`
- Modify: `02_Inference_Analysis/quantization/eval_harness_test.cpp`
- Modify: `02_Inference_Analysis/quantization/batch_consistency_test.cpp`
- Modify: `02_Inference_Analysis/quantization/int8_consistency_test.cpp`

**标准提醒**：TEST 宏体不要求头注释（测试名即描述）；辅助函数与非显然的常量要写。

- [ ] **Step 1: eval_harness_test.cpp / batch_consistency_test.cpp 的辅助函数**

```cpp
/// 缺模型或测试图时跳过用例——CI 与裸克隆环境没有这些产物。
void SkipIfAssetsMissing() {

/// 把单图张量复制 batch 份，拼成 [batch,3,H,W] 输入。
std::vector<float> TileBatch(const std::vector<float>& one, int batch) {
```

- [ ] **Step 2: batch_consistency_test.cpp 的容差常量（第 22–25 行）**

```cpp
constexpr int kInput = 640;
constexpr int kBatch = 4;
// 同一份输入在 batch=1 与 batch=4 下应逐位可比：差异只可能来自 ORT 内部的
// 并行归约顺序，故容差取浮点级而非精度级——超出即说明 batch 切片错位。
constexpr float kScoreTol = 1e-5f;
constexpr float kCoordTol = 1e-4f;
```

- [ ] **Step 3: int8_consistency_test.cpp 的阈值常量（第 21–23 行）**

现有行内注释已说明「是什么」，补的是「为什么是这个值」。替换为：

```cpp
// 只强约束 FP32 的高置信框：低分框在量化后本就允许抖动，全量比对会假红。
constexpr float kStrongScore = 0.5f;
// 匹配框的最小 IoU / 允许的分数偏差：INT8 是有损的，判据是「同一个目标仍被
// 检出且框基本重合」，不是数值相等。阈值来自修复后实测留的余量，
// 根因与修复见 notes.md §INT8 0 检测框根因与修复。
constexpr float kMinMatchIou = 0.7f;
constexpr float kMaxScoreDiff = 0.15f;
```

- [ ] **Step 4: int8_consistency_test.cpp 的辅助函数（第 25、52 行）**

```cpp
/// 缺产物时跳过：INT8 模型需先跑 quant_yolov8_static 生成。
void SkipIfMissing(const std::string& path, const char* what) {

/// 断言 INT8 结果覆盖 FP32 的每个高置信框：同类别、IoU≥kMinMatchIou、
/// 分数差≤kMaxScoreDiff。
/// @note 这是检测头保 FP32 修复的回归闸门——修复前 INT8 输出 0 框，本断言红
void ExpectInt8MatchesFp32(const std::string& int8_model_path) {
```

- [ ] **Step 5: 验证**

```bash
clang-format-21 --dry-run --Werror 02_Inference_Analysis/quantization/*_test.cpp && \
cmake --build build --target quant_rolling_stats_test quant_eval_harness_test \
  quant_batch_consistency_test quant_int8_consistency_test 2>&1 | tail -3 && \
git diff -U0 -- 02_Inference_Analysis/quantization/ \
  | grep -E '^[+-]' | grep -vE '^(\+\+\+|---)' | grep -vE '^[+-]\s*(//|$)'
```

Expected: 格式通过、构建成功、grep 无输出。

### Task 7: tools/*.py（Google 风格 docstring）

**Files:**
- Modify: `02_Inference_Analysis/quantization/tools/quantize_yolov8_static.py`
- Modify: `02_Inference_Analysis/quantization/tools/eval_map_coco128.py`

- [ ] **Step 1: quantize_yolov8_static.py 的模块级 docstring**

在第 4 行文件功能注释之后、`from __future__` 之前插入：

```python
"""ORT static PTQ 生成 YOLOv8n 的 INT8 QDQ 模型。

默认排除检测头 `/model.22/`（154 节点保 FP32）：整图量化时 YOLOv8 输出张量
(1,84,8400) 混合框坐标(0~640)与类别分数(0~1)，Concat 输出的 per-tensor
scale≈2.5 会把所有分数压成 0，检测框全丢——与校准集大小无关。
根因与修复见 ../notes.md §INT8 0 检测框根因与修复。

内存约束：Entropy 校准在内存中累积全部中间层输出做直方图，32 图峰值 RSS 约
5.24GB。本机 WSL 仅 7.7GB，必须包内存墙跑（裸跑曾多次打崩 WSL VM）：

    systemd-run --user --scope -p MemoryMax=5G -p MemorySwapMax=4G <cmd>

被杀时降 --calib-limit，不要提高内存上限。
"""
```

- [ ] **Step 2: 各函数的 Google 风格 docstring**

```python
def parse_args() -> argparse.Namespace:
    """解析命令行参数。"""


def import_runtime_deps():
    """惰性导入量化依赖，缺失时给出可执行的安装提示。

    Returns:
        dict: 依赖名 → 模块/类的映射。

    Note:
        写成惰性导入是为了让 --help 在没装 onnxruntime 的环境也能跑通。
    """


def collect_images(calib_images: Iterable[str], calib_dirs: Iterable[str]) -> list[Path]:
    """汇总校准图片路径（去重前，按目录名排序）。

    Args:
        calib_images: --calib-image 传入的单图路径。
        calib_dirs: --calib-dir 传入的目录，递归收集受支持的图片后缀。

    Returns:
        校准图路径列表。

    Raises:
        SystemExit: 路径不存在，或最终一张图都没收到。
    """


def letterbox_rgb(image_rgb, size: int, cv2, np):
    """等比缩放 + 灰边(114)填充到 size×size。

    Note:
        必须与 C++ 侧 w10::LetterboxToTensor 保持同一套变换——校准数据的分布
        要和推理时一致，否则激活范围偏移，量化 scale 就选错了。
    """


def preprocess_image(path: Path, size: int, cv2, np):
    """读图并预处理成 NCHW float32 张量（BGR→RGB + letterbox + /255）。"""


def make_reader_class(calibration_data_reader_base):
    """构造 CalibrationDataReader 子类（懒加载逐张预处理）。

    Note:
        懒加载不是风格选择：一次性预处理全部校准图会让 RSS 再涨一截，
        叠加 Entropy 的直方图累积会直接打爆 WSL。见模块 docstring 的内存约束。
    """


def main() -> int:
    """生成 MinMax 与 Entropy 两个 INT8 模型。

    Returns:
        进程退出码，0 表示成功。

    Warning:
        ORT `quantize_static` 默认参数下 Entropy 实测退化为 MinMax——本模块两个
        INT8 产物字节级相同（2026-07-11 勘误）。产出两个文件是为了保留对比接口，
        不代表两种校准策略真的生效。见 ../notes.md §概念边界速查。
    """
```

- [ ] **Step 3: eval_map_coco128.py 的 docstring**

```python
def parse_args() -> argparse.Namespace:
    """解析命令行参数。"""


def main() -> int:
    """用 ultralytics model.val 在 coco128 上评 mAP。

    Returns:
        进程退出码，0 表示成功。

    Note:
        逐张推理，不吃内存——与 quantize_yolov8_static.py 的 Entropy 校准内存墙无关。
    """
```

- [ ] **Step 4: 验证脚本仍可运行**

```bash
.venv/bin/python 02_Inference_Analysis/quantization/tools/quantize_yolov8_static.py --help | head -3 && \
.venv/bin/python 02_Inference_Analysis/quantization/tools/eval_map_coco128.py --help | head -3
```

Expected: 两个脚本都打印 usage（docstring 不影响 argparse）。若 `.venv` 不存在，
改用 `python3` 亦可——`--help` 路径不 import onnxruntime。

- [ ] **Step 5: 按 spec 验收标准逐条验证 quant**

**验收①——函数头覆盖**（spec 标准 1）。列出「定义行的上一行不是注释」的函数：

```bash
awk '/^(\[\[nodiscard\]\] )?(void|int|double|bool|float|std::|const char\* |PercentileStats |StageLatencyMs |InferStat )[A-Za-z_:<>*& ]*\(/ && prev !~ /^[[:space:]]*\/\// {print FILENAME":"NR": "$0} {prev=$0}' \
  02_Inference_Analysis/quantization/*.cpp 02_Inference_Analysis/quantization/*.hpp
```

Expected: 仅剩平凡函数（`Count()`/`WindowSize()` 等 getter）与多行签名的续行误报。
逐条确认每个命中要么已豁免、要么补上 brief。（此检查是粗筛，不能替代通读。）

**验收②——陷阱可检索**（spec 标准 2）：

```bash
grep -rnE "@(pre|warning|note)|注意：" 02_Inference_Analysis/quantization/ | wc -l
```

Expected: 明显大于 0（预期 15+ 条）。再逐条比对 `notes.md` 的已知坑是否都有落点：
检测头保 FP32、Entropy 默认退化为 MinMax、Entropy 校准内存墙、EP 静默回退、
IOBinding 噪声级。**缺哪条补哪条**。

**验收③⑤——人工通读**（spec 标准 3、5，无法机械化）：通读本阶段 diff，确认
(a) 没有「翻译代码」式注释（如「// 遍历所有样本」）；(b) 注释没有复制 notes.md 的
叙事段落，只留一行指针。发现即删改。

- [ ] **Step 6: 跑测试，与基线比对**

```bash
ctest --test-dir build -R "Quant_" --output-on-failure 2>&1 | tail -8
```

Expected: 与 Task 0 Step 2 记录的基线**逐条一致**（注释改动不该动任何测试结果；
若某条由绿变红，说明误删/误改了代码，回退重来）。

- [ ] **Step 7: Commit quant 回填**

```bash
git add 02_Inference_Analysis/quantization/
git commit --author="hanchunbo <hanchunbo@users.noreply.github.com>" -m "docs(quant): 按新标准回填注释——决策出处、坑标签、Python docstring

- 常量补出处：kNoisePct 3% 噪声判定依据、INT8 一致性阈值为何不是数值相等
- 坑标签：EP 静默回退、PrintIoBindingDelta 依赖行序、Entropy 默认退化为 MinMax
- Python：Google 风格 docstring + 检测头保 FP32 根因、Entropy 校准内存墙
- 纯注释改动，无逻辑变更

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Phase 3：W16 语法对齐（范围已缩小）

**读过代码后的实情**：W16 内容层面已达新标准（decode.hpp 讲清了张量布局/坐标反算/
clamp 理由，yolo_detector.hpp 讲清了组合而非继承，yolo_detector.cpp 有 BGR→RGB 理由
与分步）。**既有散文一律不重写**，只做两件事：补缺失的 struct brief、把已用散文
表达的错误条件/陷阱提为标准标签。

### Task 8: 四个 hpp 的 brief 与标签

**Files:**
- Modify: `02_Inference_Analysis/w16_yolo_detector/detection.hpp`
- Modify: `02_Inference_Analysis/w16_yolo_detector/decode.hpp`
- Modify: `02_Inference_Analysis/w16_yolo_detector/nms.hpp`
- Modify: `02_Inference_Analysis/w16_yolo_detector/yolo_detector.hpp`

- [ ] **Step 1: 补缺 brief 的 struct（这些当前完全没有头注释）**

```cpp
// detection.hpp —— Detection 的字段级注释已有，补类型 brief：
/// 单个检测框：原图坐标系下的 xyxy + 置信度 + 类别。
struct Detection {

// decode.hpp：
/// 解码可选项：置信度阈值 + 部署硬化项（NaN/Inf 跳过、候选预留）。
struct DecodeOptions {

// yolo_detector.hpp：
/// 一次检测的分段耗时（毫秒）。不含文件读取，只覆盖已解码图像的三段。
struct DetectionTiming {

/// 检测结果 + 本次分段耗时。
struct DetectionResult {
```

- [ ] **Step 2: 把已有散文里的错误条件提为标签（不改散文本身）**

`decode.hpp` 第 41–42 行现有散文「out.size() 必须等于 (4 + num_classes) *
num_anchors，否则抛 std::invalid_argument」——保留该行，在其后补一行标签：

```cpp
// @throws std::invalid_argument out.size() 与 (4+num_classes)*num_anchors 不符，
//         或 num_classes/num_anchors/img_w/img_h 非正、conf_thresh 越界、scale 非正有限数
```

`yolo_detector.hpp` 第 53 行现有「读取失败抛 std::runtime_error」保留，补：

```cpp
  // @throws std::runtime_error 图片读取失败
```

`nms.hpp` 的 `Nms` 声明上方补：

```cpp
// @throws std::invalid_argument iou_thresh 越界或 max_det < 0
```

- [ ] **Step 3: 验证 W16 未破坏 + 测试仍绿**

```bash
clang-format-21 --dry-run --Werror 02_Inference_Analysis/w16_yolo_detector/*.hpp && \
cmake --build build --target w16_decode_test w16_nms_test w16_yolo_detector_test 2>&1 | tail -3 && \
ctest --test-dir build -R "W16_" --output-on-failure 2>&1 | tail -5
```

Expected: 格式通过、构建成功、测试结果与 Task 0 基线一致。

- [ ] **Step 4: 验证只改注释 + 提交**

```bash
git diff -U0 -- 02_Inference_Analysis/w16_yolo_detector/ \
  | grep -E '^[+-]' | grep -vE '^(\+\+\+|---)' | grep -vE '^[+-]\s*(//|$)'
```

Expected: 无输出。

```bash
git add 02_Inference_Analysis/w16_yolo_detector/
git commit --author="hanchunbo <hanchunbo@users.noreply.github.com>" -m "docs(w16): 注释对齐新标准——补 struct brief 与 @throws 标签

W16 内容层面已合规（张量布局/坐标反算/组合决策均已写明），故仅补缺失的
类型 brief，并把散文里已述的错误条件提为 @throws 标签，既有散文不重写。

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Phase 4：收口

### Task 9: 收口验证与推送

**Files:** 无（只读验证 + push）

- [ ] **Step 1: 确认三个 commit 的范围符合预期**

```bash
git log --oneline -3 && git diff --stat HEAD~3..HEAD
```

Expected: 三个 commit（标准文件 / quant / W16）；diff 只涉及计划列出的文件。

**注意**：本计划不改 README.md / Roadmap.md 进度节——注释规范不是交付物里程碑，
`quant` 与 `trt` 的状态描述不受影响。CLAUDE.md 提交检查清单提到的 `docs/tech-debt.md`
**已归档到 `docs/archive/tech-debt.md`**（CLAUDE.md 的路径引用已过时，属既有问题，
不在本计划范围内改）。

- [ ] **Step 2: 全仓格式终检（CI 同款命令）**

```bash
find . -maxdepth 3 -regex '.*0[1-4]_.*' \( -name "*.cpp" -o -name "*.hpp" \) \
  | xargs clang-format-21 --dry-run --Werror
```

Expected: 无输出。

- [ ] **Step 3: 推送 dev**

```bash
git push origin dev
```

**注意**：按 CLAUDE.md「Branch & Merge Policy」，合入 main **必须先问用户**，
本计划不含合 main 步骤。

- [ ] **Step 4: 向用户汇报并请示 trt**

汇报三个 commit 的范围与验收结果，并说明 trt 回填未做（用户已明确押后），
询问是否现在另起 trt 计划。

---

## 不在本计划范围

- **trt 模块回填**（10 文件约 810 行）：用户明确要求 W16/quant 收口后再做。
  spec §5 保留其要点（补函数头覆盖 + TRT 10 API 生命周期、engine 缓存戳约束的坑标签）。
  本批收口后按同样模式另起计划——届时应先实读 trt 代码，避免重蹈 W16 这次
  「假设与实情不符」的覆辙。
- **W1–W15 存档**：冻结，不回填。
- **Doxygen 文档生成**（Doxyfile / CI 步骤）：只用语法层，不建生成流程。
- **notes.md 正文改动**：分工不变，本计划只在注释侧加指针。
