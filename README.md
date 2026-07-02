# 2026 AI Edge-Inference Breakthrough Plan

### —— 边缘端 AI 推理的 C++20/23 高性能工程实践

[![C++](https://img.shields.io/badge/C++-20/23-blue.svg?logo=c%2B%2B)](https://isocpp.org/)
[![TensorRT](https://img.shields.io/badge/TensorRT-8.x-green.svg?logo=nvidia)](https://developer.nvidia.com/tensorrt)
[![ONNX](https://img.shields.io/badge/ONNX-Runtime-purple.svg)](https://onnxruntime.ai/)
[![License](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

> **项目定位**：一套面向边缘端 AI 推理的 **C++20/23** 高性能工程实践。围绕现代 C++ 工程能力与系统级性能优化，逐周攻克端侧推理的性能瓶颈，沉淀可复现、可演示的工程化作品集。

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

> 单一执行主线见 [docs/Roadmap.md](./docs/Roadmap.md)（W1→W21 全程）。下表为镜像总览。
> **2026-06-27 重构**：W16 后作废季度结构，改走「求职最短路径主线」——CV 收口 2 周 → LLM 主攻做深，边做边投。

### 已完成（W1–W16）

| 阶段 | 周次 | 核心主题 | 关键技术 | 状态 |
|------|------|----------|----------|------|
| 基石（原 Q1） | W1–W13 | 工程基石与高性能体系 | jthread/stop_token、mmap、`std::span`/`mdspan`、CMake、OpenCV 底层算子 | ✅ |
| 推理（原 Q2 前段） | W14–W16 | ONNX Runtime 推理闭环 | ORT C++ API、前后处理、YOLOv8n 检测、CUDA EP（单帧 ~7.5×） | ✅ |

逐周一行一周明细见 [docs/Roadmap.md「已完成里程碑」](./docs/Roadmap.md)；阶段手册见 [docs/archive/Q1.md](./docs/archive/Q1.md)、[docs/archive/Q2.md](./docs/archive/Q2.md)。

### 主线（W17 起 · 求职最短路径 · Phase + 交付物里程碑，不再按周切分）

| 阶段 | 交付物 | 核心主题 | 关键技术 | 状态 |
|------|--------|----------|----------|------|
| **Phase 0** | `quant` / `trt` | CV 收口（变现 + LLM 地基） | INT8 量化、Profiling、**部署硬化**、TensorRT C++（FP16 + INT8 Calibrator + GPU 端到端） | 🔄 下一站 `quant` |
| **Phase 1** | `llm` | LLM 端侧基础（主攻起步） | llama.cpp、KV Cache、GGUF、Transformer 推理 | ⬜ |
| **Phase 2** | `vllm` / `deploy` | LLM 服务化 + 工程交付 | **vLLM 部署**、OpenAI 兼容 API、PagedAttention、SSE 流式、Docker 容器化 | ⬜ |
| Phase 3 | 弹性 | 弹性深化池（按面试反馈补） | 昇腾 CANN/MindIE、TensorRT-LLM、SGLang/TGI、ARM/RKNN、CUDA 算子 | ⬜ |

完整执行手册（硬条件反推、投递里程碑阶梯、砍掉清单）见 [docs/Roadmap.md](./docs/Roadmap.md)。

---

## 开发环境

| 环境 | 配置详情 | 用途 |
|------|----------|------|
| **VPS** | x86 64 Bit, 2GB RAM, **GCC 15.2.0** | 远程开发、CI/CD |
| **本地笔记本** | i5-12500H, 16GB RAM, RTX 3060, 512GB SSD | 本地开发、GPU 推理测试 |

---

## 动态时间分配框架

> 适用于全年 Q2-Q4 执行阶段，所有季度文档均内置高价值任务地图。

**标签体系**：🔴 **核心**（必须掌握）· 🟡 **加分**（拓展深度）· 🟢 **弹性**（视时间决定深度）

| 场景 | 触发条件 | 行动规则 |
|------|---------|---------|
| 当周拖延 | 进度 < 80% | 从下一个 🟢 周借时间，不往前压 🔴 |
| 关键节点未达标 | W16 未出 Demo / W18 未出报告 | 砍最近的 🟡 周，保 🔴 |
| ARM 硬件不可用 | 无树莓派/Android | W31-W32 降为 5h 调研，节省时间给 W27-W28 |

**核心原则**：🔴 的时间只能加，不能减；🟢 的时间是全局调度的"水库"。

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
│   ├── w6_mmap_loader/          # mmap + std::span + std::expected
│   └── w7_cmake_engineering/    # CMake lib/app/tests 三层结构 + C++20 模块
├── 02_Inference_Analysis/       # Q2 模型分析与性能报表
├── 03_Hardware_Acceleration/    # Q3 核心加速框架源码
├── 04_System_Integration/       # Q4 完整系统集成方案
└── docs/                        # 详细执行手册与基准测试
```

---

## 快速开始

### 前提条件

**一次性安装所有依赖（Ubuntu 24.04 / WSL2）：**

```bash
# 1. 添加 GCC 15 PPA（Ubuntu 24.04 官方源尚未收录）
sudo add-apt-repository -y ppa:ubuntu-toolchain-r/test
sudo apt-get update -q

# 2. 安装编译器、覆盖率工具、构建工具
sudo apt-get install -y g++-15 gcc-15 lcov ninja-build cmake

# 3. 安装 clang-format-21（CI 使用版本，本地必须严格对齐，不可用 apt 默认的 v18/v19）
curl -fsSL https://apt.llvm.org/llvm.sh | sudo bash -s -- 21
sudo apt-get install -y clang-format-21

# 4. 验证
g++-15 --version            # 期望：15.2.0+
gcov-15 --version           # 期望：15.2.0+（lcov 覆盖率需要与编译器版本一致）
clang-format-21 --version   # 期望：21.x（与 CI 严格一致，v18/v19/v20 输出会有差异）
ninja --version             # 期望：1.10+
cmake --version             # 期望：3.28+（3.30+ 可启用 import std; 模块演示）

# 兜底：若本地装不上 g++-15，可仅安装 g++-14。根 CMakeLists 会自动回退，
# 但 C++23 的 std::expected / import std / std::mdspan 等特性受限，CI/生产仍以 g++-15 为准。
# 注意：clang-format-21 没有同等兜底，本地必须装 v21，否则格式检查易与 CI 不一致。

# 5. 克隆仓库
git clone https://github.com/hanchunbo/Edge-AI-Genesis-2026.git
cd Edge-AI-Genesis-2026
```

> GTest 源码已随仓库内置于 `third_party/v1.15.2.zip`，FetchContent 直接读取本地 zip，
> configure / build / ctest **全程无需网络**，内网环境开箱即用。

#### W14+ 额外依赖：ONNX Runtime（CPU 包，Q2 起需要）

包体 ~8MB（CPU），不入库。一次性下载到 `third_party/onnxruntime/`：

```bash
mkdir -p third_party/onnxruntime
cd third_party/onnxruntime
curl -L -O https://github.com/microsoft/onnxruntime/releases/download/v1.26.0/onnxruntime-linux-x64-1.26.0.tgz
tar xzf onnxruntime-linux-x64-1.26.0.tgz
rm onnxruntime-linux-x64-1.26.0.tgz
```

CMake 自动从 `third_party/onnxruntime/onnxruntime-linux-x64-1.26.0/` 接入，
可用 `-DONNXRUNTIME_ROOT=<path>` 指向其他路径（如系统安装、其他版本）。

CPU 包足以满足 W14/W15 全部成功标准（CUDA EP 实测见下）。

#### W16 额外依赖：Python venv + ultralytics（仅用于导出 onnx + 对拍基准）

W16 的 C++ 推理不依赖 Python；但导出 `yolov8n.onnx` 和生成 ultralytics 对拍基准需要：

```bash
sudo apt install -y python3.12-venv          # WSL/Ubuntu 默认未装 ensurepip
python3 -m venv .venv && . .venv/bin/activate
pip install torch torchvision --index-url https://download.pytorch.org/whl/cpu  # CPU 版即可（导出/单图推理用）
pip install ultralytics onnx
# 导出模型 + 标签 + 测试图 + 对拍基准：
cd 02_Inference_Analysis/w16_yolo_detector
python tools/export_yolov8n.py && python tools/gen_reference.py
```

> 坑：`pip install ultralytics` 会拉 torchvision，若与先装的 torch 版本不匹配会报
> `operator torchvision::nms does not exist`——torch 和 torchvision 必须同源（同从 cpu index 装）。
> `.venv/` 与 `models/*.onnx`、`models/*.jpg` 不入库（见 `.gitignore`）。

#### quant 额外依赖：ORT static INT8 PTQ（第二阶段量化）

`quant_yolov8_static` CMake target 会调用 ONNX Runtime Python 量化 API，对 W16
导出的 `yolov8n.onnx` 生成 MinMax / Entropy 两个 QDQ INT8 模型。建议复用上面的
`.venv`：

```bash
. .venv/bin/activate
pip install onnxruntime onnx opencv-python numpy
cmake -S . -B build -G Ninja -DPython3_EXECUTABLE="$PWD/.venv/bin/python"
cmake --build build --target quant_yolov8_static
```

默认校准样本使用 `02_Inference_Analysis/w16_yolo_detector/models/test_image.jpg`，
因此需先完成 W16 的模型/图片导出。如果 build 目录是在创建 `.venv` 前配置的，必须按上面的
`-DPython3_EXECUTABLE=...` 重新配置一次，否则 CMake 可能继续使用旧 Python。手动查看参数：

```bash
python3 02_Inference_Analysis/quantization/tools/quantize_yolov8_static.py --help
```

#### W14.5 可选：CUDA EP（GPU 推理，WSL2 + RTX 30 系实测）

在本地 GPU 机上启用 ONNX Runtime CUDA ExecutionProvider。**pin 版本：CUDA 12.3
+ cuDNN 9 + ORT GPU 1.26.0**（CUDA 12.3 对齐 host 驱动 546.30 的能力上限；
cuDNN 8/9 ABI 不可混用）。纯 CPU 环境（VPS/CI）不装这些也能编译运行，CUDA 用例
会优雅跳过。

**1. 加 CUDA apt 源（WSL：driver 归 Windows host，Linux 侧绝不装 driver）**

```bash
cd /tmp && wget -q https://developer.download.nvidia.com/compute/cuda/repos/wsl-ubuntu/x86_64/cuda-keyring_1.1-1_all.deb
sudo dpkg -i cuda-keyring_1.1-1_all.deb
# cuDNN 9 不在 wsl-ubuntu 源里，需额外加 ubuntu2404 源（复用同一 keyring）
echo "deb [signed-by=/usr/share/keyrings/cuda-archive-keyring.gpg] https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2404/x86_64/ /" | sudo tee /etc/apt/sources.list.d/cuda-ubuntu2404.list
sudo apt-get update
```

**2. 装精简工具链 + cuDNN 9（绕开 nsight → libtinfo5 坑）**

```bash
# 不要装 cuda-toolkit-12-3：它依赖旧版 nsight-systems → libtinfo5，
# 而 Ubuntu 24.04 已废弃 libtinfo5，会报 "held broken packages"。
# 精简 meta 含 nvcc + 全部 runtime/dev 库，profiler 留到 W16 用新版单装。
sudo apt-get install -y cuda-compiler-12-3 cuda-libraries-12-3 \
                        cuda-libraries-dev-12-3 cudnn9-cuda-12
```

**3. 下 ORT GPU 包（与 CPU 包并排）**

```bash
cd third_party/onnxruntime
wget https://github.com/microsoft/onnxruntime/releases/download/v1.26.0/onnxruntime-linux-x64-gpu-1.26.0.tgz
tar xzf onnxruntime-linux-x64-gpu-1.26.0.tgz && rm onnxruntime-linux-x64-gpu-1.26.0.tgz
```

**4. 用 GPU 包配置并验证**

```bash
cmake -B build-gpu -S . -DCMAKE_CXX_COMPILER=g++-15 -G Ninja \
  -DONNXRUNTIME_ROOT="$(pwd)/third_party/onnxruntime/onnxruntime-linux-x64-gpu-1.26.0"
cmake --build build-gpu --target w14_inference_engine_test -j$(nproc)
ctest --test-dir build-gpu -R "W14_" --output-on-failure   # LoadsModelWithCudaEp 不再跳过
./build-gpu/02_Inference_Analysis/w14_ort_basics/w14_ort_basics_demo --cuda  # 应打印「实际 EP: CUDA」
```

**排障三件套**

```bash
nvidia-smi                                  # 确认 GPU + 驱动可见
/usr/local/cuda/bin/nvcc --version          # 确认 CUDA 12.3
ldconfig -p | grep -E 'cudnn|cudart|cublas' # 确认运行时库在链接器路径（NVIDIA 包自动加 ld.so.conf.d）
```

> 注：ORT 的 `libonnxruntime_providers_cuda.so` 由运行时 dlopen，与 `libonnxruntime.so`
> 同目录，现有 `BUILD_RPATH` 已覆盖；CUDA/cuDNN runtime 库经 ldconfig 可达，无需手设
> `LD_LIBRARY_PATH`。若 CUDA 不可用，引擎会优雅回退 CPU（`InferenceEngine::ActiveEp()`
> 查实际 EP，`EpFallbackReason()` 查回退原因）。

### 场景一：标准构建（含单元测试）

```bash
cmake -B build -S . -DCMAKE_CXX_COMPILER=g++-15 -G Ninja
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

### 场景二：W8 覆盖率报告

```bash
# 开启 gcov 插桩重新配置
cmake -B build -S . -DCMAKE_CXX_COMPILER=g++-15 -G Ninja -DW8_COVERAGE=ON

# 编译
cmake --build build -j$(nproc)

# 生成报告（自动运行 W1-W7 全部测试 → 采集 → 过滤 → HTML）
cmake --build build --target w8_coverage

# 在本机浏览器访问报告（VPS 无桌面，用 HTTP server）
python3 -m http.server 8080 --directory build/w8_coverage_report
# → 浏览器访问 http://VPS_IP:8080
```

> 覆盖率基线（W1-W7 合计，g++-15 编译）：行覆盖率 **98.6%**，函数覆盖率 **100%**

### 场景三：跳过测试，只编译功能程序

```bash
cmake -B build -S . -DCMAKE_CXX_COMPILER=g++-15 -G Ninja -DBUILD_TESTING=OFF
cmake --build build -j$(nproc)
# 此时无需 GTest，所有演示程序和 benchmark 均可正常编译运行
```

### 调试专用（ASAN / TSAN）

```bash
# 内存越界 / use-after-free 检测
cmake -B build -S . -DCMAKE_CXX_COMPILER=g++-15 -G Ninja -DENABLE_ASAN=ON
cmake --build build --target <目标名> -j$(nproc)
ctest --test-dir build -R "<周次正则>" --output-on-failure

# 线程竞争检测
cmake -B build -S . -DCMAKE_CXX_COMPILER=g++-15 -G Ninja -DENABLE_TSAN=ON
cmake --build build --target <目标名> -j$(nproc)
ctest --test-dir build -R "<周次正则>" --output-on-failure
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

### 知识沉淀
- **FAQ 库**：建立高性能 C++ 与 AI 算子的常见问题与参考答案库

---

## 许可证

本项目采用 [MIT License](LICENSE) 开源协议。
