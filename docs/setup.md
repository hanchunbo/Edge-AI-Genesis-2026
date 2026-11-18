# 环境搭建与构建指南

> 适用于 Ubuntu 24.04 / WSL2。[README](../README.md#快速开始) 只保留最小构建步骤；
> 本文是完整的依赖安装、推理后端（ORT / CUDA / TensorRT）配置与构建场景说明。

## 基础工具链

**一次性安装所有依赖：**

**一次性安装所有依赖（Ubuntu 24.04 / WSL2）：**

```bash
# 1. 添加 GCC 15 PPA（Ubuntu 24.04 官方源尚未收录）
sudo add-apt-repository -y ppa:ubuntu-toolchain-r/test
sudo apt-get update -q

# 2. 安装编译器、覆盖率工具、构建工具、OpenCV（W9 起需要）
sudo apt-get install -y g++-15 gcc-15 lcov ninja-build cmake libopencv-dev

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

## 推理依赖

### ONNX Runtime（CPU 包，W14 起需要）

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

### W16 额外依赖：Python venv + ultralytics（仅用于导出 onnx + 对拍基准）

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

### quant 额外依赖：ORT static INT8 PTQ（第二阶段量化）

`quant_yolov8_static` CMake target 会调用 ONNX Runtime Python 量化 API，对 W16
导出的 `yolov8n.onnx` 生成 MinMax / Entropy 两个 QDQ INT8 模型。建议复用上面的
`.venv`：

```bash
. .venv/bin/activate
pip install onnxruntime onnx opencv-python numpy
cmake -S . -B build -G Ninja -DPython3_EXECUTABLE="$PWD/.venv/bin/python"
systemd-run --user --scope -p MemoryMax=5G -p MemorySwapMax=4G \
  cmake --build build --target quant_yolov8_static
```

默认优先使用 `$HOME/code/datasets/coco128/images/train2017`，最多校准 **32 张图**；目录
不存在时回退 W16 的 `models/test_image.jpg`。单图模式只验证工具链，不能用于复述历史
coco128 精度结论。需先完成 W16 的模型/图片导出。`QUANT_CALIB_DIR`、
`QUANT_CALIB_LIMIT`、`QUANT_MODELS_DIR` 可通过 CMake `-D` 覆盖；限制为 0 表示显式不限量，
负数会被拒绝。32 张图不是内存上限：上面的 systemd 内存约束针对本机 WSL，其他环境须按
可用内存限制进程，不在 CMake 中强制依赖 systemd。被终止时先降低校准图数。

如果 build 目录是在创建 `.venv` 前配置的，必须按上面的 `-DPython3_EXECUTABLE=...`
重新配置，否则 CMake 可能继续使用旧 Python。手动运行时也明确校准数量：

```bash
python3 02_Inference_Analysis/quantization/tools/quantize_yolov8_static.py --help
systemd-run --user --scope -p MemoryMax=5G -p MemorySwapMax=4G \
  .venv/bin/python 02_Inference_Analysis/quantization/tools/quantize_yolov8_static.py \
  --model 02_Inference_Analysis/w16_yolo_detector/models/yolov8n.onnx \
  --calib-dir "$HOME/code/datasets/coco128/images/train2017" --calib-limit 32
```

CPU/GPU 构建可读取同一份已验证的 ONNX，无需为了创建第二个 build 目录重复校准：

```bash
cmake -S . -B build-gpu -G Ninja -DCMAKE_CXX_COMPILER=g++-15 \
  -DONNXRUNTIME_ROOT="$PWD/third_party/onnxruntime/onnxruntime-linux-x64-gpu-1.26.0" \
  -DQUANT_MODELS_DIR="$PWD/build/02_Inference_Analysis/quantization/models"
cmake --build build-gpu --target quant_int8_consistency_test -j2
ctest --test-dir build-gpu -R '^Quant_Int8ConsistencyTest$' --output-on-failure
```

该一致性测试仍使用 CPU EP。缺模型时本地可跳过；正式验收必须确认两个用例实际通过，
不能把 skip 算作精度验证。现有 MinMax/Entropy 两产物字节相同，原因见
[量化报告勘误](benchmarks/quant_int8_report.md)。

### quant 可选：NVTX 标记与 Nsight 采集

`QUANT_ENABLE_NVTX` 默认关闭，普通构建不需要 NVTX/Nsight。标记版使用
[NVIDIA NVTX v3.2.2](https://github.com/NVIDIA/NVTX/tree/v3.2.2) 头文件：

```bash
mkdir -p build/quant-tools
curl --fail --location https://codeload.github.com/NVIDIA/NVTX/tar.gz/refs/tags/v3.2.2 \
  -o build/quant-tools/nvtx-v3.2.2.tar.gz
echo '76b875507d2501c9431d79e272ae5b6d4131bd0ffa7a7d3b79ac261d35609f7c  build/quant-tools/nvtx-v3.2.2.tar.gz' | sha256sum -c -
tar -xzf build/quant-tools/nvtx-v3.2.2.tar.gz -C build/quant-tools
```

2026-09-10 补装 Nsight Systems **2025.1.3.140**：从 NVIDIA 官方仓库下载固定版本，
校验 SHA-256 后解压至用户目录，直接运行 CLI。此方式不安装 `.deb` 的 GUI 依赖，
不改变 CUDA、cuDNN 或 Windows 显卡驱动；也不会获得 root 权限。

```bash
curl --fail --location \
  https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2404/x86_64/nsight-systems-2025.1.3_2025.1.3.140-1_amd64.deb \
  -o build/quant-tools/nsight-systems-2025.1.3.deb
echo '77290633afb63a119d53a9f11e2c64cfcc2c3b79a2fb5b4b4e673e1de1dd6836  build/quant-tools/nsight-systems-2025.1.3.deb' | sha256sum -c -
mkdir -p "$HOME/.local/opt/nsight-systems-2025.1.3" "$HOME/.local/bin"
dpkg-deb -x build/quant-tools/nsight-systems-2025.1.3.deb "$HOME/.local/opt/nsight-systems-2025.1.3"
ln -s "$HOME/.local/opt/nsight-systems-2025.1.3/opt/nvidia/nsight-systems/2025.1.3/target-linux-x64/nsys" "$HOME/.local/bin/nsys"
"$HOME/.local/bin/nsys" --version
"$HOME/.local/bin/nsys" status --environment
```

本机 WSL 还需要 CUPTI 转换 GPU 时间戳：只升级 Nsight 后仍无设备事件，按
[NVIDIA 的 WSL 处理说明](https://forums.developer.nvidia.com/t/nsys-doesnt-show-cuda-kernel-and-memory-data/315536/9)
在 `nsys -z` 输出的配置文件中设置 `CuptiUseRawGpuTimestamps=false` 后，短采集才出现
kernel / memcpy 表。若配置已存在，保留其他内容，只修改该项；本机原先没有此文件：

```bash
mkdir -p "$HOME/.config/NVIDIA Corporation"
echo 'CuptiUseRawGpuTimestamps=false' >> "$HOME/.config/NVIDIA Corporation/nsys-config.ini"
```

该设置使用 CUPTI 时间戳转换，精度边界见 NVIDIA 说明；仅 `nsys` 退出码为 0 不足以证明
GPU trace 完整，还要检查导出的 SQLite 中有非空 `CUPTI_ACTIVITY_KIND_KERNEL` 与
`CUPTI_ACTIVITY_KIND_MEMCPY`。本机首次 2024.6.2 采集只有 host API，已保留为失败验收记录。

本机进程树采样环境检查通过；系统级采样检查未通过，本轮不使用系统级采样，
也不调整内核权限。分析先使用 CUDA/NVTX/OSRT trace，`perf` 与 Nsight Compute 按需另装。

标记版构建与采集命令见 [quant 模块笔记](../02_Inference_Analysis/quantization/notes.md#profiling-入口与采集边界)。
2026-09-10 切换执行权限后 GPU 可用，已完成五轮基准、Nsight 设备 trace 与 ORT 分配分析。
结论与 WSL 采集处理见 [性能瓶颈报告](benchmarks/quant_profiling_report.md)；
退出 UAF 的后续修复见 [生命周期报告](benchmarks/quant_runtime_lifetime.md)。历史 GPU 数字保留原日期。

### W14.5 可选：CUDA EP（GPU 推理，WSL2 + RTX 30 系实测）

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

### TensorRT 10.16（trt 交付物，本地 GPU 机）

复用上方 CUDA apt 源。**必须锁版本**：不带 `=版本` 会装到 TRT 11 并连带拉 CUDA 13
（超出 driver 546.30 的 CUDA 12.3 能力上限）。且 apt 不会自动为依赖挑 cuda12.9 构建
（依赖包的 candidate 也是 cuda13.x），因此全部依赖包都要显式锁版本一次装齐：

```bash
V=10.16.1.11-1+cuda12.9
sudo apt-get install -y tensorrt-dev=$V libnvinfer-bin=$V \
  libnvinfer10=$V libnvinfer-lean10=$V libnvinfer-plugin10=$V \
  libnvinfer-vc-plugin10=$V libnvinfer-dispatch10=$V libnvonnxparsers10=$V \
  libnvinfer-dev=$V libnvinfer-lean-dev=$V libnvinfer-dispatch-dev=$V \
  libnvinfer-plugin-dev=$V libnvinfer-vc-plugin-dev=$V libnvonnxparsers-dev=$V \
  libnvinfer-headers-dev=$V libnvinfer-safe-headers-dev=$V libnvinfer-headers-plugin-dev=$V \
  libnvinfer-headers-python-plugin-dev=$V libnvinfer-win-builder-resource10=$V
dpkg-query -W -f '${Package}\n' | grep -E '^(libnvinfer|libnvonnxparsers|tensorrt)' | xargs sudo apt-mark hold
# 冒烟（yolov8n.onnx 是动态维度导出，必须给 --shapes，否则 trtexec 兜底 1x3x1x1 直接 FAILED）：
# /usr/src/tensorrt/bin/trtexec --onnx=<模型> --shapes=images:1x3x640x640 --fp16  期望 PASSED
```

TRT 10.16 的 cuda12.9 构建按 CUDA minor version compatibility 运行在 12.3 driver 上；
实测未连带任何 CUDA 包（19 个包全是 libnvinfer*/libnvonnxparsers*/tensorrt-dev），
`/usr/local/cuda` 仍指向 12.3。

`.cu` 编译（M3 预处理 kernel）：nvcc 12.3 官方只支持 g++ ≤ 12.2，需
`sudo apt-get install -y g++-12` 专供 nvcc（`-ccbin g++-12` / CMake
`CMAKE_CUDA_HOST_COMPILER=g++-12`）；主工程仍用 g++-15，两者只过 C ABI 边界。
nvcc 不在 PATH，全路径 `/usr/local/cuda/bin/nvcc`。

## 构建场景

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

# 无桌面环境可用 HTTP server 在浏览器查看
python3 -m http.server 8080 --directory build/w8_coverage_report
# → 浏览器访问 http://<主机 IP>:8080
```

> 覆盖率基线（W1-W7 合计，g++-15 编译）：行覆盖率 **98.6%**，函数覆盖率 **100%**

### 场景三：跳过测试，只编译功能程序

```bash
cmake -B build -S . -DCMAKE_CXX_COMPILER=g++-15 -G Ninja -DBUILD_TESTING=OFF
cmake --build build -j$(nproc)
# 此时无需 GTest，所有演示程序和 benchmark 均可正常编译运行
```

### 调试专用（ASAN / TSAN）

本机 CUDA + ASAN 初次运行报 CUDA error 2；设置
`ASAN_OPTIONS=protect_shadow_gap=0` 后可初始化 CUDA，参见
[NVIDIA 说明](https://developer.nvidia.com/docs/drive/drive-os/7.0.3/public/drive-os-tensorrt-developer-guide/troubleshooting.html#issues-with-cuda-and-address-sanitizer)。
这只处理工具兼容性，不是退出 UAF 的修复。预编译 ORT/CUDA 库未被 ASAN 插桩，
其内部访问错误仍可能漏报；本次以 Valgrind 修复前后对照作为主要证据。

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

## 排障与文档工具

### 可选：退出内存错误诊断工具

2026-09-11 为 quant 退出问题安装 GDB 15.1、Valgrind 3.22 与匹配当前 glibc 的
调试符号。本次未升级 glibc、ORT 或 CUDA。一般环境先检查版本和安装预览：

```bash
dpkg-query -W libc6
apt-cache policy libc6-dbg
sudo apt-get install --simulate --no-install-recommends gdb valgrind libc6-dbg
```

本机 glibc 为 `2.39-0ubuntu8.8`，源中候选调试包已变为 `8.9`，为保持排查环境
一致，实际使用对应版本包；下面 URL 仅适用于该 glibc 版本：

```bash
mkdir -p build/quant-tools
curl -fL https://archive.ubuntu.com/ubuntu/pool/main/g/glibc/libc6-dbg_2.39-0ubuntu8.8_amd64.deb \
  -o build/quant-tools/libc6-dbg_2.39-0ubuntu8.8_amd64.deb
sudo apt-get install -y --no-install-recommends gdb
sudo apt-get install -y --no-install-recommends \
  "$PWD/build/quant-tools/libc6-dbg_2.39-0ubuntu8.8_amd64.deb" valgrind
gdb --version
valgrind --version
```

用途与原始栈读法见 [退出修复报告](benchmarks/quant_runtime_lifetime.md)。

### 文档图渲染（Mermaid CLI）

文档图验证使用 Mermaid CLI 11.16.0（`npx -p @mermaid-js/mermaid-cli@11.16.0 mmdc`）。
2026-09-11 复用已缓存的 CLI，补装 Puppeteer 对应的 Chrome Headless Shell
150.0.7871.24；无浏览器的环境可在该 CLI 的 npm 环境中运行
`puppeteer browsers install chrome-headless-shell`，再通过 `mmdc -p puppeteer.json`
指定下载后的 `executablePath`。这仅用于渲染文档图，不是推理构建依赖。
