# 总路线图（W1–W16 周志存档 · W17 起 Phase 主线）

> **本文件是项目唯一执行主线**。**结构分两段**：
> - **W1–W16（存档）**：已完成，按周记录，冻结回顾，不再改动。
> - **W17 起（前向）**：走「求职最短路径」，采用 **Phase + 交付物里程碑** 两层结构——**不再按周切分**，交付物按内容定大小、各自解锁一档可投岗位。
>
> 作废原 Q2/Q3/Q4 季度结构。旧季度手册已归档至 `docs/archive/`（Q2/Q3/Q4.md）留作素材参考，不再作为执行依据。
>
> **后半段核心目标**：以最快路径补齐「通往西安高薪端侧 AI 部署/LLM 岗」的硬条件，边做边投。
> **决策依据**：`D:\1-usual\Notes\cv\岗位趋势小结_20260627.md` + `岗位清单_多城市_三平台_20260627.xlsx`（猎聘/BOSS×西安/北京/深圳，30 份完整 JD 聚合）。

---

## 已完成里程碑（W1–W16）

> 紧凑回顾，一行一周；逐周细节见各模块 `notes.md`，阶段手册见 [`Q1.md`](./Q1.md)（W1–W13）与 [`archive/Q2.md`](./archive/Q2.md)（W14–W16）。

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

## 战略定位（2026-06-27 校准）

候选人真正的资产是 **C++ 高性能 + 推理部署工程内功**（ORT、CMake/CI、perf/valgrind 调优、jthread/mmap、零拷贝）。
CV 与 LLM 都是这个底座上的应用层，**不是二选一**，连接两者的桥是「量化」。

**权重决策**：**CV 收口（2 周变现）→ LLM 主攻（做深，战略重心）**。

- **CV 收口**：离现状最近（ORT + YOLOv8n 已完成），只差量化 + TensorRT 即可投最对口岗。已投入的 80% 不收口才叫浪费。
- **LLM 主攻**：前景、薪资更高，且有少数**经验/学历不限**的入口岗对候选人背景友好——西安以 **紫光国芯 AI 应用部署(大模型方向，经验不限/本科)** 为真·门槛锚点。西安本轮最高薪岗全在 LLM。
  > ⚠️ **2026-06-28 复核纠错**：早前把「影微」当门槛不限锚点是错的——影微实为**大模型推理优化(NPU 方向)，2 年以上**，要自研 NPU 算子映射/runtime，属下表 NPU 簇（硬件门槛，进弹性），**本主线 demo 拿不到影微**。
- **公共内功**：量化（INT8 / GGUF）、Profiling、TensorRT 三个簇通吃，先做不亏。

> **风险提示（含经验年限天花板）**：① LLM 赛道更卷，demo 要做深（KV Cache 显存账、量化吞吐实测要能讲透）才有说服力。② **经验年限是结构性天花板**：目标岗多为 3-5 年 / 2 年以上，demo 补不了年限；真·**经验不限/学历不限**的岗才是主投靶子（西安紫光、北汽云端/端侧 50-70K、个别 NPU 猎头岗）。③ 里程碑的现实定位是**送进面试间 + 谈得有料**，不是「一投即中」——20-40K 档可稳过筛，50-70K 的国产芯/分布式/定制框架档靠这几个 demo 够不到（经验+硬件+深度三重门槛）。**优先按「经验不限岗」投，其余按面试反馈校准。**

---

## 高薪岗的硬条件反推（2026-06-27岗位清单）

> **一张表回答两件事**：每个交付物**补什么硬条件**（→ 为什么主线这么排）+ **解锁什么岗位**（→ 做完投什么）。
> 岗位/薪资源自 2026-06-27 岗位清单（30 份 JD）。**交付物为主键，按主线顺序排**；末行 NPU 簇无 demo，进 Phase 3 弹性。

| 交付物 | 解锁岗位簇 · 代表岗（薪资） | 该交付物补的硬条件 |
|---|---|---|
| `quant` | CV 部署：睿创 J11352（20–40K）；知象 30–45K（备胎，⚠️见注） | **INT8 量化** + 前后处理 + 部署硬化 + C++ |
| `trt` | CV 部署强化（小米异构等，25–55K） | + **TensorRT FP16/INT8** + GPU 端到端 |
| `llm` | LLM 端侧·门槛低入口：**紫光国芯 AI 应用部署起步（面议，经验不限/本科）**、珠海芯动力（15–30K，硕士） | **KV Cache + GGUF 量化 + llama.cpp + Transformer 推理** + C++ |
| `vllm` | LLM 服务化：燧原/朗坤/小儿方（30–70K） | + **vLLM/SGLang + OpenAI 兼容 API + PagedAttention** + 服务化落地 |
| `deploy` | LLM 服务化收尾：紫光（面议高） | + **Docker 容器化** + 部署文档 |
| —（无 demo） | NPU/国产芯（量大·硬件门槛）：睿创 J11308、影微、中科昇腾（10–60K） | 昇腾 CANN/MindIE、地平线 BPU、自研 NPU 算子映射——⚠️无板子，进 Phase 3 弹性 |

> **量化是三个簇的公共必修**，故排第一，一步同时推进 CV 收口与 LLM 地基。
> **vLLM 提示（2026-06-27 JD 复核）**：30 份 JD 聚合里 vLLM/SGLang/TGI 频次 26、PagedAttention/投机解码 14，高薪 LLM Infra 岗（燧原/朗坤/寒武纪）点名要会用工业框架，而非手搓兼容层——故 vLLM 已从弹性升进主线 Phase 2（交付物 `vllm`/`deploy`）。
> **NPU 提示**：NPU/昇腾/CANN/BPU 频次 43（排名 3，西安核心盘刚需），但受限于无 Atlas/Jetson/地平线板子，作硬件门槛盲点进 Phase 3 弹性调研（昇腾社区免费算力可补理论 + 一个 demo）。
> **⚠️ 知象备注**：技术面通过后被放鸽子，诚信存疑，**降级为备胎**，不为其调整节奏；CV 收口的对口锚点以睿创 J11352 为准。

---

## 主线（W17 之后全部走此结构：Phase + 交付物里程碑）

> **两层结构**：**Phase** = 战略阶段；**交付物** = 阶段内一个可独立交付、且各自解锁一档可投岗位的里程碑。
> 交付物用主题名标识（即模块目录/命名空间名），**按内容定大小、不承诺时间**，做完即更新简历投一批。

### Phase 0 — CV 收口（变现最快 + LLM 地基）

#### 交付物 `quant` 🔴 INT8 量化 + 部署硬化 + Profiling 报告
- **范围**：
  - **量化**：ORT 官方工具对 YOLOv8n 做 PTQ（MinMax / Entropy 两策略）；量化前先用 perf / Nsight 定位瓶颈。
    剪枝/蒸馏作**概念覆盖**（JD 里量化/剪枝/蒸馏/图优化常成套出现，频次 29）——讲清三者定位差异即可，不动手实现。
  - **部署硬化**（承接 W16 评估出的部署形态缺口，均为小改高 ROI 项）：
    decode 预留容量（`reserve`，消除逐帧 realloc）、输入侧 IOBinding 复用（输入输出**双绑** + buffer 池，让 W16「噪声级」收益变实）、
    `max_det` 上限 + NaN/Inf 鲁棒（`nms.cpp`/`decode.cpp`）、`Detect` 路径运行时分段埋点（pre / infer / post 滚动 P50/P99）、
    CUDA 不可用时**显式告警**而非静默回退。
    > GPU 端预处理/后处理融图、FP16 路径属后端优化，归 `trt` 交付物，不在此（避免与 TRT engine 割裂）。
- **产出**：
  - 量化前后 **体积 / 延迟 / mAP 对比报告**（`docs/benchmarks/`）
  - Profiling 报告（瓶颈归因 + 优化方向）
  - 部署硬化项 before/after 数字（分配次数 / IOBinding 实测收益 / 鲁棒性用例）
  - 模块：`02_Inference_Analysis/quantization/`（命名空间 `quant`）
- **成功指标**：INT8 推理延迟较 FP32 显著下降，mAP 掉点可量化并解释；能讲清对称/非对称、Per-Channel vs Per-Tensor；能区分量化 vs 剪枝 vs 蒸馏；部署硬化项有 before/after 数字支撑
- 🎯 **里程碑：CV 簇可投**（睿创 J11352）；简历挂「INT8 量化 + Profiling + 部署硬化」
- ⚡ 同时是 LLM 端侧量化的地基

#### 交付物 `trt` 🔴 TensorRT C++ Engine（FP16 + INT8 Calibrator + GPU 端到端）
- **范围**：直接调 TRT C++ API（`IBuilder→INetworkDefinition→ICudaEngine→IExecutionContext`）建 engine；实现 `IInt8EntropyCalibrator2`；
  **GPU 端预处理/后处理**（letterbox + decode 上 GPU 或融图，消除 PCIe 往返——W16 实测前后处理占端到端 ~45%）
- **产出**：
  - **四路对比表**：CPU EP / CUDA EP / TRT FP16 / TRT INT8（体积 / 延迟 / mAP）
  - TRT 优化报告 + GPU 端到端流水线延迟拆解
  - 模块：`02_Inference_Analysis/tensorrt/`（命名空间 `trt`）
- **成功指标**：TRT INT8 延迟 < FP16；能讲清 TRT 与 ORT 量化路径差异；端到端（含前后处理）延迟较 W16 CUDA EP 明显下降
- 🔼 CV 簇硬问题扛得住；简历升级「多后端 CPU+CUDA+**TensorRT**，含 INT8 + GPU 端到端」

### Phase 1 — LLM 端侧基础（主攻起步，做深）

#### 交付物 `llm` 🔴 llama.cpp 部署 + KV Cache + 源码精读
- **范围**：跑通小模型 chat demo；精读 `llama_decode` 推理路径；吃透 KV Cache（为何不能常规 batch）+ GGUF 量化类型（Q4_K_M / Q8_0，接 `quant` 量化内功）；读源码即落地 **Transformer 推理流程**理解
- **产出**：
  - C++ 调 `llama.h` 的命令行 chat demo
  - **KV Cache 显存账** + TTFT / tokens·s⁻¹ 实测报告
  - 模块：`04_System_Integration/llama_cpp/`（命名空间 `llm`）
- **成功指标**：能讲清 KV Cache 显存随上下文增长的账、prefill vs decode 阶段差异、GGUF 量化与 INT8 PTQ 的异同
- 🎯 **里程碑：LLM 基础打通**，可投门槛不限的 LLM 应用/部署入门岗（紫光应用部署起步、珠海芯动力）；紫光主体要服务化，做完 `vllm`+`deploy` 才完整够得上

### Phase 2 — LLM 服务化 + 工程交付（命中紫光/燧原级硬条件）

#### 交付物 `vllm` 🔴 vLLM 部署 + OpenAI 兼容服务化（工业框架上手）
- **范围**：用 **vLLM** 起小模型推理服务，跑通 OpenAI-compatible API（SSE 流式）+ 并发；
  对照 `llm` 手写理解，吃透 **PagedAttention / continuous batching** 为何比朴素 KV Cache 高吞吐
- **产出**：
  - vLLM 服务 + OpenAI 兼容接口 + 压测报告（吞吐 / 延迟 / 显存占用）
  - **vLLM vs 朴素 llama.cpp 服务**吞吐对比；模块：`04_System_Integration/vllm_serving/`（命名空间 `vllm`）
- **成功指标**：能讲清 PagedAttention 显存碎片化解法、continuous batching 调度；能跑出 vLLM 高并发吞吐曲线
- 🔼 燧原/朗坤/小儿方/寒武纪「vLLM/SGLang/TGI + PagedAttention」直命中（频次 26/14）；简历挂「工业级 LLM 推理框架」

#### 交付物 `deploy` 🔴 Docker 容器化 + 部署文档
- **范围**：vLLM 服务打包成镜像；写标准化部署文档（含 GPU runtime 配置）
- **产出**：Docker 镜像 + 部署文档；模块：`04_System_Integration/deploy/`
- 🔼 紫光「Docker/K8s」、中科昇腾「镜像编排」命中（Docker/K8s/MaaS 频次 28）；工程交付视角加分

### Phase 3 — 弹性深化池（不排死，由「边做边投」面试反馈决定补哪个）

按命中频率排候选，反馈回流到这里再决定：
- **昇腾 CANN/MindIE 调研 + demo**（NPU/昇腾/BPU 频次 43，西安核心盘刚需；无板子→借昇腾社区免费算力补理论 + 跑通一个部署 demo，面试能聊即可）
- **TensorRT-LLM 初探**（紫光点名，接 `trt` 的 TRT 内功 + `vllm` 视角）
- **SGLang / TGI 横向对比**（在 `vllm` 基础上补框架广度）
- **ARM / NCNN / RKNN 嵌入式**（频次 26，需交叉编译 + 边缘板子，偏重）
- **自定义 CUDA 算子 / TRT Plugin**（小米 / 知象「算子优化」加分，最硬最耗时，放最后）

---

## 投递节奏（边做边投）

每完成一个交付物 → 更新对应版本简历 → 投一批 → 用面试反馈校准 Phase 3 / 下一步。
**各交付物解锁的岗位与薪资见上方「[高薪岗的硬条件反推](#高薪岗的硬条件反推2026-06-27岗位清单)」矩阵。**

---

## 相比旧 Q2/Q3/Q4 砍掉了什么

| 砍掉 / 降弹性 | 原位置 | 原因 |
|---|---|---|
| Qt 上位机 | Q2 W22–26 | 8 个对口 JD 里 0 命中 |
| EdgeSight 全栈整合 | Q2 W26 | 依赖 Qt，非求职硬条件 |
| ARM / NCNN 嵌入式 | Q3 W31–33 | 命中 3 JD 但偏重，需交叉编译环境，进弹性池 |
| 自定义 CUDA 算子 / Plugin | Q3 W29 | 最硬最耗时，降为弹性池末位 |
| MLC-LLM Android | Q4 W41 | 框架广度，非硬条件 |
| 技术博客 ×3、独立库重构周 | Q2/Q4 | 非求职硬条件 |

合计约 20 周删除或进弹性池，季度结构作废，统一成本主线一条。

---

*数据来源详见 `D:\1-usual\Notes\cv\岗位趋势小结_20260627.md`（30 份完整 JD 聚合 + 技能词频）与同目录 xlsx。每开始一个新交付物，同步更新 `CLAUDE.md` 当前进度与本表里程碑状态。*
