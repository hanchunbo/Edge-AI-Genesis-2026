# 2026 AI Edge-Inference Breakthrough Plan

### —— 边缘端 AI 推理的 C++20/23 高性能工程实践

[![C++](https://img.shields.io/badge/C++-20/23-blue.svg?logo=c%2B%2B)](https://isocpp.org/)
[![TensorRT](https://img.shields.io/badge/TensorRT-10.16-green.svg?logo=nvidia)](https://developer.nvidia.com/tensorrt)
[![ONNX](https://img.shields.io/badge/ONNX-Runtime-purple.svg)](https://onnxruntime.ai/)
[![License](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

> **项目定位**：一套面向边缘端 AI 推理的 **C++20/23** 高性能工程实践。围绕现代 C++ 工程能力与系统级性能优化，攻克端侧推理的性能瓶颈，沉淀可复现、可演示的工程化作品集。

---

## 作品主线：YOLOv8n C++ 推理部署

> 从推理引擎到 TensorRT 的一条端到端 C++ 部署链路，每一步都有对拍或基准数据。
> 实测机器：i5-12500H + RTX 3060 Laptop（WSL2），ONNX Runtime 1.26、CUDA 12.3、TensorRT 10.16。

| 步骤 | 模块 | 做了什么 | 关键结果 |
|------|------|----------|----------|
| ① 推理引擎 | [`w14_ort_basics/`](02_Inference_Analysis/w14_ort_basics/) | ORT C++ API 的 RAII 封装：`std::span` 零拷贝输入、CPU / CUDA EP 切换与显式回退、IOBinding | 后续检测、量化、TRT 对拍共用的引擎层 |
| ② YOLOv8n 检测 | [`w16_yolo_detector/`](02_Inference_Analysis/w16_yolo_detector/) | Letterbox（复用 [W10 手写算子](01_Linux_CPP_Foundations/w10_resize/)）+ 输出头解码 + 手写逐类 NMS + 坐标反算；NMS / 解码拆成不依赖 ORT 的纯 C++ 库单独测试 | 逐框 score 与 ultralytics 对齐到 **<0.001**；纯推理 CPU 44ms → CUDA 5.9ms（约 7×），端到端 **94 FPS**（[基准](docs/benchmarks/w16_yolo_bench.md)） |
| ③ INT8 量化 + 部署硬化 | [`quantization/`](02_Inference_Analysis/quantization/) | ORT 静态 PTQ、coco128 mAP 评估 harness、分段 P50/P99 埋点、NaN/Inf 与 `max_det` 鲁棒性；Nsight + ORT 节点分配做性能归因；Valgrind 定位并修复入口退出期 UAF | CPU 纯推理快约 **24%**，mAP50-95 0.4454 → 0.4285（−0.017）；CUDA EP 上 INT8 反而慢约 2×，Nsight 显示 kernel 数 325 → 843，GPU INT8 移交 TRT（[量化报告](docs/benchmarks/quant_int8_report.md) · [瓶颈报告](docs/benchmarks/quant_profiling_report.md)） |
| ④ TensorRT | [`tensorrt/`](02_Inference_Analysis/tensorrt/) | 直接调 TRT C++ API 建 FP16 engine（缓存戳 + 动态维度 profile）、RAII GPU 缓冲、复用 ② 的前后处理做一致性对拍 | FP16 纯推理 **2.93ms**，比 CUDA EP FP32（5.64ms）快 48%（[报告](docs/benchmarks/trt_engine_report.md)） |

> 尚未完成：TRT INT8（Calibrator + mAP 闭环）与 GPU 端前后处理。数据口径与测量方法见各报告。

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

## 进度与路线图

| 阶段 | 内容 | 状态 |
|------|------|------|
| 基石（W1–W13） | C++20/23 系统编程：jthread/stop_token、mmap、`std::span`/`mdspan`、CMake、OpenCV 底层算子 | ✅ |
| 推理（W14–W16） | ONNX Runtime 推理闭环：ORT C++ API、前后处理、YOLOv8n 检测、CUDA EP | ✅ |
| 量化 `quant` | INT8 PTQ、部署硬化、Nsight / ORT 性能归因 | ✅ 核心完成 |
| TensorRT `trt` | FP16 engine + 一致性对拍已完成；INT8 Calibrator 与 GPU 端前后处理待做 | 🟡 |

后续方向：LLM 端侧推理（llama.cpp、KV Cache、GGUF）→ LLM 服务化（vLLM）→ 容器化部署。逐项里程碑见 [docs/Roadmap.md](./docs/Roadmap.md)。

---

## 开发环境

| 环境 | 配置详情 | 用途 |
|------|----------|------|
| **VPS** | x86 64 Bit, 2GB RAM, **GCC 15.2.0** | 远程开发（纯 CPU 构建） |
| **本地笔记本** | i5-12500H, 16GB RAM, RTX 3060, 512GB SSD | 本地开发、GPU 推理测试 |
| **CI** | GitHub Actions（Ubuntu 24.04） | 构建 + 单元测试（不含推理模块）+ clang-format 检查 |

---

## 项目结构

```
Edge-AI-Genesis-2026/
├── 01_Linux_CPP_Foundations/    # C++20/23 系统编程基石（W1–W13）
│   ├── w1_memory_safety/        # RAII + Concepts + std::expected
│   ├── w2_move_semantics/       # 移动语义 + std::span
│   ├── w3_filesystem/           # std::format + string_view
│   ├── w4_threading/            # counting_semaphore
│   ├── w5_thread_pool/          # jthread + stop_token + alignas(64)
│   ├── w6_mmap_loader/          # mmap + std::span + std::expected
│   ├── w7_cmake_engineering/    # CMake lib/app/tests 三层结构 + C++20 模块
│   ├── w8_testing/              # GTest + lcov 覆盖率
│   ├── w9_opencv_optimized/     # OpenCV 像素级操作 + std::mdspan
│   ├── w10_resize/              # 手写 Resize / Letterbox 算子
│   ├── w11_debugging/           # gdb / valgrind / perf / 火焰图
│   └── w13_image_pipeline/      # 多线程图像预处理引擎（W12 为复盘周，无代码目录）
├── 02_Inference_Analysis/       # 推理部署
│   ├── w14_ort_basics/          # ORT 推理引擎（RAII + CPU/CUDA EP + IOBinding）
│   ├── w15_classify_pipeline/   # MobileNetV2 分类端到端闭环
│   ├── w16_yolo_detector/       # YOLOv8n 检测（解码 + NMS + 对拍）
│   ├── quantization/            # INT8 PTQ + 部署硬化 + 性能归因（交付物 quant）
│   └── tensorrt/                # TensorRT C++ FP16 engine（交付物 trt）
├── docs/
│   ├── Roadmap.md               # 唯一执行主线
│   ├── setup.md                 # 环境搭建与构建指南
│   ├── benchmarks/              # 基准与量化 / TRT 报告
│   ├── notes/                   # 主题知识库
│   └── interview_faq.md         # 问答自测题库
└── third_party/                 # GTest 源码包、mdspan；ORT / TRT 需另装（见 docs/setup.md）
```

> **命名约定**：W1–W16 按周次命名，作为周志存档冻结；W17 起按主题命名（如 `quantization/`、`tensorrt/`），后续交付物同样按主题新建目录。

---

## 快速开始

最小构建（Ubuntu 24.04 / WSL2）：

```bash
# GCC 15 不在 Ubuntu 24.04 官方源，需加 PPA
sudo add-apt-repository -y ppa:ubuntu-toolchain-r/test
sudo apt-get update -q
sudo apt-get install -y g++-15 ninja-build cmake libopencv-dev

git clone https://github.com/hanchunbo/Edge-AI-Genesis-2026.git
cd Edge-AI-Genesis-2026
cmake -B build -S . -DCMAKE_CXX_COMPILER=g++-15 -G Ninja
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

> GTest 已随仓库内置（`third_party/v1.15.2.zip`），构建与测试全程无需网络。
> 推理模块依赖 ONNX Runtime / CUDA / TensorRT，未安装时 CMake 给出 warning 并跳过对应目标。
> 推理后端安装、YOLOv8n 模型导出、覆盖率与 Sanitizer 用法见 [docs/setup.md](docs/setup.md)。

---

## 代码规范

Google C++ Style（`.clang-format` / `.clang-tidy` 自动检查，CI 用 clang-format-21 校验格式）。评审要点：

| 维度 | 审查要点 |
|------|----------|
| 内存零拷贝 | 是否使用 `std::span` 替代裸指针？ |
| 错误处理 | 是否使用 `std::expected` 替代异常？ |
| 线程安全 | 是否使用 `jthread`？停止机制是否基于 `stop_token`？ |
| 类型约束 | 模板是否使用 Concepts 约束？ |
| 缓存优化 | 热点变量是否使用 `alignas(64)` 对齐？ |

---

## 许可证

本项目采用 [MIT License](LICENSE) 开源协议。
