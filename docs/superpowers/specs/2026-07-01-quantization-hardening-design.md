# Quant — 基于 W16 的 YOLO 部署硬化与 INT8 评估设计文档

> 设计状态：2026-07-01 review 后修订。用户确认：允许对 W16 做**向后兼容的加性扩展**，
> quant 不再复制一套 YOLO core，而是在 W16 基线上做部署硬化、运行时统计和 INT8 评估 harness。

## 1. 目标

进入 Phase 0 的 `quant` 交付物时，不重新实现一套 YOLO 检测流水线，而是把 W16 已完成的
YOLOv8n demo 推进到更接近部署评估的形态：

- W16 继续作为检测能力基线：预处理、ORT 推理、YOLOv8 解码、NMS、对拍 ultralytics。
- W16 只做向后兼容加性硬化：新增可选配置，不破坏现有 `W16_*` 测试和历史 benchmark。
- quant 模块专注部署评估 harness：运行时滚动统计、FP32/INT8 模型切换、batch consistency、
  benchmark/report 输出。

核心产出不是“再写一个 YOLO”，而是形成一条可调参、可观测、可量化对比的 C++ 部署评估路径。

## 2. 与 W16 的差异

W16 已经具备：

- YOLOv8n 端到端检测 pipeline。
- `decode` / `nms` 纯 C++ core。
- 坐标反算 + clamp。
- CPU / CUDA EP 对比。
- `Run` vs `IOBinding` benchmark。
- batch=1 / batch=4 纯推理 benchmark。
- 端到端 `pre / infer / post / total` 一次性 benchmark。
- IntraOp 线程扫描。
- ultralytics 单图对拍。

quant 新增价值集中在：

| 维度 | W16 现状 | quant 目标 |
|---|---|---|
| 后处理鲁棒性 | decode 已 clamp，但无 NaN/Inf 防护；NMS 无 `max_det` | W16 core 加可选 `DecodeOptions` / `NmsOptions` |
| 部署配置 | W14 线程 / IOBinding 只在 benchmark 用 | W16 `DetectorConfig` 加性透传线程与 IOBinding |
| 运行时统计 | benchmark 一次性测量 | quant harness 维护滚动 P50/P99 |
| CUDA 回退 | 可用 `ActiveEp()` / `EpFallbackReason()` 查 | quant demo/benchmark 显式打印告警 |
| batch 正确性 | batch=4 只证明能跑 | quant 补 batch 输出切片一致性测试 |
| 量化评估 | 无 FP32/INT8 对比入口 | quant 以 model path 切换 FP32/INT8，共用同一 pipeline |
| 报告定位 | W16 工程快测 | quant 部署硬化 + INT8 量化报告 |

## 3. 范围决策

### 3.1 用户已确认的边界

- **C++ 工程优先**：先把部署评估路径做扎实，再接 INT8 量化工具。
- **允许 W16 加性扩展**：不再机械遵守“冻结=完全不能动”，但只能做向后兼容扩展。
- **不做 W16 语义重构**：保留 W16 API、测试、notes 的历史结论。
- **正式检测 API 仍以 batch=1 为主**：batch=4 只进入 benchmark / consistency test。
- **技术债路径**：技术债文档实际位于 `docs/archive/tech-debt.md`。

### 3.2 不在本阶段做

- 不做 TensorRT engine、FP16 路径、TRT INT8 calibrator。
- 不做 GPU 端 letterbox / decode / NMS，也不做 ORT 图融合。
- 不把 W14-W16 大规模抽成新的公共 inference/detect 库。
- 不实现剪枝 / 蒸馏，只在文档中做概念覆盖与定位区分。

这些内容分别归 `trt` 或后续弹性深化，避免 `quant` 被后端优化和重构拖散。

## 4. W16 加性扩展设计

### 4.1 `DecodeOptions`

在 `02_Inference_Analysis/w16_yolo_detector/decode.hpp` 中新增 options overload，保留旧函数签名：

```cpp
struct DecodeOptions {
  float conf_thresh = 0.25f;
  bool skip_non_finite = false;
  int reserve_hint = 0;
};

[[nodiscard]] std::vector<Detection> DecodeYolov8(
    std::span<const float> out, int num_classes, int num_anchors,
    const DecodeOptions& options, float scale, int pad_left, int pad_top,
    int img_w, int img_h);
```

旧接口继续存在，并委托到新接口：

```cpp
[[nodiscard]] std::vector<Detection> DecodeYolov8(
    std::span<const float> out, int num_classes, int num_anchors,
    float conf_thresh, float scale, int pad_left, int pad_top,
    int img_w, int img_h);
```

行为约束：

- 默认 `skip_non_finite=false`，保持 W16 历史行为。
- quant 路径设置 `skip_non_finite=true`，跳过坐标、宽高、score 中含 NaN/Inf 的候选。
- `reserve_hint > 0` 时按 `min(reserve_hint, num_anchors)` 预留候选容量。
- 参数非法时抛 `std::invalid_argument`，Release 下同样生效。

### 4.2 `NmsOptions`

在 `nms.hpp` 中新增 options overload，保留旧函数签名：

```cpp
struct NmsOptions {
  float iou_thresh = 0.45f;
  int max_det = 0;  // 0 表示不限制，保持 W16 历史行为。
};

[[nodiscard]] std::vector<Detection> Nms(std::vector<Detection> dets,
                                         const NmsOptions& options);
```

旧接口继续存在，并委托到新接口：

```cpp
[[nodiscard]] std::vector<Detection> Nms(std::vector<Detection> dets,
                                         float iou_thresh);
```

行为约束：

- 默认 `max_det=0`，不改变 W16 结果。
- quant 路径设置 `max_det=300`，对齐常见 YOLO 部署上限。
- `iou_thresh` 不合法或 `max_det < 0` 时抛 `std::invalid_argument`。
- 输出仍保持全局 score 降序。

### 4.3 `DetectorConfig` 透传部署配置

在 W16 `DetectorConfig` 中加性新增字段，默认值保持旧行为：

```cpp
struct DetectorConfig {
  int input_size = 640;
  float conf_thresh = 0.25f;
  float iou_thresh = 0.45f;
  w14::Ep ep = w14::Ep::kCuda;
  int intra_op_threads = 0;
  int inter_op_threads = 0;
  bool use_iobinding = false;
  bool skip_non_finite = false;
  int reserve_hint = 0;
  int max_det = 0;
};
```

构造 `YOLODetector` 时用 `w14::SessionConfig` 创建 engine；`Detect()` 根据 `use_iobinding`
选择 `Run` 或 `RunIoBinding`。旧调用方不传这些字段，行为保持不变。

### 4.4 W16 文档处理

W16 notes 不改写历史结果，只补一个“小节”说明：

- 后续 quant 为部署硬化做了加性扩展。
- W16 原始 benchmark 仍代表当时 demo 状态。
- 新部署评估数字以 quant benchmark/report 为准。

## 5. quant 模块结构

新增目录：

```text
02_Inference_Analysis/quantization/
  rolling_stats.hpp
  rolling_stats.cpp
  eval_harness.hpp
  eval_harness.cpp
  quant_benchmark.cpp
  rolling_stats_test.cpp
  batch_consistency_test.cpp
  eval_harness_test.cpp
  notes.md
  CMakeLists.txt
```

顶层 `CMakeLists.txt` 新增：

```cmake
add_subdirectory(02_Inference_Analysis/quantization)
```

命名空间统一为 `quant`。测试名使用 W17 起的新规则：
`Quant_RollingStatsTest`、`Quant_BatchConsistencyTest`、`Quant_EvalHarnessTest`。

quant 不再包含自己的 `decode.cpp` / `nms.cpp`，而是链接并复用 `w16_yolo_lib` /
`w16_detect_core` 的加性硬化能力。

## 6. quant 组件设计

### 6.1 `RollingStats`

`rolling_stats` 记录分段耗时，服务运行时观测和 benchmark 输出。

```cpp
struct StageLatencyMs {
  double pre;
  double infer;
  double post;
  double total;
};

struct PercentileStats {
  double p50;
  double p99;
};
```

设计要点：

- 固定窗口，默认保存最近 128 帧，避免长期运行无限增长。
- 每帧记录 `pre / infer / post / total`。
- P50/P99 查询时复制窗口并排序，窗口较小，成本可接受。
- 空窗口查询固定返回 0，便于 demo 初始状态打印。
- 统计类不做全局单例，归属于 harness 实例。

### 6.2 `EvalHarness`

`eval_harness` 是 quant 的核心，不替代 W16 detector，而是包装一组 model path + detector config +
统计能力，服务 FP32/INT8 对比。

```cpp
struct ModelCase {
  std::string name;       // fp32 / int8_minmax / int8_entropy
  std::string model_path;
};

struct EvalConfig {
  w16::DetectorConfig detector;
  int warmup = 10;
  int iters = 50;
};
```

职责：

- 用同一张图、同一份 detector 配置跑多个模型。
- 记录端到端 `pre / infer / post / total`。
- 暴露 CUDA fallback reason，由 benchmark/demo 显式打印。
- 输出每个模型的检测数量、Top score、耗时统计，供 Markdown 报告使用。

## 7. Benchmark 与报告

`quant_benchmark.cpp` 不重复 W16 全量 benchmark，而是围绕“W16 baseline -> quant hardened -> INT8”
做差异化输出：

- W16 默认配置 vs hardened 配置：`skip_non_finite / max_det / reserve_hint / use_iobinding / threads`。
- FP32 hardened pipeline 的端到端三段：`pre / infer / post / total / FPS`。
- `Run` vs `RunIoBinding` 只保留一张对比表，并明确收益若进噪声就如实记录。
- CPU / CUDA EP 对比；CUDA 不可用时显式打印回退原因。
- batch=4 consistency 验证：证明动态 batch 不只是“不崩”，而是每份输出与 batch=1 对齐。

后续 INT8 接入后，同一个 benchmark 扩展为：

- FP32 vs INT8 模型体积。
- FP32 vs INT8 端到端延迟与纯推理延迟。
- FP32 vs INT8 检测框一致性或 mAP 近似评估。

报告文件：

- `docs/benchmarks/quant_yolo_hardening.md`：部署硬化 before/after 与工程结论。
- `docs/benchmarks/quant_int8_report.md`：量化前后体积 / 延迟 / 精度对比。

## 8. 测试设计

### 8.1 W16 扩展测试

新增或扩展 W16 测试：

- `W16_DecodeTest`：NaN / Inf 跳过、`reserve_hint` 不改变结果、非法参数抛异常。
- `W16_NmsTest`：`max_det` 截断、`max_det=0` 与旧行为一致、非法参数抛异常。
- `W16_DetectorTest`：线程配置和 `use_iobinding` 路径可构造、模型存在时能跑通。

这些仍使用 `W16_*` 命名，因为被测能力属于 W16 检测库的加性扩展。

### 8.2 `Quant_RollingStatsTest`

覆盖：

- P50 / P99 计算。
- 固定窗口滚动淘汰旧值。
- 空窗口查询固定返回 0。

### 8.3 `Quant_BatchConsistencyTest`

覆盖：

- 用 W14 engine 直接跑 batch=1 与 batch=4。
- 对 batch=4 输出按 batch 维切片。
- 每份输出解码 + NMS 后与对应 batch=1 结果对齐。
- 正式 detector API 仍不暴露 batch N。

### 8.4 `Quant_EvalHarnessTest`

覆盖：

- 模型和测试图存在时跑通 FP32 case。
- 模型缺失时 `GTEST_SKIP()`，保持 CI / VPS 无模型环境不红。
- 请求 CUDA 但回退 CPU 时不崩溃，并能读取 fallback reason。

## 9. 文档设计

`quantization/notes.md` 必须包含：

- 模块定位：quant 是 W16 基线之上的部署评估 harness，不是 W16 的复制版。
- Mermaid 数据流图：模型列表 / 图片 -> W16 detector -> RollingStats -> benchmark/report。
- 与 W16 的差异表：W16 已有、W16 加性扩展、quant 新增 harness。
- 测试 / benchmark 命令。
- 量化概念链接到主题库。

W16 notes 仅补：

- “为 quant 做了向后兼容加性扩展”的说明。
- 指向 quant notes/report，不改写 W16 原始结论。

主题库新增或扩展：

- `docs/notes/inference.md`：PTQ、MinMax / Entropy、对称 / 非对称、per-tensor / per-channel、QDQ vs QOperator。
- `docs/notes/systems-perf.md` 如果建立：profiling 方法、P50/P99、micro-benchmark vs end-to-end。

AGENTS / Roadmap 同步：

- 开始实现 quant 时更新 `AGENTS.md` 当前进度一句话。
- `docs/Roadmap.md` 中 `quant` 状态从“下一站”改为“进行中”。
- 如提交检查涉及技术债，路径按 `docs/archive/tech-debt.md`。

## 10. 验收标准

第一阶段部署硬化验收：

- `ctest -R "W16_|Quant_"` 全绿，模型缺失测试按预期 skip。
- 既有 W16 默认行为不变，旧接口仍可编译。
- `clang-format-21 --dry-run --Werror` 通过。
- `quant_benchmark` 能输出端到端三段表和 CUDA fallback 告警。
- `Quant_BatchConsistencyTest` 证明 batch=4 输出按 batch 切片后与 batch=1 一致。
- `docs/benchmarks/quant_yolo_hardening.md` 明确区分 W16 baseline 与 quant hardened 数字。

第二阶段 INT8 验收：

- ORT 官方量化工具生成 MinMax / Entropy 两类 INT8 模型。
- FP32 / INT8 共用同一 W16 detector + quant harness 对比模型体积、端到端延迟、检测结果或 mAP。
- `docs/benchmarks/quant_int8_report.md` 能解释延迟变化和精度掉点。

## 11. 风险与应对

- **触碰 W16 存档边界**：只允许向后兼容加性扩展；W16 notes 不改写历史结论。
- **IOBinding 仍可能收益噪声级**：报告必须诚实记录；它是配置能力和测量对象，不承诺显著加速。
- **INT8 对 CUDA EP 未必稳定提速**：量化报告要区分 CPU EP、CUDA EP、模型小导致固定开销占比高等因素。
- **batch consistency 范围容易膨胀**：只作为测试 / benchmark 证明，不扩展正式 detector API。
- **量化精度评估数据集不足**：第一版可用检测一致性和小样本报告，后续再接 COCO 子集 mAP。

## 12. 实施顺序建议

1. 对 W16 `decode` / `nms` 增加 options overload，并保持旧接口委托。
2. 扩展 W16 单测，证明默认行为不变、硬化配置生效。
3. 扩展 W16 `DetectorConfig`，透传线程、IOBinding、NaN/Inf、`max_det`。
4. 新建 `quantization/`，实现 `RollingStats` 与测试。
5. 实现 `EvalHarness`、`quant_benchmark` 和 batch consistency test。
6. 写 W16 notes 补充段、`quantization/notes.md`、`quant_yolo_hardening.md`。
7. 再接 ORT Python 量化脚本与 `quant_int8_report.md`。
