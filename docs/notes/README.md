# docs/notes —— 主题知识库（概念详解 / 速查）

> **定位**：可复用概念按**主题**收纳，作为复习唯一正文。解决「知识点散落在周笔记、跨 week 找」的痛点。
> 设计见 `docs/superpowers/specs/2026-06-15-notes-knowledge-base-design.md`（方案甲）。
>
> 三份跨周文档分工：
> - `docs/cpp20_23_cheatsheet.md` = 语法速记（怎么写）
> - `docs/interview_faq.md` = 问答自测（面试怎么答）
> - **`docs/notes/`（本目录）** = 概念详解（是什么 + 为什么 + 坑）

## 主题文件

| 文件 | 收纳主题 | 来源周 | 状态 |
|---|---|---|---|
| [inference.md](inference.md) | ONNX vs ORT、shape/tensor/Ort::Value、Env/Session/Engine、Env 全局唯一、零拷贝输入、CPU/GPU 内存 + Host→Device、I/O 元数据缓存、EP 优雅回退、Softmax、Top-K | W14/W15 | ✅ W14 + W15 后处理（softmax/Top-K）已入 |
| [cpp-core.md](cpp-core.md) | static 四种用法、ABI vs API（RAII/移动语义/span/Concepts/expected/format 待毕业） | W1/W2/W3/W9/W10 | 🟡 部分（W14 答疑的 static/ABI 已入） |
| `concurrency.md` | jthread/stop_token、线程池、counting_semaphore、false sharing/alignas(64)、数据竞争检测 | W4/W5 | ⬜ 计划中 |
| `systems-perf.md` | mmap 零拷贝/madvise、perf+火焰图、Valgrind、GDB 多线程死锁、benchmark 方法 | W6/W11 | ⬜ 计划中 |
| [image-ops.md](image-ops.md) | 分类预处理范式、ImageNet 归一化、BGR↔RGB、HWC↔CHW、silent 预处理 bug；（双线性插值、Letterbox、坐标对齐、isContinuous 待补） | W9/W10/W13/W15 | 🟡 部分（W15 已入；W9/10/13 待毕业） |
| `engineering.md` | CMake 三层结构/生成器表达式/FetchContent、GTest 集成、gcov/lcov 覆盖率 | W7/W8 | ⬜ 计划中 |

## 知识点格式

每个概念按统一三段式（+ 实战出处链接）：

```markdown
### <概念名>
**是什么**：一句话定义。
**为什么 / 何时用**：动机与适用场景。
**坑**：易错点 / 边界条件。
> 实战出处：`<周笔记路径>`（完整设计与 Mermaid）
```

## 链接方向

- 主题文件 → 周笔记（看完整语境 / Mermaid）
- 周笔记 → 主题文件（看通用概念）

## 工作流

当周照常写周笔记；每周 / 季末把可复用概念「毕业」进对应主题文件，周笔记留向上链接。防止知识点再次散落。
