# 仓库知识库重组设计（方案甲）

**日期**：2026-06-15
**目标**：解决"可复用知识点散落在 14 个周笔记、复习时跨 week 找"的痛点。

## 问题

每个 week 的 `notes.md` 混了两类内容：
- **A·模块专属**：该模块的设计决策、Mermaid 架构图、踩坑、测试结果 —— 贴着代码才有意义。
- **B·可复用概念**：RAII、move、span、归一化等跨周通用知识 —— 复习时老得跨 week 翻。

痛点只在 B。按周（或按季度）切都是时间轴，找"零拷贝"这类主题仍是散的。

## 方案

新建 `docs/notes/`，**按主题**收纳 B（可复用概念），作为复习唯一正文；周笔记瘦身为 A + 向上链接。

### 主题文件（初拟 6 个 + 索引，边界可在执行时微调）

| 文件 | 收纳知识点 | 来源周 |
|---|---|---|
| `cpp-core.md` | RAII、移动语义/完美转发、`std::span`、Concepts、`std::expected`、`std::format`、`std::mdspan`、`std::filesystem` | W1/W2/W3/W9/W10 |
| `concurrency.md` | `jthread`/`stop_token`、线程池、`counting_semaphore` 生产者消费者、false sharing/`alignas(64)`、数据竞争检测 | W4/W5 |
| `systems-perf.md` | mmap 零拷贝/`madvise`、`perf`+火焰图、Valgrind 泄漏分级、GDB 多线程死锁、benchmark 方法 | W6/W11 |
| `image-ops.md` | 双线性插值、Letterbox、HWC2CHW、归一化、坐标对齐模式(HalfPixel/Asymmetric)、`cv::Mat::isContinuous` | W9/W10/W13/W15 |
| `inference.md` | ONNX vs ORT、RAII Session、Env 全局唯一、零拷贝张量输入、I/O 元数据缓存、图优化、softmax/Top-K | W14/W15 |
| `engineering.md` | CMake 三层结构/PUBLIC-PRIVATE-INTERFACE/生成器表达式/FetchContent、GTest 集成、gcov/lcov 覆盖率 | W7/W8 |
| `README.md` | 索引页：主题列表 + 每个主题下的知识点目录（锚点链接） | — |

### 知识点正文格式（主题文件内）

```markdown
### <概念名>

**是什么**：一句话定义。
**为什么 / 何时用**：动机与适用场景。
**坑**：易错点 / 边界条件。

> 实战出处：`01_Linux_CPP_Foundations/w5_thread_pool/notes.md`（完整设计与 Mermaid）
```

### 周笔记瘦身后

- **保留**：模块概述、Mermaid 架构图、本模块设计决策、踩坑、测试/基准结果。
- **删除并替换**：纯概念解释段落 → 一行链接 `> 概念详解见 docs/notes/concurrency.md#false-sharing`。
- **原则**：保留作者原话与理解，只搬运 + 精简措辞，**不臆改内容**。

### 链接方向

- 主题文件 → 周笔记（看完整语境 / Mermaid）。
- 周笔记 → 主题文件（看通用概念）。

### 不动

`docs/cpp20_23_cheatsheet.md`、`docs/interview_faq.md`、`docs/Q*.md`、Obsidian vault（vault 留私密内容）。

### 三份跨周文档分工（防止越整越乱）

- `cpp20_23_cheatsheet.md` = **语法速记**（怎么写）
- `interview_faq.md` = **问答自测**（面试怎么答）
- **`docs/notes/`（本方案）** = **概念详解/速查**（是什么+为什么+坑）

### 未来工作流规矩（写进 CLAUDE.md）

当周照常写周笔记；每周 / 季末把可复用概念"毕业"进 `docs/notes/` 主题文件，周笔记留链接。防止知识点再次散落。

## 执行策略

**先做 1 个主题试点**：`concurrency.md`（来自最大的 W4 495 行 + W5 546 行），并瘦身这两个周笔记。给用户看效果，确认风格与边界后，再批量处理其余主题。

## 验收

- `docs/notes/` 下 6 个主题文件 + README 索引建立。
- 14 个周笔记中的可复用概念已搬入对应主题文件，周笔记替换为链接、保留 Mermaid 与模块专属内容。
- 复习时：想找某概念，进 `docs/notes/README.md` → 主题文件即可读到正文，无需跨 week 翻。
- 内容无臆改（保留原作者表述）。
