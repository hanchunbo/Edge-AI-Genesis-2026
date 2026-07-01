# Quant — INT8 量化前的 YOLO 部署硬化设计文档

> 设计状态：2026-07-01 讨论确认。用户选择 **C++ 工程优先**，采用方案 B：
> 在 `quantization/` 新主题模块中复刻并硬化检测 core，不修改 W1-W16 存档代码。

## 1. 目标

进入 Phase 0 的 `quant` 交付物，先建立一条比 W16 更接近部署形态的 YOLOv8n C++ 检测流水线，
再把 FP32 / INT8 模型挂到同一条流水线上做量化对比。这个模块的第一目标不是立刻追求
TensorRT 或 GPU 前后处理，而是补齐 W16 复盘暴露出的工程硬化缺口：

- 检测后处理鲁棒性：`reserve`、NaN/Inf 跳过、`max_det` 上限。
- 运行时可观测性：`pre / infer / post / total` 分段延迟的滚动 P50/P99。
- 部署配置接入：线程配置、IOBinding 开关、CUDA 回退显式告警。
- 正确性证据增强：batch=4 只做 benchmark / consistency 验证，正式 API 保持 batch=1 单路实时。
- 后续量化对比：FP32 与 INT8 共用同一 pipeline，只替换 model path，避免前后处理差异污染结论。

## 2. 范围决策

### 2.1 用户已确认的边界

- **方案选择**：采用方案 B，新模块复刻并硬化检测 core。
- **冻结边界**：W1-W16 作为周志存档冻结，不直接修改 `w14_ort_basics` / `w16_yolo_detector`。
- **API 范围**：正式检测 API 只支持 batch=1；batch=4 进入 benchmark / consistency test。
- **技术债路径**：技术债文档实际位于 `docs/archive/tech-debt.md`。

### 2.2 不在本阶段做

- 不做 TensorRT engine、FP16 路径、TRT INT8 calibrator。
- 不做 GPU 端 letterbox / decode / NMS，也不做 ORT 图融合。
- 不把 W14-W16 抽成新的公共 inference/detect 库。
- 不实现剪枝 / 蒸馏，只在文档中做概念覆盖与定位区分。

这些内容分别归 `trt` 或后续弹性深化，避免 `quant` 被后端优化和重构拖散。

## 3. 模块结构

新增目录：

```text
02_Inference_Analysis/quantization/
  detection.hpp
  decode.hpp
  decode.cpp
  nms.hpp
  nms.cpp
  pipeline.hpp
  pipeline.cpp
  rolling_stats.hpp
  rolling_stats.cpp
  quant_benchmark.cpp
  decode_test.cpp
  nms_test.cpp
  rolling_stats_test.cpp
  pipeline_test.cpp
  batch_consistency_test.cpp
  notes.md
  CMakeLists.txt
```

顶层 `CMakeLists.txt` 新增：

```cmake
add_subdirectory(02_Inference_Analysis/quantization)
```

命名空间统一为 `quant`。测试名使用 W17 起的新规则：`Quant_DecodeTest`、
`Quant_NmsTest`、`Quant_RollingStatsTest`、`Quant_PipelineTest`、
`Quant_BatchConsistencyTest`。

## 4. 组件设计

### 4.1 纯 C++ core

`detection.hpp` 定义检测框结构，作为 decode / nms / pipeline 的共享数据结构：

```cpp
struct Detection {
  float x1;
  float y1;
  float x2;
  float y2;
  float score;
  int class_id;
};
```

`decode` 负责把 YOLOv8 单 batch 输出 `[1,C,A]` 解析成原图坐标系候选框：

- 校验 `num_classes > 0`、`num_predictions > 0`、`scale > 0`、图像尺寸合法。
- 校验张量大小等于 `(4 + num_classes) * num_predictions`。
- 遍历每个 prediction 时取类别最大值作为 score。
- 跳过非有限值：坐标、宽高、score 任何一个为 NaN/Inf 都不进入候选集。
- `reserve` 预留候选 vector 容量，减少逐帧 realloc。容量不盲目预留到全部 predictions，
  初始可按 `min(num_predictions, 1024)`，benchmark 再用分配次数或耗时验证。
- 做 `cxcywh -> xyxy`、letterbox 坐标反算、边界 clamp。

`nms` 负责逐类非极大值抑制：

- 输入按值传递，内部排序，不改调用方数据。
- 全局按 score 降序，逐类抑制同类高 IoU 框。
- 输出仍保持全局 score 降序。
- 新增 `max_det`，在结果达到上限后停止继续保留。
- `max_det <= 0` 视为配置错误，抛 `std::invalid_argument`。

这两部分不依赖 ORT / OpenCV，保证最高风险逻辑可独立测试。

### 4.2 Pipeline 编排

`pipeline` 组合 OpenCV、W10 letterbox、W14 InferenceEngine 和 quant core，形成单图实时检测 API。

核心配置：

```cpp
struct PipelineConfig {
  int input_size = 640;
  float conf_thresh = 0.25f;
  float iou_thresh = 0.45f;
  int max_det = 300;
  w14::Ep ep = w14::Ep::kCuda;
  int intra_op_threads = 0;
  int inter_op_threads = 0;
  bool use_iobinding = false;
};
```

核心接口：

```cpp
class YoloPipeline {
 public:
  YoloPipeline(const std::string& model_path, PipelineConfig cfg = {});

  [[nodiscard]] std::vector<Detection> Detect(const cv::Mat& bgr);
  [[nodiscard]] std::vector<Detection> Detect(const std::string& image_path);
  [[nodiscard]] w14::Ep ActiveEp() const;
  [[nodiscard]] const std::string& EpFallbackReason() const;
  [[nodiscard]] const RollingStats& Stats() const;
};
```

`Detect` 的 batch=1 数据流：

```text
cv::Mat BGR
  -> cvtColor BGR->RGB
  -> w10::LetterboxToTensor(640, LetterboxInfo)
  -> w14::InferenceEngine::Run 或 RunIoBinding
  -> quant::DecodeYolov8
  -> quant::Nms(max_det)
  -> vector<quant::Detection>
```

CUDA 显式告警策略：

- 构造时若请求 `ep=kCuda` 但 `ActiveEp()==kCpu`，保留 W14 的优雅回退语义。
- `YoloPipeline` 对外暴露 `EpFallbackReason()`，demo / benchmark 打印明确告警。
- 不在库内直接退出进程；是否失败退出由 demo / benchmark 决定。

### 4.3 RollingStats

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
- 每次 `Detect` 记录 `pre / infer / post / total`。
- P50/P99 查询时复制窗口并排序，窗口较小，成本可接受。
- 统计类不做全局单例，归属于 pipeline 实例。

## 5. Benchmark 与报告

`quant_benchmark.cpp` 负责输出可直接粘贴到 Markdown 的表格：

- FP32 pipeline 的端到端三段：`pre / infer / post / total / FPS`。
- `Run` vs `RunIoBinding` 对比。
- CPU / CUDA EP 对比；CUDA 不可用时显式打印回退原因。
- IntraOp 线程扫描。
- batch=4 consistency 验证：同图平铺或多图输入在 benchmark / test 中切片对比，
  证明动态 batch 不只是“不崩”，而是每份输出与 batch=1 对齐。

后续 INT8 接入后，同一个 benchmark 扩展为：

- FP32 vs INT8 模型体积。
- FP32 vs INT8 端到端延迟与纯推理延迟。
- FP32 vs INT8 检测框一致性或 mAP 近似评估。

报告文件：

- `docs/benchmarks/quant_yolo_hardening.md`：部署硬化 before/after 与工程结论。
- `docs/benchmarks/quant_int8_report.md`：量化前后体积 / 延迟 / 精度对比。

## 6. 测试设计

### 6.1 `Quant_DecodeTest`

覆盖：

- 合成张量正常解码。
- 低于 `conf_thresh` 的候选被过滤。
- NaN / Inf 的 score、坐标、宽高被跳过。
- letterbox 坐标反算与 clamp。
- 非法张量尺寸、非法参数抛异常。

### 6.2 `Quant_NmsTest`

覆盖：

- IoU 手算对拍。
- 同类高 IoU 抑制。
- 跨类不抑制。
- 输出全局 score 降序。
- `max_det` 截断。
- 非法 `max_det` 抛异常。

### 6.3 `Quant_RollingStatsTest`

覆盖：

- P50 / P99 计算。
- 固定窗口滚动淘汰旧值。
- 空窗口查询固定返回 0，便于 demo 初始状态打印。

### 6.4 `Quant_PipelineTest`

覆盖：

- 模型和测试图存在时跑通 bus 图。
- 模型缺失时 `GTEST_SKIP()`，保持 CI / VPS 无模型环境不红。
- 请求 CUDA 但回退 CPU 时不崩溃，并能读取 fallback reason。

### 6.5 `Quant_BatchConsistencyTest`

覆盖：

- 用 W14 engine 直接跑 batch=1 与 batch=4。
- 对 batch=4 输出按 batch 维切片。
- 每份输出解码 + NMS 后与对应 batch=1 结果对齐。

正式 pipeline 仍不暴露 batch N API；这个测试只补足 W16 技术债证据。

## 7. 文档设计

`quantization/notes.md` 必须包含：

- 模块定位：W17 起主题模块，不是 W16 周志续写。
- Mermaid 数据流图：输入图片 / 模型路径 -> 预处理 -> ORT -> 硬化 decode -> NMS -> 统计 / 检测结果。
- 与 W16 的差异表：`reserve`、NaN/Inf、`max_det`、滚动统计、线程/IOBinding 接入、显式 CUDA 告警。
- 测试 / benchmark 命令。
- 量化概念链接到主题库。

主题库新增或扩展：

- `docs/notes/inference.md`：PTQ、MinMax / Entropy、对称 / 非对称、per-tensor / per-channel、QDQ vs QOperator。
- `docs/notes/systems-perf.md` 如果建立：profiling 方法、P50/P99、micro-benchmark vs end-to-end。

AGENTS / Roadmap 同步：

- 开始实现 quant 时更新 `AGENTS.md` 当前进度一句话。
- `docs/Roadmap.md` 中 `quant` 状态从“下一站”改为“进行中”。
- 如提交检查涉及技术债，路径按 `docs/archive/tech-debt.md`。

## 8. 验收标准

第一阶段部署硬化验收：

- `ctest -R "Quant_"` 全绿，模型缺失测试按预期 skip。
- `clang-format-21 --dry-run --Werror` 通过。
- `quant_benchmark` 能输出端到端三段表和 CUDA fallback 告警。
- `Quant_BatchConsistencyTest` 证明 batch=4 输出按 batch 切片后与 batch=1 一致。
- `docs/benchmarks/quant_yolo_hardening.md` 有 before/after 或相邻版本对比数字。

第二阶段 INT8 验收：

- ORT 官方量化工具生成 MinMax / Entropy 两类 INT8 模型。
- FP32 / INT8 共用同一 pipeline 对比模型体积、端到端延迟、检测结果或 mAP。
- `docs/benchmarks/quant_int8_report.md` 能解释延迟变化和精度掉点。

## 9. 风险与应对

- **重复 W16 core 代码**：这是有意选择。W16 已冻结，quant 需要一个部署硬化版本来承载新指标。
- **IOBinding 仍可能收益噪声级**：报告必须诚实记录；它是配置能力和测量对象，不承诺显著加速。
- **INT8 对 CUDA EP 未必稳定提速**：量化报告要区分 CPU EP、CUDA EP、模型小导致固定开销占比高等因素。
- **batch consistency 范围容易膨胀**：只作为测试 / benchmark 证明，不扩展正式 pipeline API。
- **量化精度评估数据集不足**：第一版可用检测一致性和小样本报告，后续再接 COCO 子集 mAP。

## 10. 实施顺序建议

1. 新建 `quantization/` CMake 和纯 C++ core 测试。
2. 实现 `decode` / `nms` 的硬化版本，先让 `Quant_DecodeTest`、`Quant_NmsTest` 通过。
3. 实现 `RollingStats` 与单测。
4. 实现 `YoloPipeline`，接入线程配置、IOBinding 开关、CUDA fallback reason。
5. 实现 benchmark 和 batch consistency test。
6. 写 `notes.md` 与 `quant_yolo_hardening.md`。
7. 再接 ORT Python 量化脚本与 INT8 报告。
