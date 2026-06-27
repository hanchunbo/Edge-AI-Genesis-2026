# 求职最短路径主线（Job Fast-Track Roadmap）

> **本文件是 W16 之后的唯一执行主线，作废原 Q2/Q3/Q4 季度结构。**
> 旧季度手册已归档至 `docs/archive/`（Q2/Q3/Q4.md）留作素材参考，不再作为执行依据。
>
> **核心目标**：以最快路径补齐「通往西安高薪端侧 AI 部署/LLM 岗」的硬条件，边做边投。
> **决策依据**：`D:\1-usual\Notes\cv\JD库与匹配分析_20260627.md`（三平台真实 JD 聚合）。

---

## 战略定位（2026-06-27 校准）

候选人真正的资产是 **C++ 高性能 + 推理部署工程内功**（ORT、CMake/CI、perf/valgrind 调优、jthread/mmap、零拷贝）。
CV 与 LLM 都是这个底座上的应用层，**不是二选一**，连接两者的桥是「量化」。

**权重决策**：**CV 收口（2 周变现）→ LLM 主攻（做深，战略重心）**。

- **CV 收口**：离现状最近（ORT + YOLOv8n 已完成），只差量化 + TensorRT 即可投最对口岗。已投入的 80% 不收口才叫浪费。
- **LLM 主攻**：前景、薪资更高，且**对非 211 学历友好**（影微「学历经验不限」绕开了睿创 J11308 那种「本硕均 211」硬门槛）——这对候选人背景是战略性的。西安本轮最高薪岗全在 LLM。
- **公共内功**：量化（INT8 / GGUF）、Profiling、TensorRT 三个簇通吃，先做不亏。

> **风险提示**：LLM 赛道更卷，demo 要做深（KV Cache 显存账、量化吞吐实测要能讲透）才有说服力；紫光那种「完整 LLM 落地经历」硬门槛一个 demo 够不到，现实目标瞄准影微这类「门槛不限」的岗。

---

## 高薪岗的硬条件反推（决定主线必须覆盖什么）

| 岗位簇 | 代表岗（薪资） | 卡的硬条件 |
|---|---|---|
| CV 部署（收口） | 睿创 J11352（20–40K）、知象（30–45K，⚠️见备注） | **量化 + TensorRT** + 前后处理 + C++ |
| LLM 端侧（门槛低·主攻入口） | 影微（25–50K，**学历经验不限**） | **KV Cache + 量化 + llama.cpp + Transformer 推理理解 + C++** |
| LLM 服务化（高薪收尾） | 紫光（面议高） | 上一行 + **OpenAI 兼容 API + Docker + 服务化落地** |

> **量化是三个簇的公共必修**，故排第一，一步同时推进 CV 收口与 LLM 地基。
> **⚠️ 知象备注**：技术面通过后被放鸽子，诚信存疑，**降级为备胎**，不为其调整节奏；CV 收口的对口锚点以睿创 J11352 为准。

---

## 主线（W17 起，替换 W16 之后全部计划）

### Phase 0 — CV 收口（W17–W18｜变现最快 + LLM 地基）

#### W17 🔴 INT8 量化 + Profiling 报告
- **范围**：ORT 官方工具对 YOLOv8n 做 PTQ（MinMax / Entropy 两策略）；量化前先用 perf / Nsight 定位瓶颈
- **产出**：
  - 量化前后 **体积 / 延迟 / mAP 对比报告**（`docs/benchmarks/`）
  - Profiling 报告（瓶颈归因 + 优化方向）
  - 模块：`02_Inference_Analysis/w17_quantization/`
- **成功指标**：INT8 推理延迟较 FP32 显著下降，mAP 掉点可量化并解释；能讲清对称/非对称、Per-Channel vs Per-Tensor
- 🎯 **里程碑：CV 簇可投**（睿创 J11352）；简历挂「INT8 量化 + Profiling」
- ⚡ 同时是 LLM 端侧量化的地基

#### W18 🔴 TensorRT C++ Engine（FP16 + INT8 Calibrator）
- **范围**：直接调 TRT C++ API（`IBuilder→INetworkDefinition→ICudaEngine→IExecutionContext`）建 engine；实现 `IInt8EntropyCalibrator2`
- **产出**：
  - **四路对比表**：CPU EP / CUDA EP / TRT FP16 / TRT INT8（体积 / 延迟 / mAP）
  - TRT 优化报告
  - 模块：`02_Inference_Analysis/w18_tensorrt/`
- **成功指标**：TRT INT8 延迟 < FP16；能讲清 TRT 与 ORT 量化路径差异
- 🔼 CV 簇硬问题扛得住；简历升级「多后端 CPU+CUDA+**TensorRT**，含 INT8」

### Phase 1 — LLM 端侧基础（W19｜主攻起步，做深）

#### W19 🔴 llama.cpp 部署 + KV Cache + 源码精读
- **范围**：跑通小模型 chat demo；精读 `llama_decode` 推理路径；吃透 KV Cache（为何不能常规 batch）+ GGUF 量化类型（Q4_K_M / Q8_0，接 W17 量化内功）；读源码即落地 **Transformer 推理流程**理解
- **产出**：
  - C++ 调 `llama.h` 的命令行 chat demo
  - **KV Cache 显存账** + TTFT / tokens·s⁻¹ 实测报告
  - 模块：`04_System_Integration/w19_llama_cpp/`
- **成功指标**：能讲清 KV Cache 显存随上下文增长的账、prefill vs decode 阶段差异、GGUF 量化与 INT8 PTQ 的异同
- 🎯 **里程碑：LLM 簇可投**（影微——门槛不限，正好够得到）

### Phase 2 — LLM 服务化 + 工程交付（W20–W21｜命中紫光级硬条件）

#### W20 🔴 LLM 流式推理服务化（OpenAI-compatible）
- **范围**：基于 llama.cpp 封装 OpenAI-compatible REST（SSE 流式）+ 基础并发
- **产出**：能跑的兼容服务 + 接口文档；模块：`04_System_Integration/w20_llm_serving/`
- 🔼 紫光「兼容 OpenAI API」直命中

#### W21 🔴 Docker 容器化 + 部署文档
- **范围**：服务打包成镜像；写标准化部署文档
- **产出**：Docker 镜像 + 部署文档
- 🔼 紫光「Docker/K8s」、中科昇腾「镜像编排」命中；MEM 工程交付视角加分

### Phase 3 — 弹性深化池（不排死，由「边做边投」面试反馈决定补哪个）

按命中频率排候选，反馈回流到这里再决定：
- **vLLM 部署体验**（影微 / 紫光点名，框架广度）
- **TensorRT-LLM 初探**（紫光点名，接 W18 的 TRT 内功）
- **自定义 CUDA 算子 / TRT Plugin**（小米 / 知象「算子优化」加分，最硬最耗时，放最后）

---

## 投递里程碑阶梯（边做边投的变现节奏）

| 完成 | 立刻能投 | 薪资 |
|---|---|---|
| W17 | 睿创 J11352（CV 部署） | 20–40K |
| W18 | 强化 CV 簇（小米异构亦可冲刺） | 25–55K |
| W19 | 影微（LLM 端侧，门槛不限） | 25–50K |
| W21 | 紫光（LLM 服务化落地） | 面议（高） |

每完成一周 → 更新对应版本简历 → 投一批 → 用面试反馈校准 Phase 3 / 下一步。

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

*数据来源与匹配分析详见 `D:\1-usual\Notes\cv\JD库与匹配分析_20260627.md`。每开始新 week，同步更新 `CLAUDE.md` 当前进度与本表里程碑状态。*
