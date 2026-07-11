# trt 交付物设计 — TensorRT C++ Engine（FP16 + INT8 双路线 + GPU 端到端）

- **日期**：2026-07-06
- **状态**：设计已评审（用户逐段确认）
- **上游**：`docs/Roadmap.md` Phase 0 · 交付物 `trt`；quant 收口移交项（INT8 的 GPU 加速）

## 1. 背景与目标

quant 已收口：CPU 纯推理 INT8 较 FP32 快约 24%，但同一 QDQ INT8 模型在 CUDA EP 上因 QDQ Memcpy 反而慢约 2×，INT8 的 GPU 加速移交本交付物。W16 实测前后处理占端到端约 45%，是 engine 之外最大的收益来源。

目标（对齐 Roadmap 成功指标）：

1. TRT INT8 纯推理延迟 < TRT FP16；
2. 讲清 TRT 与 ORT 量化路径差异，且有同机实测数字支撑；
3. 端到端（含前后处理）延迟较 W16 CUDA EP（FP32 纯 infer 5.64ms 基线）明显下降。

产出：四路对比表（CPU EP / CUDA EP / TRT FP16 / TRT INT8：体积 / 延迟 / mAP）、TRT 优化报告、GPU 端到端流水线延迟拆解（`docs/benchmarks/trt_*.md`）、ORT TensorRT EP 参考对比（补「ORT 委托 TRT」与「原生 TRT API」之间的空白）。

## 2. 关键决策（已确认）

| 决策点 | 结论 | 理由 |
|---|---|---|
| TRT 版本 | TRT 10.x | CUDA 12.3 只有 10.x 支持（8.6 最高到 CUDA 12.1），基本无选择 |
| INT8 路线 | **双路线对比**：隐式（`IInt8EntropyCalibrator2`）+ 显式（quant 产出的 QDQ ONNX） | 既落实 Roadmap 明确列出的 Calibrator C++ 实现，又正面闭环 quant 移交案子（同一 QDQ 模型：CUDA EP 慢 2× vs TRT 显式量化加速）；「TRT vs ORT 量化路径差异」从纸面讲变实测讲。注：Calibrator 在 TRT 10 已 deprecated，本身即是 implicit vs explicit 演进的叙事素材 |
| GPU 预处理 | **手写融合 CUDA kernel**（letterbox + 归一化 + HWC→CHW 单 kernel） | 全交付物唯一手写 CUDA 背书（其余均为 API 调用），成本约 1 天，为 Phase 3 CUDA 算子方向打底；替代方案 NPP 拼装胶水量相近但叙事变调库 |
| GPU 后处理 | **EfficientNMS_TRT plugin 融进 engine** | 工业标准做法，D2H 只回传最终几十个框；不手写 GPU NMS（投入产出比低）。插入方式首选 C++ 图手术（`INetworkDefinition` 切出 boxes/scores 两路 tensor + `addPluginV2`），兜底 Python onnx_graphsurgeon 改图 |
| 里程碑 | **三段式**（见 §4；2026-07-11 追加 M1.5 参考小节） | 每段结束都有可对外讲的增量，随时可提前收口 |

## 3. 模块结构

`02_Inference_Analysis/tensorrt/`（命名空间 `trt`），对齐 quant 模块组织方式：

```
tensorrt/
├── CMakeLists.txt             # find TensorRT + CUDAToolkit，enable_language(CUDA)
├── engine_builder.{cpp,hpp}   # ONNX → engine：nvonnxparser 解析 + FP16/INT8 BuilderConfig + engine 序列化缓存
├── int8_calibrator.{cpp,hpp}  # IInt8EntropyCalibrator2：喂 coco128 校准图 + 校准缓存文件
├── trt_engine.{cpp,hpp}       # 运行时：反序列化 + IExecutionContext + GPU buffer 管理（TRT 对象全 RAII 包装）
├── preprocess.{cu,hpp}        # M3：融合 CUDA kernel（letterbox + 归一化 + HWC→CHW）
├── trt_benchmark.cpp          # 四路对比（复用 quant 的 rolling_stats 统计口径）
├── trt_demo.cpp               # 端到端 demo + 检测结果 dump（供 mAP 评分）
├── tools/                     # mAP 评分脚本：吃 C++ dump 的预测文件，接 pycocotools 链路
└── notes.md                   # 数据流 Mermaid 必画（CLAUDE.md 规范）
```

## 4. 里程碑

### M1 — FP16 engine 跑通 + 四路表骨架

- 环境：WSL2 装 TensorRT 10.x（deb 网络源，版本锁定记入 README 前提条件）；确定 nvcc 宿主编译器方案（见风险 1）。
- `engine_builder` + `trt_engine`：C++ Builder 路径（`IBuilder→INetworkDefinition→ICudaEngine→IExecutionContext`）走通，FP16 engine 落盘缓存。
- 正确性：TRT FP16 检测结果 vs W16 `reference_detections.txt` 容差比对。
- 基准：TRT FP16 纯 infer vs CUDA EP FP32（5.64ms）；**CPU EP / CUDA EP 两列直接引用 quant 已有同机数字，不重测**。

### M1.5 — ORT TensorRT EP 参考对拍（2026-07-11 追加，M2 前置独立小节）

- **做什么**：`SessionOptions` 挂 `OrtTensorRTProviderOptionsV2`，FP16 精度下与原生 TRT engine（M1 实测 2.93ms）对比纯 infer 延迟，复用 `quant` 的 `rolling_stats` 统计口径。
- **为什么**：回答「ORT 委托 TRT vs 原生 TRT API」——量化 EP 壳层开销，补 M1 收口时排查发现的叙事空白；是四路表之外的独立小节，不占正式列。
- **依赖**：本机 GPU 包已含 `libonnxruntime_providers_tensorrt.so`（`third_party/onnxruntime/onnxruntime-linux-x64-gpu-1.26.0/lib/`），免下载；该 so 链接 `libnvinfer.so.10`，与本机 TRT 10.16 在 ABI 层面匹配（2026-07-11 用 strings 核过，仍需冒烟实测确认）。只需 FP32 onnx + FP16 精度，**不依赖 M2 的 quant INT8 生成物**，可在 M2 开工前独立完成。
- **风险**：ORT 1.26 绑定编译的 TRT 版本需与本机 TRT 10.x 实测兼容，不兼容则如实记录版本不匹配现象，不强凑数字。

### M2 — INT8 双路线 + mAP 闭环

- 路线 A（隐式）：`IInt8EntropyCalibrator2` C++ 实现，coco128 校准，TRT 内部定 scale。
- 路线 B（显式）：quant 的 QDQ ONNX 直接建 engine，TRT 按图中 Q/DQ 节点定 scale。
- mAP：C++ 侧对 coco128 val 出预测 dump → Python 只评分（不在 Python 里跑推理，控内存）。
- 核心叙事落表：同一 QDQ 模型在 CUDA EP 慢 2× vs TRT 显式量化的实测对比；双路线延迟/mAP 对比。
- 达标门槛：TRT INT8 纯推理延迟 < FP16（不达标时的处理见风险 3）。

### M3 — GPU 前后处理 + 端到端拆解 + 收口

- 融合预处理 kernel（`.cu`）：每线程一个输出像素（letterbox 仿射映射 → 双线性插值 → /255 → 写 CHW 平面），与 CPU letterbox 逐像素容差比对。
- EfficientNMS_TRT 融进 engine（首选 C++ 图手术，兜底 Python graphsurgeon）。
- 端到端拆解：CUDA events 分段埋点（H2D / pre / infer+nms / D2H），vs W16 CUDA EP 端到端对比。
- 收口：`docs/benchmarks/trt_*.md` 报告、notes.md（含数据流 Mermaid）、CLAUDE.md 进度 + Roadmap 里程碑状态同步。

## 5. 数据流

**M1/M2 形态**（前后处理在 CPU，测纯 infer + mAP）：

```
JPEG → CPU letterbox（复用 W16）→ H2D FP32 CHW（~4.9MB）→ TRT engine
→ D2H 84×8400 → CPU decode+NMS（复用 W16）→ 检测框 → dump 供 mAP 评分
```

**M3 最终形态**（全 GPU 流水线）：

```
JPEG → CPU 解码 → H2D uint8 原图（~0.9MB，上行缩小 ~5×）→ 融合预处理 kernel
→ TRT engine（含 EfficientNMS）→ D2H 最终框（几 KB）→ 绘制/统计
```

## 6. 错误处理

- **ILogger 适配器**：TRT 日志接 stderr，kWARNING 以上透传；build 失败带上下文中止。
- **CUDA 错误检查宏**：每个 CUDA 调用后必查；显式告警不静默回退（承接 quant 硬化原则）。
- **engine 缓存戳**：模型哈希 + TRT 版本 + 精度 + SM 架构，不匹配自动重建（engine 不跨版本/跨卡兼容）。
- 错误传播风格与现有模块对齐（实施计划阶段核对 quant/w16 用 `std::expected` 还是其他，照抄）。

## 7. 测试

GTest，`Trt_` 前缀；CI 无 GPU，检测不到 CUDA 设备时 `GTEST_SKIP`（与现有 CUDA EP 测试处理方式对齐，计划阶段核对）。

| 测试 | 验什么 |
|---|---|
| `Trt_EngineTest` | FP16 engine 构建 + 反序列化 + 单帧推理 smoke |
| `Trt_ConsistencyTest` | TRT 检测结果 vs `reference_detections.txt` 容差比对 |
| `Trt_CalibratorTest` | 校准批次供给逻辑 + 缓存读写（纯逻辑，不真跑校准） |
| `Trt_PreprocessTest` | kernel 输出 vs CPU letterbox 逐像素容差（双线性舍入 ±1~2 LSB） |

## 8. 风险与预案（按重要性）

1. **nvcc 宿主编译器**【已核实】：CUDA 12.3 的 nvcc 官方只支持 g++ ≤ 12.2，本机只有 13/14/15。方案：装 `g++-12` 专供 nvcc（`-ccbin`），`.cu` 与主工程（g++-15）之间只过 C ABI 边界（裸指针 + stream 句柄，不跨 `std::` 类型）；装完同步 README。备选：升 CUDA toolkit 12.4+（支持 g++-13），但 driver 是 12.3，引入 minor-version-compatibility 变数，不作首选。
2. **WSL 内存**【已核实】：`.wslconfig` 已加 `memory=12GB`（2026-07-06，待 `wsl --shutdown` 生效）；TRT builder 显式 `setMemoryPoolLimit`（1~2GB 工作区）；mAP 评估 C++ dump + Python 只评分；大内存 Python 任务仍包 systemd-run 内存墙。
3. **INT8 < FP16 门槛可能不达标**：YOLOv8n 小模型在 Ampere 上 INT8 增益可能有限（部分层保高精度、融合差异）。预案：如实记录 + 逐层归因（trex/verbose profiling 看哪些层没跑 INT8），归因本身就是合格产出，不造假不硬凑。
4. **TRT 10 on WSL2 安装**：官方支持，风险低；版本锁定记入 README。
5. **EfficientNMS C++ 图手术复杂度**：兜底 Python graphsurgeon 已定。

## 9. 复用资产清单（来自 quant / W16，只读复用，不改冻结模块）

| 资产 | 位置 | 用途 |
|---|---|---|
| `yolov8n.onnx`（FP32） | `w16_yolo_detector/models/` | FP16 / 隐式 INT8 建 engine 的输入 |
| QDQ INT8 ONNX | `quantization/tools/quantize_yolov8_static.py` 产出 | 显式量化路线输入 |
| coco128 校准集 | quant 既有 | Calibrator 校准数据 |
| `eval_map_coco128.py` | `quantization/tools/` | mAP 评分链路参考（trt 侧新脚本吃 dump 预测） |
| `rolling_stats` | `quantization/` | 基准统计口径（P50/P99） |
| `reference_detections.txt` | `w16_yolo_detector/models/` | 一致性测试基准 |
| CPU letterbox / decode / NMS | `w16_yolo_detector/` | M1/M2 复用 + M3 kernel 正确性参照 |

## 10. 不做什么（YAGNI）

- dynamic shape / dynamic batch：固定 1×3×640×640，batch=1；
- 手写 GPU NMS（用现成 EfficientNMS_TRT plugin）；
- 自定义 TRT plugin 开发（归 Phase 3 观察方向）；
- DLA / structured sparsity 等本机不可验证特性；
- 重测 CPU EP / CUDA EP（引用 quant 同机数字）。
