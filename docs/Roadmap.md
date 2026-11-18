# 总路线图（W1–W16 周志存档 · W17 起 Phase 主线）

> **本文件是项目唯一执行主线**。**结构分两段**：
> - **W1–W16（存档）**：已完成，按周记录，冻结回顾，不再改动。
> - **W17 起（前向）**：采用 **Phase + 交付物里程碑** 两层结构——**不再按周切分**，交付物按内容定大小、各自解锁一档能力里程碑。
>
> 作废原 Q2/Q3/Q4 季度结构。旧季度手册已归档至 `docs/archive/`（Q2/Q3/Q4.md）留作素材参考，不再作为执行依据。
>
> **后半段核心目标**：以最短路径补齐端侧 AI 部署 / LLM 推理的**核心技术能力**，每个交付物都是可独立展示的成果。

---

## 已完成里程碑（W1–W16）

> 紧凑回顾，一行一周；逐周细节见各模块 `notes.md`，阶段手册见 [`archive/Q1.md`](./archive/Q1.md)（W1–W13）与 [`archive/Q2.md`](./archive/Q2.md)（W14–W16）。

### 基石 — 工程基石与高性能体系（W1–W13，原 Q1）

| 周次 | 主题 | 关键技术 | 状态 |
|---|---|---|---|
| W1 | 内存安全与 RAII | Concepts、`std::expected`、`std::span` | ✅ |
| W2 | 移动语义与零拷贝 | 右值引用、`std::move`/`forward`、`std::span` | ✅ |
| W3 | C++20 约束 + 现代错误处理 | Concepts、`std::expected`、`string_view` | ✅ |
| W4 | 多线程与任务同步 | `counting_semaphore`、线程安全环形队列 | ✅ |
| W5 | 通用线程池架构 | `jthread`、`stop_token`、`alignas(64)` | ✅ |
| W6 | 高性能 I/O（mmap） | `mmap` 零拷贝、`std::span` | ✅ |
| W7 | CMake 工程化（I） | target-based、生成器表达式、C++20 模块 | ✅ |
| W8 | CMake（II）+ 自动化测试 | FetchContent、lcov 覆盖率（行 98.6%） | ✅ |
| W9 | OpenCV 像素级 + mdspan | `std::mdspan`、SIMD 初探 | ✅ |
| W10 | Resize / Letterbox 底层 | 插值数学、`LetterboxToTensor` | ✅ |
| W11 | 性能调优工具链 | gdb / valgrind / perf / 火焰图 | ✅ |
| W12 | Q1 复盘 + FAQ 库 | trade-offs、cheatsheet、interview_faq | ✅ |
| W13 | 阶段项目：多线程图像预处理引擎 | 全栈整合（线程池 + 手写算子） | ✅ |

### 推理 — ONNX Runtime 推理闭环（W14–W16，原 Q2 前段）

| 周次 | 主题 | 关键技术 | 状态 |
|---|---|---|---|
| W14 | ORT C++ 基础闭环 | RAII Session、`std::span` 零拷贝、`CreateTensor` | ✅ |
| W14.5 | CUDA ExecutionProvider | CUDA 12.3 + cuDNN 9 + ORT GPU 1.26（单帧 ~7.5×） | ✅ |
| W15 | 分类推理端到端闭环 | ImageNet 归一化、softmax/Top-K 编排 | ✅ |
| W16 | YOLOv8n 检测 Demo | 多输出头 + 手写 NMS + 坐标反算 + IOBinding/batch benchmark（对拍 ultralytics <0.001） | ✅ |

---

## 战略定位（技术维度）

项目的核心底座是 **C++ 高性能 + 推理部署工程内功**（ORT、CMake/CI、perf/valgrind 调优、jthread/mmap、零拷贝）。
CV 与 LLM 都是这个底座上的应用层，**不是二选一**，连接两者的桥是「量化」。

**权重决策**：**CV 收口（离现状最近）→ LLM 主攻（做深，战略重心）**。

- **CV 收口**：离现状最近（ORT + YOLOv8n 已完成），补上量化 + TensorRT 即形成完整的 CV 部署链路。已投入的 80% 不收口才叫浪费。
- **LLM 主攻**：应用前景与需求更大，是长期战略重心。
- **公共内功**：量化（INT8 / GGUF）、Profiling、TensorRT 三个方向通吃，先做不亏。

---

## 交付物 → 硬条件（技术维度）

> 一张表回答「为什么主线这么排」：每个交付物补什么**技术硬条件**。交付物为主键，按主线顺序排。

| 交付物 | 补的技术硬条件 |
|---|---|
| `quant` | **INT8 量化** + 前后处理 + 部署硬化 + C++ |
| `trt` | + **TensorRT FP16/INT8** + GPU 端到端 |
| `llm` | + **KV Cache + GGUF 量化 + llama.cpp + Transformer 推理** |
| `vllm` | + **vLLM/SGLang + OpenAI 兼容 API + PagedAttention** + 服务化落地 |
| `deploy` | + **Docker 容器化** + 部署文档 |

> **量化是三个方向的公共必修**，故排第一，一步同时推进 CV 收口与 LLM 地基。
> **NPU/国产芯栈**（昇腾 CANN/MindIE、地平线 BPU、自研算子映射）受限于无板子，作硬件门槛盲点进 Phase 3 弹性调研。

---

## 主线（W17 之后全部走此结构：Phase + 交付物里程碑）

> **两层结构**：**Phase** = 战略阶段；**交付物** = 阶段内一个可独立交付、且各自解锁一档能力里程碑。
> 交付物用主题名标识（即模块目录/命名空间名），**按内容定大小、不承诺时间**，做完即出报告并更新 README 作品主线。

### Phase 0 — CV 收口（完整 CV 部署链路 + LLM 地基）

#### 交付物 `quant` 🟡 核心与 GPU profiling 完成，入口退出 UAF 已修复（2026-09-11）：INT8 量化 + 部署硬化
- **范围**：
  - **量化**：ORT 官方工具对 YOLOv8n 做 PTQ，保留 MinMax / Entropy 接口；现有两产物字节相同，真正的策略对比移交 TRT M2。原要求“量化前先用 perf / Nsight 定位瓶颈”未执行，2026-09-10 已补做对照 profiling，不能改写历史顺序。
    剪枝/蒸馏作**概念覆盖**（量化/剪枝/蒸馏/图优化在部署场景常成套出现）——讲清三者定位差异即可，不动手实现。
  - **部署硬化**（承接 W16 评估出的部署形态缺口，均为小改高 ROI 项）：
    decode 预留容量（`reserve`，减少容量不足时的扩容；每帧仍新建 vector，未测分配次数）、复用 W14 现有 IOBinding 路径，
    `max_det` 上限 + NaN/Inf 鲁棒（`nms.cpp`/`decode.cpp`）、`Detect` 路径运行时分段埋点（pre / infer / post 滚动 P50/P99）、
    CUDA 不可用时**显式告警**而非静默回退。
    > 原设想“输入输出完整双绑 + buffer 池”未实现，本阶段决定不投入；完整方案收益未知。是否重启由后续 `trt` 的性能证据决定。
    > GPU 端预处理/后处理融图、FP16 路径属后端优化，归 `trt` 交付物，不在此（避免与 TRT engine 割裂）。
- **产出**：
  - 量化前后 **体积 / 延迟 / mAP 对比报告**（`docs/benchmarks/`）
  - 已有分段计时、五轮基准、Nsight 设备时间线与 ORT 节点分配，见 [瓶颈报告](benchmarks/quant_profiling_report.md)
  - 已有整体硬化 before/after、现有 IOBinding 对比与鲁棒性用例；GPU 硬化 before/after 已补；分配次数未验收
  - 模块：`02_Inference_Analysis/quantization/`（命名空间 `quant`）
- **成功指标与边界**：CPU INT8 延迟较 FP32 下降，mAP 掉点可量化并解释；能讲清对称/非对称、Per-Channel vs Per-Tensor，区分量化 vs 剪枝 vs 蒸馏。已测整体硬化效果不能归因于某一项 reserve；未测分配次数和完整 IOBinding 不计入已完成范围。
- **收口结论（2026-07-06）**：CPU 纯推理 INT8 快约 24%（端到端约 22%），coco128 mAP50-95 掉约 1.7 点（0.4454→0.4285）精度可用；MinMax 与 Entropy 产物实为同一模型（ORT Entropy 校准默认参数退化为 MinMax，2026-07-11 勘误，见 `quant_int8_report.md`），选 MinMax 更省内存；GPU 上 CUDA EP 跑 INT8（QDQ）为负优化（慢约 2×），INT8 GPU 加速移交 `trt`；IOBinding CPU/GPU 实测均噪声级，判定不再投入。报告见 `docs/benchmarks/quant_int8_report.md` 与 `quant_yolo_hardening.md`。
- **2026-09-10 收尾**：修正缺产物时的测试控制流、默认 32 图校准与模型目录复用，增加默认关闭的 NVTX 标记。切换执行权限并安装 Nsight 2025.1.3、处理 WSL 时间戳后完成设备采集：GPU Run 的 FP32/INT8 为 5.73/12.38 ms，kernel 325/843，额外 H2D 仅 17,728 字节，主要新增转换与小调用。一次退出堆损坏待查（`docs/tech-debt.md`）；CI 完整 ORT 构建/模型回归仍为独立工程任务。执行记录见 `docs/superpowers/plans/2026-09-10-quant-closeout.md`。
- **2026-09-11 稳定性修复**：Valgrind 定位到 static Env 晚于 CUDA 注册表析构，退出时再次访问已释放控制块。quant 入口显式持有并透传局部 Env，目标 UAF 上下文 2→0；三轮完整 GPU 基准正常退出，相关 10 个 CTest 目标通过。原始 SIGABRT 未再次捕获；旧 W14/W16 默认入口迁移仍列技术债。见 [生命周期报告](benchmarks/quant_runtime_lifetime.md)。
- 🎯 **已完成成果的表述**：「INT8 量化 + 部署硬化 + Nsight/ORT 性能归因」；不声称已实现 GPU INT8 加速、完整 IOBinding 或长期稳定性验收。
- ⚡ 同时是 LLM 端侧量化的地基

#### 交付物 `trt` 🟡 TensorRT C++ Engine（FP16 + INT8 Calibrator + GPU 端到端）
- **范围**：直接调 TRT C++ API（`IBuilder→INetworkDefinition→ICudaEngine→IExecutionContext`）建 engine；实现 `IInt8EntropyCalibrator2`；
  **GPU 端预处理/后处理**（letterbox + decode 上 GPU 或融图，消除 PCIe 往返——W16 CUDA EP 实测前后处理占端到端 ~22%；换 TRT FP16（纯 infer 2.93ms）后推算升至 ~44%，未实测；2026-11-16 勘误，原误写「W16 实测 ~45%」）
- **产出**：
  - **四路对比表**：CPU EP / CUDA EP / TRT FP16 / TRT INT8（体积 / 延迟 / mAP）
  - TRT 优化报告 + GPU 端到端流水线延迟拆解
  - 模块：`02_Inference_Analysis/tensorrt/`（命名空间 `trt`）
- **成功指标**：TRT INT8 延迟 < FP16；能讲清 TRT 与 ORT 量化路径差异；端到端（含前后处理）延迟较 W16 CUDA EP 明显下降
- 🔼 能力升级：多后端 CPU + CUDA + **TensorRT**，含 INT8 + GPU 端到端
- **M1 完成（2026-07-07）**：TRT 10.16 + FP16 engine 跑通（2.93ms，vs CUDA EP FP32 -48%）+ 一致性对拍 + 四路表骨架（`docs/benchmarks/trt_engine_report.md`）；M1.5（ORT TRT EP 参考对拍，2026-07-11 追加）与 M2 INT8 双路线待开始

### Phase 1 — LLM 端侧基础（主攻起步，做深）

#### 交付物 `llm` 🔴 llama.cpp 部署 + KV Cache + 源码精读
- **范围**：跑通小模型 chat demo；精读 `llama_decode` 推理路径；吃透 KV Cache（为何不能常规 batch）+ GGUF 量化类型（Q4_K_M / Q8_0，接 `quant` 量化内功）；读源码即落地 **Transformer 推理流程**理解
- **产出**：
  - C++ 调 `llama.h` 的命令行 chat demo
  - **KV Cache 显存账** + TTFT / tokens·s⁻¹ 实测报告
  - 模块：`04_System_Integration/llama_cpp/`（命名空间 `llm`）
- **成功指标**：能讲清 KV Cache 显存随上下文增长的账、prefill vs decode 阶段差异、GGUF 量化与 INT8 PTQ 的异同
- 🎯 **里程碑：LLM 基础打通**；服务化能力需再做 `vllm` + `deploy` 才完整

### Phase 2 — LLM 服务化 + 工程交付

#### 交付物 `vllm` 🔴 vLLM 部署 + OpenAI 兼容服务化（工业框架上手）
- **范围**：用 **vLLM** 起小模型推理服务，跑通 OpenAI-compatible API（SSE 流式）+ 并发；
  对照 `llm` 手写理解，吃透 **PagedAttention / continuous batching** 为何比朴素 KV Cache 高吞吐
- **产出**：
  - vLLM 服务 + OpenAI 兼容接口 + 压测报告（吞吐 / 延迟 / 显存占用）
  - **vLLM vs 朴素 llama.cpp 服务**吞吐对比；模块：`04_System_Integration/vllm_serving/`（命名空间 `vllm`）
- **成功指标**：能讲清 PagedAttention 显存碎片化解法、continuous batching 调度；能跑出 vLLM 高并发吞吐曲线
- 🔼 能力升级：工业级 LLM 推理框架 + 服务化落地

#### 交付物 `deploy` 🔴 Docker 容器化 + 部署文档
- **范围**：vLLM 服务打包成镜像；写标准化部署文档（含 GPU runtime 配置）
- **产出**：Docker 镜像 + 部署文档；模块：`04_System_Integration/deploy/`
- 🔼 能力升级：服务化 / 容器化交付（Docker / K8s）

### Phase 3 — 弹性深化池（不排死，按实际需求反馈决定补哪个）

按需求热度排候选，反馈回流到这里再决定：
- 🆕 **端侧 VLM 最简 demo（新信号里优先）**：具身 / VLA / VLM 方向升温、量产部署起势。直接复用本主线 CV(YOLOv8 视觉) + `llm`(KV Cache/GGUF) 内功——跑通一个小 VLM（如 Qwen-VL/LLaVA 系）端侧推理 demo，讲清「视觉编码 → 投影 → LLM 解码」链路 + 显存账即可。ROI 高，做完 `llm` 后接最顺。
- **昇腾 CANN/MindIE 调研 + demo**（NPU/昇腾/BPU 市场刚需；无板子→借昇腾社区免费算力补理论 + 跑通一个部署 demo，讲清原理与部署流程即可）
- **TensorRT-LLM 初探**（接 `trt` 的 TRT 内功 + `vllm` 视角）
- **SGLang / TGI 横向对比**（在 `vllm` 基础上补框架广度）
- **ARM / NCNN / RKNN 嵌入式**（需交叉编译 + 边缘板子，偏重）
- **自定义 CUDA 算子 / TRT Plugin · AI 编译器(MLIR/TVM)**（算子级优化方向：门槛最硬、最耗时，作中期观察放最后）

---

## 迭代节奏

每完成一个交付物 → 出报告并更新 README 作品主线 → 按反馈校准 Phase 3 / 下一步。

---

## 相比旧 Q2/Q3/Q4 砍掉了什么

| 砍掉 / 降弹性 | 原位置 | 原因 |
|---|---|---|
| Qt 上位机 | Q2 W22–26 | 与推理部署主线关联度低 |
| EdgeSight 全栈整合 | Q2 W26 | 依赖 Qt，非核心能力 |
| ARM / NCNN 嵌入式 | Q3 W31–33 | 偏重，需交叉编译环境，进弹性池 |
| 自定义 CUDA 算子 / Plugin | Q3 W29 | 最硬最耗时，降为弹性池末位 |
| MLC-LLM Android | Q4 W41 | 框架广度，非硬条件 |
| 技术博客 ×3、独立库重构周 | Q2/Q4 | 非核心能力 |

合计约 20 周删除或进弹性池，季度结构作废，统一成一条主线。

---

*每开始一个新交付物，同步更新 `CLAUDE.md` 当前进度与本表里程碑状态。*
