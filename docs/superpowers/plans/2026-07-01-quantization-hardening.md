# quant 量化部署硬化实施计划

> **给执行 agent：** 必须使用 `superpowers:subagent-driven-development`（推荐）或 `superpowers:executing-plans` 按任务逐项执行本计划。步骤使用 checkbox（`- [ ]`）跟踪。

**目标：** 在 W16 YOLOv8n 检测基线上完成 `quant` Phase 0：部署硬化、C++ 评估 harness、ORT INT8 PTQ 对比。

**架构：** W16 继续负责检测 pipeline，只做向后兼容加性扩展；`02_Inference_Analysis/quantization/` 复用 W16/W14，不复制 YOLO core。quant 负责滚动统计、多模型评估、batch consistency、benchmark 与报告。

**技术栈：** C++20、CMake/Ninja、GoogleTest、ONNX Runtime C++ API、OpenCV、Python ORT quantization API、Mermaid。

---

## 基线与边界

- 设计文档：`docs/superpowers/specs/2026-07-01-quantization-hardening-design.md`
- 基线提交：`c2cd754 docs: revise quant hardening design around W16 baseline`
- 分支：`dev`，与 `origin/dev` 对齐
- 不纳入：`.codex/config.toml`
- W14 target：`w14_inference_engine`
- W16 targets：`w16_detect_core`、`w16_yolo_lib`
- 规则：W16 只能加性扩展；quant 不复制 decode/NMS；模型缺失测试用 `GTEST_SKIP()`。

## 子代理执行方案

采用 `superpowers:subagent-driven-development` 的串行子代理流程。不要并行派发实现子代理，因为任务 2-8 会连续触碰 W16/quant CMake 与共享接口，并行容易互相覆盖。

每个任务按同一流程执行：

1. Controller 读取当前任务全文和相关设计上下文。
2. 派发一个 fresh implementer subagent，只给该任务需要的文件、接口、测试点、验证命令和提交要求。
3. implementer 完成实现、测试、自查、提交后返回状态。
4. 派发 spec reviewer subagent，检查是否严格满足本计划和设计文档；有问题则回到 implementer 修复并复审。
5. 派发 code quality reviewer subagent，检查代码质量、测试风险、边界条件和项目规范；有问题则修复并复审。
6. 两轮 review 都通过后，controller 标记该任务完成，再进入下一个任务。

任务分组建议：

- 任务 1、10、12、13：文档型任务，可用轻量实现子代理，但仍保留 spec review。
- 任务 2、3：纯 C++ core，适合小上下文 implementer，重点 review 默认行为兼容性。
- 任务 4、6、8：跨 W16/W14/quant 的集成任务，用更强实现子代理，重点 review API 边界和 fallback 行为。
- 任务 5、7、9、11：中等复杂度，重点 review CMake target、skip 策略、profiling 口径和离线/缺依赖场景。
- 任务 14：不派实现子代理；由 controller 执行全量验证，再派 final reviewer 做整体 code review。

## 文件总览

- W16：`02_Inference_Analysis/w16_yolo_detector/{decode,nms,yolo_detector}.{hpp,cpp}` 及对应测试、`notes.md`
- quant：`02_Inference_Analysis/quantization/` 下 CMake、RollingStats、EvalHarness、batch test、benchmark、PTQ 脚本、notes
- 文档：`CMakeLists.txt`、`AGENTS.md`、`docs/Roadmap.md`、`docs/notes/*`、`docs/benchmarks/*`

---

## 任务 1：同步 quant 状态

**文件：** `AGENTS.md`、`docs/Roadmap.md`

- [ ] `AGENTS.md` 当前进度改为 `quant` 进行中：第一阶段部署硬化 + Profiling，第二阶段 INT8 PTQ。
- [ ] `docs/Roadmap.md` 中 `quant` 标记为进行中。
- [ ] 验证：

```bash
git diff -- AGENTS.md docs/Roadmap.md
```

- [ ] 提交：`docs: mark quant deliverable in progress`

---

## 任务 2：W16 decode options

**文件：** `decode.hpp`、`decode.cpp`、`decode_test.cpp`

**新增接口：**

```cpp
struct DecodeOptions {
  float conf_thresh = 0.25f;
  bool skip_non_finite = false;
  int reserve_hint = 0;
};
```

- [ ] 先写测试：旧 overload 等价、NaN/Inf skip、`reserve_hint` 不改结果、非法参数抛 `std::invalid_argument`。
- [ ] 实现：新增 options overload；旧 `conf_thresh` overload 委托到新 overload；默认行为不变。
- [ ] 验证：

```bash
cmake --build build --target w16_decode_test
ctest --test-dir build -R W16_DecodeTest --output-on-failure
```

- [ ] 提交：`feat: add hardened YOLO decode options`

---

## 任务 3：W16 NMS options

**文件：** `nms.hpp`、`nms.cpp`、`nms_test.cpp`

**新增接口：**

```cpp
struct NmsOptions {
  float iou_thresh = 0.45f;
  int max_det = 0;
};
```

- [ ] 先写测试：旧 overload 等价、`max_det=0` 不限制、`max_det>0` 截断、非法参数抛异常。
- [ ] 实现：新增 options overload；旧 `float iou_thresh` overload 委托到新 overload；输出保持全局 score 降序。
- [ ] 验证：

```bash
cmake --build build --target w16_nms_test
ctest --test-dir build -R W16_NmsTest --output-on-failure
```

- [ ] 提交：`feat: add hardened NMS options`

---

## 任务 4：W16 Detector 部署配置与计时

**文件：** `yolo_detector.hpp`、`yolo_detector.cpp`、`yolo_detector_test.cpp`

**新增能力：** `DetectorConfig` 增加线程、IOBinding、NaN/Inf、reserve、`max_det` 字段；新增 `DetectWithProfile()`、`DetectionTiming`、`DetectionResult`、`EpFallbackReason()`。

- [ ] 先写测试：部署配置可构造并跑通；`DetectWithProfile()` 返回非负分段耗时；CUDA 回退时 fallback reason 非空。
- [ ] 实现：构造 `w14::SessionConfig`；按 `use_iobinding` 选择 `Run`/`RunIoBinding`；decode/NMS 使用任务 2/3 options；旧 `Detect()` 委托到新接口。
- [ ] 验证：

```bash
cmake --build build --target w16_yolo_detector_test
ctest --test-dir build -R W16_DetectorTest --output-on-failure
ctest --test-dir build -R W16_ --output-on-failure
```

- [ ] 提交：`feat: pass deployment options through YOLO detector`

---

## 任务 5：quant 模块骨架与 RollingStats

**文件：** 顶层 `CMakeLists.txt`、`quantization/CMakeLists.txt`、`rolling_stats.hpp/.cpp/_test.cpp`

- [ ] CMake：顶层加入 `add_subdirectory(02_Inference_Analysis/quantization)`；quant target 缺 `w16_yolo_lib` 时跳过。
- [ ] 先写测试：空窗口为 0、P50/P99 正确、固定窗口淘汰、`window_size=0` 抛异常。
- [ ] 实现：`StageLatencyMs`、`PercentileStats`、`StageLatencyStats`、`RollingStats`。
- [ ] 验证：

```bash
cmake -S . -B build -G Ninja
cmake --build build --target quant_rolling_stats_test
ctest --test-dir build -R Quant_RollingStatsTest --output-on-failure
```

- [ ] 提交：`feat: add quant rolling latency stats`

---

## 任务 6：EvalHarness

**文件：** `eval_harness.hpp`、`eval_harness.cpp`、`eval_harness_test.cpp`、quant CMake

- [ ] 接口：`ModelCase{name, model_path}`、`EvalConfig{detector, warmup, iters, stats_window}`、`ModelResult`。
- [ ] 先写测试：FP32 case 可跑；统计样本数等于 `iters`；CUDA fallback reason 可读；资产缺失 skip。
- [ ] 实现：每个 model case 独立构造 W16 detector；warmup 不计入统计；正式迭代写入 `RollingStats`。
- [ ] 验证：

```bash
cmake --build build --target quant_eval_harness_test
ctest --test-dir build -R Quant_EvalHarnessTest --output-on-failure
```

- [ ] 提交：`feat: add quant evaluation harness`

---

## 任务 7：batch consistency 测试

**文件：** `batch_consistency_test.cpp`、quant CMake

- [ ] 测试逻辑：W14 engine 跑 batch=1 与 batch=4；batch=4 输出按 batch 维切片；每片复用 W16 decode/NMS；与 batch=1 比较框数、class、score、xyxy。
- [ ] 约束：不扩展正式 `YOLODetector` batch API；资产缺失 skip。
- [ ] 验证：

```bash
cmake --build build --target quant_batch_consistency_test
ctest --test-dir build -R Quant_BatchConsistencyTest --output-on-failure
```

- [ ] 提交：`test: add quant batch consistency check`

---

## 任务 8：quant_benchmark

**文件：** `quant_benchmark.cpp`、quant CMake

- [ ] 实现 CLI：`[image] [fp32_model] [optional_int8_model ...]`。
- [ ] 默认 hardened 配置：`skip_non_finite=true`、`max_det=300`、`reserve_hint=512`、`use_iobinding=true`。
- [ ] 输出 Markdown 两张表：纯 ORT infer P50/P99/FPS；端到端 pre/infer/post/total P50、total P99、FPS。
- [ ] 输出检测数量、top score、CUDA fallback reason。
- [ ] 保留 Run vs IOBinding 对比；收益低于噪声时报告必须如实记录。
- [ ] 验证：

```bash
cmake --build build --target quant_benchmark
./build/02_Inference_Analysis/quantization/quant_benchmark
```

- [ ] 提交：`feat: add quant benchmark harness`

---

## 任务 9：Profiling 与 before/after baseline

**文件：** `quant_benchmark.cpp`、`docs/benchmarks/quant_yolo_hardening.md`

- [ ] 在 benchmark 输出中明确 baseline vs hardened：W16 默认配置、quant hardened 配置。
- [ ] 记录部署硬化 before/after 数字：候选 reserve 影响、`max_det` 上限、NaN/Inf 鲁棒用例、Run vs IOBinding 收益。
- [ ] 记录 profiling baseline：pre/infer/post 占比、CPU/CUDA EP、CUDA fallback、构建类型、输入图和模型路径。
- [ ] perf/Nsight 可用时记录瓶颈归因；不可用时在报告中写明工具缺失，不伪造 profiling 结论。
- [ ] 如果本阶段不做“输入输出双绑 + buffer 池”，报告中明确列为 Roadmap 差距/后续项，避免把 `use_iobinding=true` 写成完整双绑实现。
- [ ] 验证：

```bash
cmake --build build --target quant_benchmark
./build/02_Inference_Analysis/quantization/quant_benchmark
```

- [ ] 提交：`docs: record quant profiling baseline`

---

## 任务 10：第一阶段文档

**文件：** W16 `notes.md`、`quantization/notes.md`、`docs/benchmarks/quant_yolo_hardening.md`

- [ ] W16 notes：只补 quant 加性扩展说明，不改写 W16 原始结论。
- [ ] quant notes：模块定位、Mermaid 数据流、W16 vs quant 对比、测试/benchmark 命令、主题库链接。
- [ ] hardening report：环境、baseline vs hardened 配置、纯 infer 表、端到端表、batch consistency、fallback 状态、profiling baseline、Roadmap 差距。
- [ ] Mermaid 验证：

```bash
sed -n '/```mermaid/,/```/p' 02_Inference_Analysis/quantization/notes.md \
  | sed '1d;$d' > /tmp/quant_flow.mmd
mmdc -i /tmp/quant_flow.mmd -o /tmp/quant_flow.svg
```

- [ ] 提交：`docs: add quant hardening notes`

---

## 任务 11：ORT static INT8 脚本

**文件：** `tools/quantize_yolov8_static.py`、quant CMake、`README.md`

- [ ] 脚本：`quant_pre_process` + `quantize_static`；QDQ；MinMax 和 Entropy 两个输出。
- [ ] 校准预处理：RGB、letterbox 640、NCHW、float32、除以 255。
- [ ] CMake target：`quant_yolov8_static` 始终存在；缺 Python/onnxruntime/OpenCV Python 包时输出清晰错误。
- [ ] 同步 `README.md` 前提条件，记录 Python 量化依赖安装命令和模型/图片资产要求。
- [ ] 验证：

```bash
python3 02_Inference_Analysis/quantization/tools/quantize_yolov8_static.py --help
cmake --build build --target quant_yolov8_static
```

- [ ] 提交：`feat: add ORT static INT8 quantization script`

---

## 任务 12：FP32/INT8 报告

**文件：** `docs/benchmarks/quant_int8_report.md`、`quantization/notes.md`

- [ ] 构建并生成模型：

```bash
cmake --build build --target quant_benchmark quant_yolov8_static
```

- [ ] 运行对比：

```bash
./build/02_Inference_Analysis/quantization/quant_benchmark \
  02_Inference_Analysis/w16_yolo_detector/models/test_image.jpg \
  02_Inference_Analysis/w16_yolo_detector/models/yolov8n.onnx \
  build/02_Inference_Analysis/quantization/models/yolov8n.int8.minmax.onnx \
  build/02_Inference_Analysis/quantization/models/yolov8n.int8.entropy.onnx
```

- [ ] 记录模型体积：

```bash
ls -lh 02_Inference_Analysis/w16_yolo_detector/models/yolov8n.onnx \
       build/02_Inference_Analysis/quantization/models/yolov8n.int8.minmax.onnx \
       build/02_Inference_Analysis/quantization/models/yolov8n.int8.entropy.onnx
```

- [ ] 报告内容：环境、校准数据、量化格式、模型体积、纯 infer 延迟、端到端延迟、检测变化、CPU/CUDA EP 差异、结论范围。
- [ ] 精度口径：第一版至少做检测一致性/小样本评估；明确单图一致性不是 mAP，若没有 COCO subset mAP，则标为 Roadmap 差距。
- [ ] quant notes 增加 INT8 命令与报告链接。
- [ ] 提交：`docs: add quant INT8 report`

---

## 任务 13：主题库笔记

**文件：** `docs/notes/README.md`、`docs/notes/inference.md`、`docs/notes/systems-perf.md`

- [ ] `inference.md`：PTQ、MinMax/Entropy、对称/非对称、Per-Tensor/Per-Channel、QDQ/QOperator。
- [ ] `inference.md`：补量化 vs 剪枝 vs 蒸馏的定位差异，满足 Roadmap 概念覆盖要求。
- [ ] `systems-perf.md`：P50/P99、micro-benchmark vs end-to-end、profiling 报告方法、fallback/Debug/skip 的结论边界。
- [ ] `README.md`：更新索引状态。
- [ ] 提交：`docs: add quantization and profiling notes`

---

## 任务 14：全量验证

- [ ] 配置：

```bash
cmake -S . -B build -G Ninja
```

- [ ] 构建：

```bash
cmake --build build --target w16_decode_test w16_nms_test w16_yolo_detector_test w16_yolo_demo w16_yolo_benchmark quant_rolling_stats_test quant_eval_harness_test quant_batch_consistency_test quant_benchmark
```

- [ ] 测试：

```bash
ctest --test-dir build -R "W16_|Quant_" --output-on-failure
```

- [ ] benchmark：

```bash
./build/02_Inference_Analysis/quantization/quant_benchmark
```

- [ ] 格式：

```bash
find . -maxdepth 3 -regex '.*0[1-4]_.*' \( -name "*.cpp" -o -name "*.hpp" \) | xargs clang-format-21 --dry-run --Werror
```

- [ ] git 状态：

```bash
git status --short --branch
```

预期：测试通过或模型依赖测试按预期 skip；格式检查通过；只剩本计划相关改动。
