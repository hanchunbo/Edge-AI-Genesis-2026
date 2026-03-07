# 2026 AI Edge-Inference Breakthrough Plan

### —— 从业务开发到高级 AI 部署专家的职业跃迁

[![C++](https://img.shields.io/badge/C++-20/23-blue.svg?logo=c%2B%2B)](https://isocpp.org/)
[![TensorRT](https://img.shields.io/badge/TensorRT-8.x-green.svg?logo=nvidia)](https://developer.nvidia.com/tensorrt)
[![ONNX](https://img.shields.io/badge/ONNX-Runtime-purple.svg)](https://onnxruntime.ai/)
[![License](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

> **项目定位**：利用 2026 全年完成从"业务开发"到"高级 AI 部署"的系统性转型。本项目已全面进入 **C++20/23 时代**，深度结合现代 C++ 工程能力、数据分析洞察力与 MEM 项目管理思维，旨在攻克边缘端推理性能瓶颈，冲击 30K+ 高级岗位。

---

## 技术栈亮点 (C++20/23)

| 特性 | 应用场景 | 替代方案 |
|------|----------|----------|
| **std::jthread** | 线程池 RAII 管理 | std::thread + join |
| **std::stop_token** | 优雅停止机制 | std::atomic<bool> |
| **std::counting_semaphore** | 生产者-消费者同步 | condition_variable |
| **Concepts** | 模板类型约束 | SFINAE / enable_if |
| **std::expected** (C++23) | 零开销错误处理 | 异常 / 错误码 |
| **std::span** | 零拷贝数据视图 | 指针 + 长度 |
| **std::format** | 类型安全日志 | iostream / printf |
| **alignas(64)** | 缓存行对齐 | 手动 padding |

---

## 进度追踪

### Q1 进度 (2026.01 - 2026.03) `← 当前阶段`

| 周次 | 主题 | C++20+ 特性 | 状态 |
|------|------|-------------|------|
| W1 | 内存安全与RAII | Concepts, std::expected, std::span | ✅ 完成 |
| W2 | 移动语义与零拷贝 | std::span, std::format | ✅ 完成 |
| W3 | C++20特性实战 | std::format, string_view | ✅ 完成 |
| W4 | 多线程与任务同步 | counting_semaphore | ✅ 完成 |
| W5 | 通用线程池架构 | jthread, stop_token, alignas(64) | ✅ 完成 |
| W6 | 高性能I/O (mmap) | std::span | ✅ 完成（实现+测试+benchmark） |
| W7-W8 | CMake工程构建 | C++20 模块支持 | ⬜ 待开始 |
| W9-W11 | OpenCV底层实战 | std::mdspan (C++23) | ⬜ 待开始 |
| W12-W13 | 阶段项目 | 全栈整合 | ⬜ 待开始 |

---

## 2026 上半年求职导向里程碑（成功率优先）

| 时间窗口 | 必达产出 (P0) | 求职动作 |
|------|------|------|
| 2026-03 ~ 2026-04 | W6 benchmark + W7/W8 工程化完善 + W14 最小推理引擎 | 准备第一版简历、项目讲解稿、技术问答 |
| 2026-04-15 ~ 2026-05-15 | W15/W16 最小 YOLO 管线可演示 | 第一批投递（C++工程优化/推理基础部署岗） |
| 2026-06 ~ 2026-07 | W18 Profiling 报告 + W20 量化结果（可分步） | 第二批投递（端侧推理/量化优化岗） |

> 执行原则：先拿到面试与 offer，再补齐 Qt 上位机与完整产品化能力。

---

## 团队背景

| 成员 | 背景 | 专长领域 |
|------|------|----------|
| **Member A** (Tech Lead) | 某高校 MEM 在读，5年经验 | 系统调优、性能建模、量化算法 |
| **Member B** (Partner) | 某高校，3年 C++ Qt 经验 | 跨平台 UI、AI 上位机集成 |

---

## 开发环境

| 环境 | 配置详情 | 用途 |
|------|----------|------|
| **VPS** | x86 64 Bit, 2GB RAM, **GCC 13.4.0** | 远程开发、CI/CD |
| **本地笔记本** | 笔记本, i5-12500H, 16GB RAM, RTX 3060, 512GB SSD | 本地开发、GPU 推理测试 |

---

## 2026 路线图

| 季度 | 核心主题 | 关键技术 |
|------|----------|----------|
| **Q1** | 工程基石与高性能体系 | **C++20/23**、Linux I/O、CMake、OpenCV 底层 |
| **Q2** | 推理内功与数据分析 | ONNX Runtime、性能建模、量化理论、Qt 集成 |
| **Q3** | 硬件加速双栈突击 | TensorRT、NCNN、ARM NEON、YOLO 部署 |
| **Q4** | 系统集成与职场升维 | AI 微服务、Docker、云边协同、MEM 论文 |

详细执行手册见 [docs/Q1.md](./docs/Q1.md) | [docs/Q2.md](./docs/Q2.md)

---

## 项目结构

```
Edge-AI-Genesis-2026/
├── 01_Linux_CPP_Foundations/    # Q1 实战代码库 (C++20/23)
│   ├── w1_memory_safety/        # RAII + Concepts + std::expected
│   ├── w2_move_semantics/       # 移动语义 + std::span
│   ├── w3_filesystem/           # std::format + string_view
│   ├── w4_threading/            # counting_semaphore
│   ├── w5_thread_pool/          # jthread + stop_token + alignas(64)
│   └── w6_mmap_loader/          # mmap + std::span + std::expected
├── 02_Inference_Analysis/       # Q2 模型分析与性能报表
├── 03_Hardware_Acceleration/    # Q3 核心加速框架源码
├── 04_System_Integration/       # Q4 完整系统集成方案
└── docs/                        # 详细执行手册与基准测试
```

---

## 快速开始

```bash
# 克隆仓库
git clone https://github.com/hanchunbo/Edge-AI-Genesis-2026.git
cd Edge-AI-Genesis-2026

# 确保 GCC 13+ 已安装
g++-13 --version

# 构建 Q1 项目（以 W5 线程池为例）
cd 01_Linux_CPP_Foundations/w5_thread_pool
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# 运行测试
./thread_pool_test
```

---

## 工程复盘点

### 技术选型权衡
每季度末提交《技术决策文档》，例如分析低算力环境下选择 NCNN 而非 TensorRT 的决策逻辑。

### 代码评审标准 (C++20)

| 维度 | 审查要点 |
|------|----------|
| 内存零拷贝 | 是否使用 `std::span` 替代裸指针？ |
| 错误处理 | 是否使用 `std::expected` 替代异常？ |
| 线程安全 | 是否使用 `jthread`？停止机制是否基于 `stop_token`？ |
| 类型约束 | 模板是否使用 Concepts 约束？ |
| 缓存优化 | 热点变量是否使用 `alignas(64)` 对齐？ |

### 职业调研
- **周常任务**：调研 40W+ 岗位 JD，反馈到学习计划
- **面试沉淀**：建立高性能 C++ 与 AI 算子 FAQ 库

---

## 许可证

本项目采用 [MIT License](LICENSE) 开源协议。
