# 2026 AI Edge-Inference Breakthrough Plan

### —— 从业务开发到高级 AI 部署专家的职业跃迁

[![C++](https://img.shields.io/badge/C++-17/20-blue.svg?logo=c%2B%2B)](https://isocpp.org/)
[![TensorRT](https://img.shields.io/badge/TensorRT-8.x-green.svg?logo=nvidia)](https://developer.nvidia.com/tensorrt)
[![ONNX](https://img.shields.io/badge/ONNX-Runtime-purple.svg)](https://onnxruntime.ai/)
[![License](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

> **项目定位**：利用 2026 全年完成从"业务开发"到"高级 AI 部署"的系统性转型。本计划深度结合 C++ 底层工程能力、数据分析洞察力与 MEM 项目管理思维，旨在攻克边缘端推理性能瓶颈，冲击 30K+ 高级岗位。

---

## 进度追踪

### Q1 进度 (2026.01 - 2026.03) `← 当前阶段`

| 周次 | 主题 | 状态 | 产出物 |
|------|------|------|--------|
| W1 | 内存安全与RAII | ✅ 完成 | `SafeTensorBuffer` |
| W2 | 移动语义与零拷贝 | ✅ 完成 | 性能Benchmark |
| W3 | C++17特性实战 | ✅ 完成 | 模型扫描器 |
| W4 | 多线程与任务同步 | 🔄 进行中 | 线程安全队列 |
| W5 | 通用线程池架构 | ⬜ 待开始 | `ThreadPool` |
| W6 | 高性能I/O (mmap) | ⬜ 待开始 | 文件加载对比 |
| W7-W8 | CMake工程构建 | ⬜ 待开始 | 标准化模板 |
| W9-W11 | OpenCV底层实战 | ⬜ 待开始 | 手写算子 |
| W12-W13 | 阶段项目 | ⬜ 待开始 | 预处理引擎 |

---

## 团队背景

| 成员 | 背景 | 专长领域 |
|------|------|----------|
| **Member A** (Tech Lead) | 西工大 MEM 在读，5年经验 | 系统调优、性能建模、量化算法 |
| **Member B** (Partner) | 西安石油大学，3年 C++ Qt 经验 | 跨平台 UI、AI 上位机集成 |

---

## 开发环境

| 环境 | 配置详情 | 用途 |
|------|----------|------|
| **VPS** | x86 32/64 Bit, 2GB RAM, 1 CPU, 30GB Storage | 远程开发、CI/CD |
| **本地笔记本** | 联想拯救者 Y9000P, i5-12500H, 16GB RAM, RTX 3060, 512GB SSD | 本地开发、GPU 推理测试 |

---

## 2026 路线图

| 季度 | 核心主题 | 关键技术 |
|------|----------|----------|
| **Q1** | 工程基石与高性能体系 | 现代 C++、Linux I/O、CMake、OpenCV 底层 |
| **Q2** | 推理内功与数据分析 | ONNX Runtime、性能建模、量化理论、Qt 集成 |
| **Q3** | 硬件加速双栈突击 | TensorRT、NCNN、ARM NEON、YOLO 部署 |
| **Q4** | 系统集成与职场升维 | AI 微服务、Docker、云边协同、MEM 论文 |

详细执行手册见 [docs/Q1.md](./docs/Q1.md)

---

## 项目结构

```
Edge-AI-Genesis-2026/
├── 01_Linux_CPP_Foundations/    # Q1 实战代码库
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

# 构建 Q1 项目
cd 01_Linux_CPP_Foundations
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

---

## 工程复盘点

### 技术选型权衡
每季度末提交《技术决策文档》，例如分析低算力环境下选择 NCNN 而非 TensorRT 的决策逻辑。

### 代码评审标准

| 维度 | 审查要点 |
|------|----------|
| 内存零拷贝 | 是否通过移动语义避免了不必要的拷贝？ |
| 内存对齐 | 循环展开与内存对齐是否优化？ |
| 线程安全 | 并发环境下锁的粒度是否足够细？ |

### 职业调研
- **周常任务**：调研 40W+ 岗位 JD，反馈到学习计划
- **面试沉淀**：建立高性能 C++ 与 AI 算子 FAQ 库

---

## 许可证

本项目采用 [MIT License](LICENSE) 开源协议。