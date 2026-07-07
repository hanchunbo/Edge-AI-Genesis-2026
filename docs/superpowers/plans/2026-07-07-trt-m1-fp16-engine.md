# trt M1 — FP16 engine 跑通 + 四路表骨架 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 WSL2 装好 TensorRT 10.16 并用 C++ Builder API 把 yolov8n.onnx 建成 FP16 engine（落盘缓存），检测结果与 W16 参考对拍通过，实测 TRT FP16 纯 infer 延迟并落出四路对比表骨架。

**Architecture:** 新模块 `02_Inference_Analysis/tensorrt/`（命名空间 `trt`），前后处理复用 w10 letterbox + w16 decode/NMS（均无 ORT 依赖），统计口径链 `quant_core` 的 `RollingStats`。TRT 对象全用 `std::unique_ptr` RAII（TRT 10 支持直接 delete）；错误传播对齐 w16/quant——抛 `std::runtime_error`，**不用** `std::expected`（实施前已核对）。M1 不编 `.cu`（GPU buffer 走 cudart 主机 API），CMake 到 M3 才 `enable_language(CUDA)`。

**Tech Stack:** TensorRT 10.16（deb 锁版本）、CUDA 12.3（nvcc 在 `/usr/local/cuda/bin`，不在 PATH）、g++-15 主工程 / g++-12 专供 nvcc、OpenCV、GTest。

**上游 spec:** `docs/superpowers/specs/2026-07-06-trt-design.md`（M1 节 + §6 错误处理 + §7 测试 + 风险 1/2）

---

## 环境事实（计划时已实测，直接依赖）

- GPU：RTX 3060 Laptop，SM 8.6（Ampere），driver 546.30（CUDA 12.3 上限）
- CUDA 12.3 完整 toolchain 已装（含 nvcc），位于 `/usr/local/cuda`，**不在 PATH**
- TensorRT **未装**；apt 源（`/etc/apt/sources.list.d/cuda-ubuntu2404.list`）已配好，CUDA 12.x 可用的最高 TRT 10.x 版本 = **`10.16.1.11-1+cuda12.9`**。⚠️ `apt-get install tensorrt-dev` 不锁版本会拉 `11.1.0+cuda13.3` 并连带 CUDA 13，必须显式 `=版本`
- g++-12 未装（nvcc 12.3 官方只支持 g++ ≤ 12.2，见 spec 风险 1）
- 构建目录用 **`build-gpu`**（已配置 GPU 版 ORT root）；ninja 检测到 CMakeLists 变化会自动重新 configure
- 复用资产（全部已确认存在）：
  - `02_Inference_Analysis/w16_yolo_detector/models/{yolov8n.onnx, test_image.jpg, reference_detections.txt}`
  - `w16::DecodeYolov8` / `w16::Nms` / `w16::IoU` → 目标 `w16_detect_core`（不依赖 ORT/OpenCV）
  - `w10::LetterboxToTensor` / `w10::LetterboxInfo` → 目标 `w10_resize_lib`
  - `quant::RollingStats`（P50/P99 口径）→ 目标 `quant_core`
- 引用的 quant 同机数字（`docs/benchmarks/quant_int8_report.md`，不重测）：CPU EP FP32 纯 infer P50 **40.81ms**、CPU EP INT8(MinMax) **31.31ms**、CUDA EP FP32 **5.64ms**、CUDA EP INT8(QDQ MinMax) **11.49ms**；mAP50-95：FP32 **0.4454** / INT8 **0.4285**
- mmdc 未全局安装，Mermaid 渲染验证用 `npx -y @mermaid-js/mermaid-cli`

## 每次 commit 前固定动作（所有 Task 通用，不再重复写进步骤）

```bash
cd /home/dev/code/Edge-AI-Genesis-2026
find . -maxdepth 3 -regex '.*0[1-4]_.*' \( -name "*.cpp" -o -name "*.hpp" \) | xargs clang-format-21 --dry-run --Werror
```

期望：无输出（全部通过）。有报错就先 `clang-format-21 -i <文件>` 修复再提交。
commit author 固定 `hanchunbo <hanchunbo@users.noreply.github.com>`，消息末尾加 `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`。首个 commit 前先 `git fetch origin` 确认远端 dev 无新提交。

---

### Task 1: 环境 — TensorRT 10.16 锁版本安装 + trtexec 冒烟

**Files:**
- Modify: `README.md`（「前提条件」GPU/CUDA 小节末尾追加 TRT 小节）

- [ ] **Step 1: 确认 WSL 内存上限已生效（防 OOM 崩 WSL，历史踩过坑）**

```bash
free -g | head -2
```

期望：`Mem: total` ≥ 11（`.wslconfig` 的 `memory=12GB` 已生效）。若仍是 7~8，先让用户在 Windows 侧 `wsl --shutdown` 后重开再继续——TRT builder 峰值内存数 GB，8GB 上限有崩 WSL 前科。

- [ ] **Step 2: dry-run 检查依赖拉取范围**

```bash
sudo apt-get update -q
apt-get install --dry-run tensorrt-dev=10.16.1.11-1+cuda12.9 libnvinfer-bin=10.16.1.11-1+cuda12.9 2>&1 | grep -E "^Inst" | sort
```

期望：全部是 `libnvinfer*` / `libnvonnxparsers*` / `tensorrt*` 10.16 包，外加可能的 `cuda-cudart-12-9`（side-by-side 装到 `/usr/local/cuda-12.9`，无害）。
**中止条件**：出现任何 `*-13-*`（CUDA 13）、`nvidia-driver*`、`cuda-toolkit-12-9`（整套 toolkit）→ 停下报告，不要硬装。

- [ ] **Step 3: 锁版本安装 + hold**

```bash
sudo apt-get install -y tensorrt-dev=10.16.1.11-1+cuda12.9 libnvinfer-bin=10.16.1.11-1+cuda12.9
dpkg-query -W -f '${Package}\n' | grep -E '^(libnvinfer|libnvonnxparsers|tensorrt)' | xargs sudo apt-mark hold
```

期望：安装成功；`apt-mark hold` 列出全部 TRT 包（防止后续 `apt upgrade` 升到 TRT 11 + CUDA 13）。

- [ ] **Step 4: 环境验证**

```bash
ls /usr/local/cuda && readlink /usr/local/cuda        # 仍指向 cuda-12.3
dpkg -l | grep libnvinfer10                            # 10.16.1.11-1+cuda12.9
ldconfig -p | grep -E "libnvinfer\.so|libnvonnxparsers\.so"
```

- [ ] **Step 5: trtexec 冒烟——先于任何 C++ 代码验证整条 TRT 链路**

```bash
/usr/src/tensorrt/bin/trtexec \
  --onnx=/home/dev/code/Edge-AI-Genesis-2026/02_Inference_Analysis/w16_yolo_detector/models/yolov8n.onnx \
  --fp16 --memPoolSize=workspace:1024 2>&1 | tail -20
```

期望：末尾出现 `PASSED`，并打印 GPU Compute Time（记下均值，后面与我们自己的 benchmark 互验数量级）。构建耗时 1~4 分钟属正常。
（若 trtexec 不在 `/usr/src/tensorrt/bin/`，用 `dpkg -L libnvinfer-bin | grep trtexec` 找。）

- [ ] **Step 6: README 同步（环境依赖同步铁律）**

在 README「前提条件」的 GPU/CUDA 小节末尾（ORT GPU 排障内容之后、下一个标题之前）插入：

```markdown
#### TensorRT 10.16（trt 交付物，本地 GPU 机）

复用上方 CUDA apt 源。**必须锁版本**：不带 `=版本` 会装到 TRT 11 并连带拉 CUDA 13
（超出 driver 546.30 的 CUDA 12.3 能力上限）。

```bash
sudo apt-get install -y tensorrt-dev=10.16.1.11-1+cuda12.9 libnvinfer-bin=10.16.1.11-1+cuda12.9
dpkg-query -W -f '${Package}\n' | grep -E '^(libnvinfer|libnvonnxparsers|tensorrt)' | xargs sudo apt-mark hold
# 冒烟：/usr/src/tensorrt/bin/trtexec --onnx=<模型> --fp16 期望 PASSED
```

TRT 10.16 的 cuda12.9 构建按 CUDA minor version compatibility 运行在 12.3 driver 上；
若安装连带了 `cuda-cudart-12-9`，属 side-by-side 共存，`/usr/local/cuda` 仍指向 12.3。
```

- [ ] **Step 7: Commit**

```bash
git fetch origin
git add README.md
git commit -m "docs(trt): TensorRT 10.16 锁版本安装记入前提条件

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 2: 环境 — g++-12 落地 nvcc 宿主编译器方案（退掉 spec 风险 1）

M1 不编 `.cu`，此任务只为把 M3 的最大环境风险提前退掉：装 g++-12 并验证 nvcc 能出可运行的 GPU 二进制。

**Files:**
- Modify: `README.md`（TRT 小节内追加两行）

- [ ] **Step 1: 安装 g++-12**

```bash
sudo apt-get install -y g++-12
g++-12 --version | head -1
```

- [ ] **Step 2: nvcc + g++-12 冒烟（在 scratchpad，不进仓库）**

```bash
SCRATCH=/tmp/claude-1000/-home-dev-code-Edge-AI-Genesis-2026/74206607-73bb-4eb7-8400-a43fecfe61e9/scratchpad
cat > "$SCRATCH/hello.cu" <<'EOF'
#include <cstdio>
__global__ void Kernel(int* out) { *out = 42; }
int main() {
  int* d = nullptr;
  cudaMalloc(&d, sizeof(int));
  Kernel<<<1, 1>>>(d);
  int h = 0;
  cudaMemcpy(&h, d, sizeof(int), cudaMemcpyDeviceToHost);
  cudaFree(d);
  std::printf("%d\n", h);
  return h == 42 ? 0 : 1;
}
EOF
/usr/local/cuda/bin/nvcc -ccbin g++-12 -arch=sm_86 -o "$SCRATCH/hello_cu" "$SCRATCH/hello.cu" && "$SCRATCH/hello_cu"
```

期望：编译无错，输出 `42`。这证明 M3 的 `.cu` 路线（nvcc + g++-12，与主工程 g++-15 只过 C ABI 边界）可行。

- [ ] **Step 3: README 补记 + Commit**

在 Task 1 新增的 TRT 小节末尾追加：

```markdown
`.cu` 编译（M3 预处理 kernel）：nvcc 12.3 官方只支持 g++ ≤ 12.2，需
`sudo apt-get install -y g++-12` 专供 nvcc（`-ccbin g++-12` / CMake
`CMAKE_CUDA_HOST_COMPILER=g++-12`）；主工程仍用 g++-15，两者只过 C ABI 边界。
nvcc 不在 PATH，全路径 `/usr/local/cuda/bin/nvcc`。
```

```bash
git add README.md
git commit -m "docs(trt): g++-12 专供 nvcc 的宿主编译器方案落地（退 M3 风险）

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 3: 模块骨架 — CMake 守卫接入 + 进度标记

**Files:**
- Create: `02_Inference_Analysis/tensorrt/CMakeLists.txt`
- Modify: `CMakeLists.txt`（根，第 139 行 `add_subdirectory(02_Inference_Analysis/quantization)` 之后）
- Modify: `CLAUDE.md`（「当前进度」段）

- [ ] **Step 1: 写模块 CMakeLists（仅探测 + 守卫，目标随后续 Task 增量加入）**

`02_Inference_Analysis/tensorrt/CMakeLists.txt`：

```cmake
# Copyright 2026 Edge-AI-Genesis
#
# =============================================================================
# trt：TensorRT C++ Engine（FP16/INT8 + GPU 端到端）
# =============================================================================
# 依赖系统 deb 安装的 TensorRT 10.x + CUDA Toolkit 12.3。
# 无 TRT/CUDA 的环境（VPS/CI）：warning + return 整体跳过，对齐 w14/quant 守卫模式。
# M1/M2 不编 .cu（GPU buffer 走 cudart 主机 API），enable_language(CUDA) 留到 M3。

find_package(CUDAToolkit QUIET)
find_path(TRT_INCLUDE_DIR NvInfer.h)
find_library(TRT_NVINFER_LIB nvinfer)
find_library(TRT_NVONNXPARSER_LIB nvonnxparser)

if(NOT CUDAToolkit_FOUND OR NOT TRT_INCLUDE_DIR
   OR NOT TRT_NVINFER_LIB OR NOT TRT_NVONNXPARSER_LIB)
  message(WARNING "[trt] 未找到 TensorRT/CUDAToolkit，跳过 trt 模块构建。")
  return()
endif()

message(STATUS "[trt] TensorRT: ${TRT_NVINFER_LIB}")
message(STATUS "[trt] CUDAToolkit: ${CUDAToolkit_VERSION}")

find_package(Threads REQUIRED)
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)

# 复用 W16 模型资产与统一 engine 缓存目录（缓存戳编入文件名，见 engine_builder）。
set(_TRT_W16_MODELS
  "${CMAKE_SOURCE_DIR}/02_Inference_Analysis/w16_yolo_detector/models")
set(_TRT_ENGINE_CACHE "${CMAKE_CURRENT_BINARY_DIR}/engine_cache")
```

- [ ] **Step 2: 根 CMakeLists 接入**

在根 `CMakeLists.txt` 的 `add_subdirectory(02_Inference_Analysis/quantization)` 之后加一行：

```cmake
add_subdirectory(02_Inference_Analysis/tensorrt)
```

- [ ] **Step 3: configure 验证**

```bash
cmake --build build-gpu --target help 2>&1 | head -5   # 触发重新 configure
grep -E "\[trt\]" build-gpu/CMakeCache.txt 2>/dev/null; cmake -B build-gpu -S . 2>&1 | grep "\[trt\]"
```

期望：输出 `[trt] TensorRT: /usr/lib/x86_64-linux-gnu/libnvinfer.so` 与 CUDAToolkit 版本，无 warning。

- [ ] **Step 4: CLAUDE.md 进度标记（开始新交付物的铁律）**

把「当前进度」段末尾的 `下一交付物：\`trt\`（TensorRT C++ Engine，未开始）` 改为：

```
当前交付物：`trt`（TensorRT C++ Engine）🟡 M1 进行中——FP16 engine + 四路表骨架，计划见 docs/superpowers/plans/2026-07-07-trt-m1-fp16-engine.md
```

- [ ] **Step 5: Commit**

```bash
git add 02_Inference_Analysis/tensorrt/CMakeLists.txt CMakeLists.txt CLAUDE.md
git commit -m "feat(trt): 模块骨架接入（TRT/CUDA 探测守卫），标记 M1 进行中

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 4: trt_common — ILogger 适配器 + CUDA 检查宏 + DeviceBuffer（TDD）

**Files:**
- Create: `02_Inference_Analysis/tensorrt/trt_common.hpp`
- Create: `02_Inference_Analysis/tensorrt/trt_common.cpp`
- Create: `02_Inference_Analysis/tensorrt/trt_common_test.cpp`
- Modify: `02_Inference_Analysis/tensorrt/CMakeLists.txt`（追加目标）

- [ ] **Step 1: 写失败测试** `trt_common_test.cpp`：

```cpp
// SPDX-License-Identifier: MIT
//
// 文件功能：trt 公共设施测试 —— CUDA 检查宏抛错行为与 DeviceBuffer RAII/move。

#include "trt_common.hpp"

#include <cuda_runtime_api.h>
#include <gtest/gtest.h>
#include <stdexcept>
#include <utility>

namespace {

bool CudaAvailable() {
  int count = 0;
  return cudaGetDeviceCount(&count) == cudaSuccess && count > 0;
}

TEST(TrtCommon, CudaCheckThrowsOnBadCall) {
  if (!CudaAvailable()) {
    GTEST_SKIP() << "无 CUDA 设备（CI/VPS），跳过";
  }
  EXPECT_THROW(TRT_CUDA_CHECK(cudaSetDevice(9999)), std::runtime_error);
  // 清掉上一条错误，避免污染后续用例（invalid device 非 sticky error）。
  cudaGetLastError();
}

TEST(TrtCommon, DeviceBufferAllocatesAndMoves) {
  if (!CudaAvailable()) {
    GTEST_SKIP() << "无 CUDA 设备（CI/VPS），跳过";
  }
  trt::DeviceBuffer a(1024);
  EXPECT_NE(a.Get(), nullptr);
  EXPECT_EQ(a.Bytes(), 1024u);

  trt::DeviceBuffer b(std::move(a));
  EXPECT_NE(b.Get(), nullptr);
  EXPECT_EQ(a.Get(), nullptr);  // 被移走后不再持有，防 double-free

  trt::DeviceBuffer c;
  c = std::move(b);
  EXPECT_NE(c.Get(), nullptr);
  EXPECT_EQ(b.Get(), nullptr);
}

}  // namespace
```

- [ ] **Step 2: CMakeLists 追加目标（此时源文件未写，构建应失败）**

模块 CMakeLists 末尾追加：

```cmake
# -----------------------------------------------------------------------------
# trt_core：公共设施 + engine 构建/运行时（随 M1 各 Task 增量加源文件）
# -----------------------------------------------------------------------------
add_library(trt_core STATIC
  trt_common.cpp
)
target_include_directories(trt_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${TRT_INCLUDE_DIR}
)
target_link_libraries(trt_core PUBLIC
  ${TRT_NVINFER_LIB}
  ${TRT_NVONNXPARSER_LIB}
  CUDA::cudart
)
target_compile_features(trt_core PUBLIC cxx_std_20)

if(BUILD_TESTING)
  add_executable(trt_common_test trt_common_test.cpp)
  target_link_libraries(trt_common_test
    PRIVATE trt_core GTest::gtest_main Threads::Threads)
  add_test(NAME Trt_CommonTest COMMAND trt_common_test)
endif()
```

```bash
cmake --build build-gpu --target trt_common_test -j$(nproc)
```

期望：FAIL——`trt_common.cpp`/`trt_common.hpp` 不存在。

- [ ] **Step 3: 实现** `trt_common.hpp`：

```cpp
// SPDX-License-Identifier: MIT
//
// 文件功能：trt 公共设施 —— ILogger 适配器、CUDA 错误检查宏、GPU 显存 RAII、
//           文件读取工具。

#ifndef EDGE_AI_GENESIS_2026_TENSORRT_TRT_COMMON_HPP_
#define EDGE_AI_GENESIS_2026_TENSORRT_TRT_COMMON_HPP_

#include <NvInfer.h>
#include <cuda_runtime_api.h>

#include <cstddef>
#include <format>
#include <stdexcept>
#include <string>
#include <vector>

namespace trt {

// TRT 日志适配器：kWARNING 及以上透传 stderr——显式告警不静默（承接 quant
// 硬化原则）。TRT 要求 logger 生命周期覆盖全部 TRT 对象，统一走 GlobalLogger()。
class Logger final : public nvinfer1::ILogger {
 public:
  void log(Severity severity, const char* msg) noexcept override;
};

[[nodiscard]] Logger& GlobalLogger();

// GPU 显存 RAII：cudaMalloc/cudaFree 配对，move-only（显存句柄不可共享复制）。
class DeviceBuffer {
 public:
  DeviceBuffer() = default;
  explicit DeviceBuffer(std::size_t bytes);
  ~DeviceBuffer();
  DeviceBuffer(DeviceBuffer&& other) noexcept;
  DeviceBuffer& operator=(DeviceBuffer&& other) noexcept;
  DeviceBuffer(const DeviceBuffer&) = delete;
  DeviceBuffer& operator=(const DeviceBuffer&) = delete;

  [[nodiscard]] void* Get() const { return ptr_; }
  [[nodiscard]] std::size_t Bytes() const { return bytes_; }

 private:
  void* ptr_ = nullptr;
  std::size_t bytes_ = 0;
};

// 整读二进制文件（ONNX 模型 / engine 缓存），失败抛 std::runtime_error。
[[nodiscard]] std::vector<char> ReadFileBytes(const std::string& path);

}  // namespace trt

// CUDA 错误检查宏：每个 CUDA 调用后必查，失败抛异常并带调用点上下文
// （spec §6：显式告警不静默回退）。do-while(false) 保证宏在 if/else 中安全。
#define TRT_CUDA_CHECK(expr)                                            \
  do {                                                                  \
    const cudaError_t trt_cuda_check_err_ = (expr);                     \
    if (trt_cuda_check_err_ != cudaSuccess) {                           \
      throw std::runtime_error(std::format(                             \
          "[trt] CUDA 调用失败: {} @ {}:{} -> {}", #expr, __FILE__,     \
          __LINE__, cudaGetErrorString(trt_cuda_check_err_)));          \
    }                                                                   \
  } while (false)

#endif  // EDGE_AI_GENESIS_2026_TENSORRT_TRT_COMMON_HPP_
```

`trt_common.cpp`：

```cpp
// SPDX-License-Identifier: MIT
//
// 文件功能：trt 公共设施实现 —— 详见 trt_common.hpp。

#include "trt_common.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <utility>

namespace trt {

void Logger::log(Severity severity, const char* msg) noexcept {
  // Severity 枚举值越小越严重（kINTERNAL_ERROR=0 … kVERBOSE=4）。
  if (severity <= Severity::kWARNING) {
    const char* tag = severity == Severity::kWARNING ? "W" : "E";
    std::fprintf(stderr, "[trt][%s] %s\n", tag, msg);
  }
}

Logger& GlobalLogger() {
  static Logger logger;
  return logger;
}

DeviceBuffer::DeviceBuffer(std::size_t bytes) : bytes_(bytes) {
  TRT_CUDA_CHECK(cudaMalloc(&ptr_, bytes));
}

DeviceBuffer::~DeviceBuffer() {
  if (ptr_ != nullptr) {
    // 析构路径不抛异常，释放失败只能忽略（进程退出时 CUDA 上下文自会回收）。
    static_cast<void>(cudaFree(ptr_));
  }
}

DeviceBuffer::DeviceBuffer(DeviceBuffer&& other) noexcept
    : ptr_(std::exchange(other.ptr_, nullptr)),
      bytes_(std::exchange(other.bytes_, 0)) {}

DeviceBuffer& DeviceBuffer::operator=(DeviceBuffer&& other) noexcept {
  if (this != &other) {
    if (ptr_ != nullptr) {
      static_cast<void>(cudaFree(ptr_));
    }
    ptr_ = std::exchange(other.ptr_, nullptr);
    bytes_ = std::exchange(other.bytes_, 0);
  }
  return *this;
}

std::vector<char> ReadFileBytes(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("[trt] 无法读取文件: " + path);
  }
  return {std::istreambuf_iterator<char>(file),
          std::istreambuf_iterator<char>()};
}

}  // namespace trt
```

- [ ] **Step 4: 跑测试确认通过**

```bash
cmake --build build-gpu --target trt_common_test -j$(nproc)
ctest --test-dir build-gpu -R Trt_CommonTest --output-on-failure
```

期望：2 个用例 PASS（本机有 GPU，不触发 SKIP）。

- [ ] **Step 5: Commit**

```bash
git add 02_Inference_Analysis/tensorrt/
git commit -m "feat(trt): 公共设施——ILogger 适配 + CUDA 检查宏 + DeviceBuffer RAII

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 5: engine_builder — ONNX → FP16 engine + 缓存戳（TDD）

**Files:**
- Create: `02_Inference_Analysis/tensorrt/engine_builder.hpp`
- Create: `02_Inference_Analysis/tensorrt/engine_builder.cpp`
- Create: `02_Inference_Analysis/tensorrt/trt_engine_test.cpp`
- Modify: `02_Inference_Analysis/tensorrt/CMakeLists.txt`

- [ ] **Step 1: 写失败测试** `trt_engine_test.cpp`：

```cpp
// SPDX-License-Identifier: MIT
//
// 文件功能：trt engine 测试 —— FP16 构建 + 缓存命中 +（Task 6 扩展）单帧 smoke。

#include "engine_builder.hpp"

#include <cuda_runtime_api.h>
#include <filesystem>
#include <gtest/gtest.h>
#include <string>

namespace {

bool CudaAvailable() {
  int count = 0;
  return cudaGetDeviceCount(&count) == cudaSuccess && count > 0;
}

void SkipIfEnvMissing() {
  if (!CudaAvailable()) {
    GTEST_SKIP() << "无 CUDA 设备（CI/VPS），跳过";
  }
  if (!std::filesystem::exists(TRT_MODEL_PATH)) {
    GTEST_SKIP() << "缺少 yolov8n.onnx（W16 资产）";
  }
}

// 首次运行会真实构建 FP16 engine，3060 上约 1~4 分钟；之后命中缓存秒级。
TEST(TrtEngineBuilder, BuildsAndCachesFp16Engine) {
  SkipIfEnvMissing();

  const trt::BuildConfig config{
      .onnx_path = TRT_MODEL_PATH,
      .cache_dir = TRT_ENGINE_CACHE_DIR,
      .precision = trt::Precision::kFp16,
  };

  const std::string first = trt::BuildOrLoadEngine(config);
  ASSERT_TRUE(std::filesystem::exists(first));
  EXPECT_GT(std::filesystem::file_size(first), 1024u * 1024u);  // 至少 MB 级
  const auto mtime = std::filesystem::last_write_time(first);

  // 第二次调用：路径一致 + 未重建（mtime 不变）即缓存命中。
  const std::string second = trt::BuildOrLoadEngine(config);
  EXPECT_EQ(first, second);
  EXPECT_EQ(std::filesystem::last_write_time(second), mtime);
}

}  // namespace
```

- [ ] **Step 2: CMakeLists 接入 + 确认失败**

`trt_core` 源列表加 `engine_builder.cpp`；`if(BUILD_TESTING)` 块内追加：

```cmake
  add_executable(trt_engine_test trt_engine_test.cpp)
  target_link_libraries(trt_engine_test
    PRIVATE trt_core GTest::gtest_main Threads::Threads)
  target_compile_definitions(trt_engine_test PRIVATE
    TRT_MODEL_PATH="${_TRT_W16_MODELS}/yolov8n.onnx"
    TRT_ENGINE_CACHE_DIR="${_TRT_ENGINE_CACHE}"
  )
  add_test(NAME Trt_EngineTest COMMAND trt_engine_test)
```

```bash
cmake --build build-gpu --target trt_engine_test -j$(nproc)
```

期望：FAIL——`engine_builder.hpp` 不存在。

- [ ] **Step 3: 实现** `engine_builder.hpp`：

```cpp
// SPDX-License-Identifier: MIT
//
// 文件功能：trt engine 构建器 —— ONNX → TRT engine（FP16/FP32），带磁盘缓存。

#ifndef EDGE_AI_GENESIS_2026_TENSORRT_ENGINE_BUILDER_HPP_
#define EDGE_AI_GENESIS_2026_TENSORRT_ENGINE_BUILDER_HPP_

#include <cstddef>
#include <string>

namespace trt {

// kFp32 保留作 FP16 一致性排查对照与 M2 显式 INT8 的基线；kInt8 归 M2。
enum class Precision { kFp16, kFp32 };

struct BuildConfig {
  std::string onnx_path;
  std::string cache_dir;  // 不存在时自动创建
  Precision precision = Precision::kFp16;
  // spec 风险 2：WSL 内存有限，显式限制 builder 工作区（默认 1GB）。
  std::size_t workspace_bytes = 1ULL << 30;
};

// ONNX → TRT engine，返回 engine 文件路径。
//
// 缓存戳（spec §6）：模型内容哈希 + TRT 运行库版本 + 精度 + SM 架构全部编入
// 文件名——任一变化 → 文件名变 → 未命中自动重建。engine 不跨 TRT 版本 /
// 跨卡兼容，靠文件名戳而非 sidecar 元数据，省一次一致性维护。
// 失败抛 std::runtime_error（对齐 w16/quant 错误风格）。
[[nodiscard]] std::string BuildOrLoadEngine(const BuildConfig& config);

}  // namespace trt

#endif  // EDGE_AI_GENESIS_2026_TENSORRT_ENGINE_BUILDER_HPP_
```

`engine_builder.cpp`：

```cpp
// SPDX-License-Identifier: MIT
//
// 文件功能：trt engine 构建器实现 —— 详见 engine_builder.hpp。

#include "engine_builder.hpp"

#include "trt_common.hpp"

#include <NvInfer.h>
#include <NvOnnxParser.h>
#include <cuda_runtime_api.h>

#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace trt {

namespace {

// FNV-1a 64bit：内容指纹（非加密用途），避免为一个哈希引第三方库。
[[nodiscard]] std::uint64_t Fnv1aHash(const std::vector<char>& data) {
  std::uint64_t hash = 1469598103934665603ULL;
  for (const char c : data) {
    hash ^= static_cast<unsigned char>(c);
    hash *= 1099511628211ULL;
  }
  return hash;
}

[[nodiscard]] std::string SmTag() {
  int device = 0;
  TRT_CUDA_CHECK(cudaGetDevice(&device));
  cudaDeviceProp prop{};
  TRT_CUDA_CHECK(cudaGetDeviceProperties(&prop, device));
  return std::format("sm{}{}", prop.major, prop.minor);
}

[[nodiscard]] const char* PrecisionTag(Precision precision) {
  return precision == Precision::kFp16 ? "fp16" : "fp32";
}

}  // namespace

std::string BuildOrLoadEngine(const BuildConfig& config) {
  const std::vector<char> onnx_bytes = ReadFileBytes(config.onnx_path);

  const std::string stem =
      std::filesystem::path(config.onnx_path).stem().string();
  // getInferLibVersion()：运行时链到的 TRT 版本（如 101601），比头文件宏更真。
  const std::string cache_name = std::format(
      "{}.{}.{}.trt{}.{:016x}.plan", stem, PrecisionTag(config.precision),
      SmTag(), getInferLibVersion(), Fnv1aHash(onnx_bytes));
  const std::filesystem::path cache_path =
      std::filesystem::path(config.cache_dir) / cache_name;

  if (std::filesystem::exists(cache_path)) {
    return cache_path.string();
  }

  // Builder 链：IBuilder → INetworkDefinition ← IParser，IBuilderConfig 调参，
  // buildSerializedNetwork 直接产出序列化 engine（TRT 10 已移除
  // buildEngineWithConfig）。TRT 10 接口支持直接 delete，unique_ptr 即 RAII。
  std::unique_ptr<nvinfer1::IBuilder> builder(
      nvinfer1::createInferBuilder(GlobalLogger()));
  if (builder == nullptr) {
    throw std::runtime_error("[trt] createInferBuilder 失败");
  }

  // TRT 10 网络恒为 explicit batch，flags 传 0。
  std::unique_ptr<nvinfer1::INetworkDefinition> network(
      builder->createNetworkV2(0U));
  if (network == nullptr) {
    throw std::runtime_error("[trt] createNetworkV2 失败");
  }

  // parser 持有 network 引用：两者生命周期都要覆盖到 build 结束。
  std::unique_ptr<nvonnxparser::IParser> parser(
      nvonnxparser::createParser(*network, GlobalLogger()));
  if (parser == nullptr) {
    throw std::runtime_error("[trt] createParser 失败");
  }
  if (!parser->parseFromFile(
          config.onnx_path.c_str(),
          static_cast<int>(nvinfer1::ILogger::Severity::kWARNING))) {
    throw std::runtime_error("[trt] ONNX 解析失败（详见上方 TRT 日志）: " +
                             config.onnx_path);
  }

  std::unique_ptr<nvinfer1::IBuilderConfig> builder_config(
      builder->createBuilderConfig());
  if (builder_config == nullptr) {
    throw std::runtime_error("[trt] createBuilderConfig 失败");
  }
  builder_config->setMemoryPoolLimit(nvinfer1::MemoryPoolType::kWORKSPACE,
                                     config.workspace_bytes);
  if (config.precision == Precision::kFp16) {
    builder_config->setFlag(nvinfer1::BuilderFlag::kFP16);
  }

  std::unique_ptr<nvinfer1::IHostMemory> serialized(
      builder->buildSerializedNetwork(*network, *builder_config));
  if (serialized == nullptr) {
    throw std::runtime_error("[trt] engine 构建失败（详见上方 TRT 日志）: " +
                             config.onnx_path);
  }

  std::filesystem::create_directories(config.cache_dir);
  // 先写 .tmp 再 rename：构建中途进程被杀不会留下半截文件被当缓存命中。
  const std::filesystem::path tmp_path{cache_path.string() + ".tmp"};
  {
    std::ofstream out(tmp_path, std::ios::binary);
    if (!out) {
      throw std::runtime_error("[trt] 无法写 engine 缓存: " +
                               tmp_path.string());
    }
    out.write(static_cast<const char*>(serialized->data()),
              static_cast<std::streamsize>(serialized->size()));
  }
  std::filesystem::rename(tmp_path, cache_path);
  return cache_path.string();
}

}  // namespace trt
```

- [ ] **Step 4: 跑测试确认通过（首次构建 1~4 分钟属正常，会打 TRT tactic 日志）**

```bash
cmake --build build-gpu --target trt_engine_test -j$(nproc)
ctest --test-dir build-gpu -R Trt_EngineTest --output-on-failure
```

期望：PASS；`build-gpu/02_Inference_Analysis/tensorrt/engine_cache/` 出现形如 `yolov8n.fp16.sm86.trt101601.<hash>.plan` 的文件。

- [ ] **Step 5: Commit**

```bash
git add 02_Inference_Analysis/tensorrt/
git commit -m "feat(trt): engine_builder——ONNX→FP16 engine + 缓存戳落盘

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 6: trt_engine — 反序列化 + enqueueV3 运行时（TDD）

**Files:**
- Create: `02_Inference_Analysis/tensorrt/trt_engine.hpp`
- Create: `02_Inference_Analysis/tensorrt/trt_engine.cpp`
- Modify: `02_Inference_Analysis/tensorrt/trt_engine_test.cpp`（追加 smoke 用例）
- Modify: `02_Inference_Analysis/tensorrt/CMakeLists.txt`（trt_core 加源文件）

- [ ] **Step 1: 追加失败测试**——`trt_engine_test.cpp` 顶部 `#include "engine_builder.hpp"` 后加 `#include "trt_engine.hpp"`、`#include <algorithm>`、`#include <cmath>`、`#include <vector>`，文件末尾（匿名命名空间内）追加：

```cpp
TEST(TrtEngine, SingleFrameSmokeInference) {
  SkipIfEnvMissing();

  const std::string engine_path = trt::BuildOrLoadEngine(trt::BuildConfig{
      .onnx_path = TRT_MODEL_PATH,
      .cache_dir = TRT_ENGINE_CACHE_DIR,
      .precision = trt::Precision::kFp16,
  });
  trt::TrtEngine engine(engine_path);

  // yolov8n 固定形状：输入 1×3×640×640，输出 [1,84,8400]。
  EXPECT_EQ(engine.InputCount(), 1u * 3u * 640u * 640u);
  ASSERT_EQ(engine.OutputShape().size(), 3u);
  EXPECT_EQ(engine.OutputShape()[1], 84);
  EXPECT_EQ(engine.OutputShape()[2], 8400);

  const std::vector<float> input(engine.InputCount(), 0.5f);
  const std::vector<float> output = engine.Infer(input);

  ASSERT_EQ(output.size(), engine.OutputCount());
  EXPECT_TRUE(std::all_of(output.begin(), output.end(),
                          [](float v) { return std::isfinite(v); }));
  // 前 4 通道是像素坐标（cx,cy,w,h），合理输出必有大于 1 的值。
  EXPECT_GT(*std::max_element(output.begin(), output.end()), 1.0f);
}
```

```bash
cmake --build build-gpu --target trt_engine_test -j$(nproc)
```

期望：FAIL——`trt_engine.hpp` 不存在。

- [ ] **Step 2: 实现** `trt_engine.hpp`：

```cpp
// SPDX-License-Identifier: MIT
//
// 文件功能：trt 运行时引擎 —— engine 反序列化 + IExecutionContext +
//           GPU 缓冲管理，同步单帧推理。

#ifndef EDGE_AI_GENESIS_2026_TENSORRT_TRT_ENGINE_HPP_
#define EDGE_AI_GENESIS_2026_TENSORRT_TRT_ENGINE_HPP_

#include "trt_common.hpp"

#include <NvInfer.h>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace trt {

// TRT engine 运行时包装：固定形状单输入单输出（YAGNI：不做 dynamic shape）。
// 成员声明顺序即析构逆序契约：context_ 必须先于 engine_ 先于 runtime_ 析构，
// 所以按 runtime_ → engine_ → context_ 顺序声明。
class TrtEngine {
 public:
  explicit TrtEngine(const std::string& engine_path);
  ~TrtEngine();
  TrtEngine(const TrtEngine&) = delete;
  TrtEngine& operator=(const TrtEngine&) = delete;

  // 同步推理：H2D → enqueueV3 → D2H → stream 同步，返回展平输出。
  // input.size() != InputCount() 时抛 std::invalid_argument。
  [[nodiscard]] std::vector<float> Infer(std::span<const float> input);

  [[nodiscard]] std::size_t InputCount() const { return input_count_; }
  [[nodiscard]] std::size_t OutputCount() const { return output_count_; }
  [[nodiscard]] const std::vector<std::int64_t>& OutputShape() const {
    return output_shape_;
  }

 private:
  std::unique_ptr<nvinfer1::IRuntime> runtime_;
  std::unique_ptr<nvinfer1::ICudaEngine> engine_;
  std::unique_ptr<nvinfer1::IExecutionContext> context_;
  std::string input_name_;
  std::string output_name_;
  std::size_t input_count_ = 0;
  std::size_t output_count_ = 0;
  std::vector<std::int64_t> output_shape_;
  DeviceBuffer input_buffer_;
  DeviceBuffer output_buffer_;
  cudaStream_t stream_ = nullptr;
};

}  // namespace trt

#endif  // EDGE_AI_GENESIS_2026_TENSORRT_TRT_ENGINE_HPP_
```

`trt_engine.cpp`：

```cpp
// SPDX-License-Identifier: MIT
//
// 文件功能：trt 运行时引擎实现 —— 详见 trt_engine.hpp。

#include "trt_engine.hpp"

#include <cuda_runtime_api.h>

#include <format>
#include <stdexcept>

namespace trt {

namespace {

[[nodiscard]] std::size_t ElementCount(const nvinfer1::Dims& dims) {
  std::size_t count = 1;
  for (std::int32_t i = 0; i < dims.nbDims; ++i) {
    count *= static_cast<std::size_t>(dims.d[i]);
  }
  return count;
}

}  // namespace

TrtEngine::TrtEngine(const std::string& engine_path) {
  const std::vector<char> blob = ReadFileBytes(engine_path);

  runtime_.reset(nvinfer1::createInferRuntime(GlobalLogger()));
  if (runtime_ == nullptr) {
    throw std::runtime_error("[trt] createInferRuntime 失败");
  }
  engine_.reset(runtime_->deserializeCudaEngine(blob.data(), blob.size()));
  if (engine_ == nullptr) {
    // 常见原因：engine 由其他 TRT 版本/显卡构建。缓存戳编在文件名里，
    // 正常流程走 BuildOrLoadEngine 不会进到这里。
    throw std::runtime_error("[trt] engine 反序列化失败: " + engine_path);
  }
  context_.reset(engine_->createExecutionContext());
  if (context_ == nullptr) {
    throw std::runtime_error("[trt] createExecutionContext 失败");
  }

  // 按 IO mode 识别输入/输出名（TRT 10 全面转向按名字寻址的 tensor API）。
  // yolov8n 恰为 1 入 1 出；多输入/多输出模型不在本交付物范围，直接报错。
  for (std::int32_t i = 0; i < engine_->getNbIOTensors(); ++i) {
    const char* name = engine_->getIOTensorName(i);
    if (engine_->getTensorIOMode(name) == nvinfer1::TensorIOMode::kINPUT) {
      if (!input_name_.empty()) {
        throw std::runtime_error("[trt] 仅支持单输入模型");
      }
      input_name_ = name;
    } else {
      if (!output_name_.empty()) {
        throw std::runtime_error("[trt] 仅支持单输出模型");
      }
      output_name_ = name;
    }
  }
  if (input_name_.empty() || output_name_.empty()) {
    throw std::runtime_error("[trt] 未找到输入/输出 tensor");
  }

  const nvinfer1::Dims in_dims = engine_->getTensorShape(input_name_.c_str());
  const nvinfer1::Dims out_dims =
      engine_->getTensorShape(output_name_.c_str());
  input_count_ = ElementCount(in_dims);
  output_count_ = ElementCount(out_dims);
  output_shape_.assign(out_dims.d, out_dims.d + out_dims.nbDims);

  // FP16 engine 的网络边界 I/O 默认仍是 FP32（kFP16 只影响内部层精度），
  // 缓冲按 float 分配。
  input_buffer_ = DeviceBuffer(input_count_ * sizeof(float));
  output_buffer_ = DeviceBuffer(output_count_ * sizeof(float));
  TRT_CUDA_CHECK(cudaStreamCreate(&stream_));

  // 地址固定（缓冲随对象生存期不变），构造期一次绑定，Infer 里不再重复设置。
  if (!context_->setTensorAddress(input_name_.c_str(), input_buffer_.Get()) ||
      !context_->setTensorAddress(output_name_.c_str(),
                                  output_buffer_.Get())) {
    throw std::runtime_error("[trt] setTensorAddress 失败");
  }
}

TrtEngine::~TrtEngine() {
  if (stream_ != nullptr) {
    // 析构路径不抛：同步失败也要继续销毁 stream。
    static_cast<void>(cudaStreamSynchronize(stream_));
    static_cast<void>(cudaStreamDestroy(stream_));
  }
}

std::vector<float> TrtEngine::Infer(std::span<const float> input) {
  if (input.size() != input_count_) {
    throw std::invalid_argument(
        std::format("[trt] 输入元素数不符: got {} want {}", input.size(),
                    input_count_));
  }
  TRT_CUDA_CHECK(cudaMemcpyAsync(input_buffer_.Get(), input.data(),
                                 input.size_bytes(), cudaMemcpyHostToDevice,
                                 stream_));
  if (!context_->enqueueV3(stream_)) {
    throw std::runtime_error("[trt] enqueueV3 失败");
  }
  std::vector<float> output(output_count_);
  TRT_CUDA_CHECK(cudaMemcpyAsync(output.data(), output_buffer_.Get(),
                                 output_count_ * sizeof(float),
                                 cudaMemcpyDeviceToHost, stream_));
  TRT_CUDA_CHECK(cudaStreamSynchronize(stream_));
  return output;
}

}  // namespace trt
```

- [ ] **Step 3: CMakeLists——`trt_core` 源列表加 `trt_engine.cpp`**

```cmake
add_library(trt_core STATIC
  trt_common.cpp
  engine_builder.cpp
  trt_engine.cpp
)
```

- [ ] **Step 4: 跑测试确认通过（缓存已建好，应秒级）**

```bash
cmake --build build-gpu --target trt_engine_test -j$(nproc)
ctest --test-dir build-gpu -R Trt_EngineTest --output-on-failure
```

期望：2 个用例 PASS。

- [ ] **Step 5: Commit**

```bash
git add 02_Inference_Analysis/tensorrt/
git commit -m "feat(trt): TrtEngine 运行时——反序列化 + enqueueV3 + GPU 缓冲 RAII

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 7: Trt_ConsistencyTest — TRT FP16 vs W16 参考检测对拍

**Files:**
- Create: `02_Inference_Analysis/tensorrt/trt_consistency_test.cpp`
- Modify: `02_Inference_Analysis/tensorrt/CMakeLists.txt`

- [ ] **Step 1: 写测试**（匹配规则照抄 `w16_yolo_detector/yolo_detector_test.cpp`：框数相等 + 每个参考框有同类 / IoU>0.9 / score±0.05 的匹配）：

```cpp
// SPDX-License-Identifier: MIT
//
// 文件功能：trt 一致性测试 —— TRT FP16 检测结果 vs W16 ultralytics 参考对拍。
//           前后处理复用 w10 letterbox + w16 decode/NMS，只有 infer 段换 TRT。

#include "engine_builder.hpp"
#include "trt_engine.hpp"

#include "custom_resize.hpp"  // w10::LetterboxToTensor
#include "decode.hpp"         // w16::DecodeYolov8
#include "nms.hpp"            // w16::Nms / w16::IoU

#include <cmath>
#include <cuda_runtime_api.h>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <span>
#include <vector>

namespace {

bool CudaAvailable() {
  int count = 0;
  return cudaGetDeviceCount(&count) == cudaSuccess && count > 0;
}

// 与 w16_yolo_detector/yolo_detector_test.cpp 相同的参考文件格式：
// 每行 "class_id score x1 y1 x2 y2"（原图坐标，xyxy）。
std::vector<w16::Detection> LoadReference(const std::string& path) {
  std::ifstream f(path);
  std::vector<w16::Detection> refs;
  int cls = 0;
  float score = 0.0f, x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f;
  while (f >> cls >> score >> x1 >> y1 >> x2 >> y2) {
    refs.push_back(w16::Detection{x1, y1, x2, y2, score, cls});
  }
  return refs;
}

TEST(TrtConsistency, Fp16MatchesUltralyticsReference) {
  if (!CudaAvailable()) {
    GTEST_SKIP() << "无 CUDA 设备（CI/VPS），跳过";
  }
  if (!std::filesystem::exists(TRT_MODEL_PATH) ||
      !std::filesystem::exists(TRT_IMAGE_PATH) ||
      !std::filesystem::exists(TRT_REFERENCE_PATH)) {
    GTEST_SKIP() << "缺少模型/图片/参考文件（W16 资产）";
  }

  // 前处理：与 W16 完全一致（BGR→RGB→letterbox /255 CHW）。
  const cv::Mat bgr = cv::imread(TRT_IMAGE_PATH, cv::IMREAD_COLOR);
  ASSERT_FALSE(bgr.empty());
  cv::Mat rgb;
  cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
  w10::LetterboxInfo info{};
  const std::vector<float> input =
      w10::LetterboxToTensor(rgb, 640, 640, info);

  // infer 段换 TRT FP16。
  const std::string engine_path = trt::BuildOrLoadEngine(trt::BuildConfig{
      .onnx_path = TRT_MODEL_PATH,
      .cache_dir = TRT_ENGINE_CACHE_DIR,
      .precision = trt::Precision::kFp16,
  });
  trt::TrtEngine engine(engine_path);
  const std::vector<float> output = engine.Infer(input);

  // 后处理：复用 w16 decode + NMS（阈值与 W16 默认一致）。
  ASSERT_EQ(engine.OutputShape().size(), 3u);
  const int num_channels = static_cast<int>(engine.OutputShape()[1]);
  const int num_anchors = static_cast<int>(engine.OutputShape()[2]);
  std::vector<w16::Detection> got = w16::DecodeYolov8(
      std::span<const float>(output.data(), output.size()), num_channels - 4,
      num_anchors, w16::DecodeOptions{.conf_thresh = 0.25f}, info.scale,
      info.pad_left, info.pad_top, bgr.cols, bgr.rows);
  got = w16::Nms(std::move(got), w16::NmsOptions{.iou_thresh = 0.45f});

  const std::vector<w16::Detection> refs = LoadReference(TRT_REFERENCE_PATH);
  ASSERT_FALSE(refs.empty()) << "参考文件为空";
  EXPECT_EQ(got.size(), refs.size());

  // 匹配规则与 W16 测试一致：FP16 量化误差远小于 INT8（quant 已验证 INT8
  // 都能过同源对拍），阈值不放宽。若 FP16 过不了 score±0.05，先记录实际
  // 偏差进 notes.md 再议，不悄悄放宽。
  for (const w16::Detection& r : refs) {
    bool matched = false;
    for (const w16::Detection& g : got) {
      if (g.class_id == r.class_id && w16::IoU(g, r) > 0.9f &&
          std::abs(g.score - r.score) < 0.05f) {
        matched = true;
        break;
      }
    }
    EXPECT_TRUE(matched) << "参考框未匹配: class=" << r.class_id
                         << " score=" << r.score << " box=[" << r.x1 << ","
                         << r.y1 << "," << r.x2 << "," << r.y2 << "]";
  }
}

}  // namespace
```

- [ ] **Step 2: CMakeLists 接入**——`if(BUILD_TESTING)` 块内追加：

```cmake
  add_executable(trt_consistency_test trt_consistency_test.cpp)
  target_link_libraries(trt_consistency_test
    PRIVATE trt_core w16_detect_core w10_resize_lib ${OpenCV_LIBS}
            GTest::gtest_main Threads::Threads)
  target_compile_definitions(trt_consistency_test PRIVATE
    TRT_MODEL_PATH="${_TRT_W16_MODELS}/yolov8n.onnx"
    TRT_IMAGE_PATH="${_TRT_W16_MODELS}/test_image.jpg"
    TRT_REFERENCE_PATH="${_TRT_W16_MODELS}/reference_detections.txt"
    TRT_ENGINE_CACHE_DIR="${_TRT_ENGINE_CACHE}"
  )
  add_test(NAME Trt_ConsistencyTest COMMAND trt_consistency_test)
```

- [ ] **Step 3: 跑测试**

```bash
cmake --build build-gpu --target trt_consistency_test -j$(nproc)
ctest --test-dir build-gpu -R Trt_ConsistencyTest --output-on-failure
```

期望：PASS（engine 缓存已在，秒级）。若 score 容差失败：打印实际 got/ref 分数差，把偏差数字记入 notes.md 后再决定是否放宽（放宽必须留注释带实测数字）。

- [ ] **Step 4: 全量回归（确认没碰坏别的模块）**

```bash
ctest --test-dir build-gpu --output-on-failure 2>&1 | tail -15
```

期望：全部 PASS（或与改动前相同的既有跳过项）。

- [ ] **Step 5: Commit**

```bash
git add 02_Inference_Analysis/tensorrt/
git commit -m "test(trt): FP16 一致性对拍——TRT infer + w16 前后处理 vs ultralytics 参考

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 8: trt_benchmark — FP16 纯 infer 实测 vs CUDA EP 基线

**Files:**
- Create: `02_Inference_Analysis/tensorrt/trt_benchmark.cpp`
- Modify: `02_Inference_Analysis/tensorrt/CMakeLists.txt`

- [ ] **Step 1: 写 benchmark**（口径对齐 `quant_benchmark.cpp`：warmup=5 / iters=20 / `quant::RollingStats` P50/P99；「纯 infer」含 H2D/D2H 与 stream 同步，对齐 ORT `Run()` 的口径——CUDA EP 的 5.64ms 同样含内部拷贝）：

```cpp
// SPDX-License-Identifier: MIT
//
// 文件功能：trt M1 基准 —— TRT FP16 纯 infer 延迟（P50/P99），
//           对比 quant 同机 CUDA EP FP32 基线（引用不重测）。

#include "engine_builder.hpp"
#include "trt_engine.hpp"

#include "custom_resize.hpp"  // w10::LetterboxToTensor
#include "rolling_stats.hpp"  // quant::RollingStats（统计口径对齐 quant）

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <string>
#include <vector>

namespace {

constexpr int kInput = 640;
constexpr int kWarmup = 5;  // 对齐 quant_benchmark 口径
constexpr int kIters = 20;  // 对齐 quant_benchmark 口径
// quant_int8_report.md 同机基线（2026-07 实测），spec 明确引用不重测。
constexpr double kCudaEpFp32P50Ms = 5.64;

}  // namespace

int main(int argc, char** argv) {
  const std::string image_path = argc > 1 ? argv[1] : TRT_IMAGE_PATH;
  const cv::Mat bgr = cv::imread(image_path, cv::IMREAD_COLOR);
  if (bgr.empty()) {
    std::fprintf(stderr, "图片读取失败: %s\n", image_path.c_str());
    return 1;
  }

  cv::Mat rgb;
  cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
  w10::LetterboxInfo info{};
  const std::vector<float> input =
      w10::LetterboxToTensor(rgb, kInput, kInput, info);

  const std::string engine_path = trt::BuildOrLoadEngine(trt::BuildConfig{
      .onnx_path = TRT_MODEL_PATH,
      .cache_dir = TRT_ENGINE_CACHE_DIR,
      .precision = trt::Precision::kFp16,
  });
  trt::TrtEngine engine(engine_path);

  for (int i = 0; i < kWarmup; ++i) {
    static_cast<void>(engine.Infer(input));
  }

  quant::RollingStats stats(128);
  for (int i = 0; i < kIters; ++i) {
    const auto start = std::chrono::steady_clock::now();
    static_cast<void>(engine.Infer(input));
    const auto end = std::chrono::steady_clock::now();
    const double ms =
        std::chrono::duration<double, std::milli>(end - start).count();
    stats.Add(quant::StageLatencyMs{.infer = ms, .total = ms});
  }
  const quant::StageLatencyStats summary = stats.Summary();

  const double engine_mb =
      static_cast<double>(std::filesystem::file_size(engine_path)) /
      (1024.0 * 1024.0);
  std::printf("# trt M1 Benchmark（TRT FP16 纯 infer）\n\n");
  std::printf("- image: `%s`\n", image_path.c_str());
  std::printf("- engine: `%s`（%.1f MB）\n", engine_path.c_str(), engine_mb);
  std::printf(
      "- 口径: 含 H2D/D2H 与 stream 同步（对齐 ORT Run），warmup=%d "
      "iters=%d\n\n",
      kWarmup, kIters);
  std::printf("| 路线 | P50(ms) | P99(ms) | FPS |\n");
  std::printf("|---|---:|---:|---:|\n");
  std::printf("| CUDA EP FP32（quant 同机引用） | %.2f | — | %.1f |\n",
              kCudaEpFp32P50Ms, 1000.0 / kCudaEpFp32P50Ms);
  std::printf("| TRT FP16（实测） | %.2f | %.2f | %.1f |\n", summary.infer.p50,
              summary.infer.p99, 1000.0 / summary.infer.p50);
  std::printf("\n> TRT FP16 vs CUDA EP FP32: %+.1f%%（负值 = TRT 更快）\n",
              (summary.infer.p50 - kCudaEpFp32P50Ms) * 100.0 /
                  kCudaEpFp32P50Ms);
  return 0;
}
```

- [ ] **Step 2: CMakeLists 接入**（`quant_core` 传递依赖 ORT，带 RPATH；quant 模块被跳过的环境下不建 benchmark）——模块 CMakeLists 末尾追加：

```cmake
# -----------------------------------------------------------------------------
# trt_benchmark：统计口径链 quant_core（RollingStats）。quant_core 缺失（ORT
# 不在）时跳过——benchmark 本身不用 ORT，但口径统一优先。
# -----------------------------------------------------------------------------
if(TARGET quant_core)
  add_executable(trt_benchmark trt_benchmark.cpp)
  target_link_libraries(trt_benchmark PRIVATE
    trt_core quant_core w10_resize_lib ${OpenCV_LIBS})
  target_compile_definitions(trt_benchmark PRIVATE
    TRT_MODEL_PATH="${_TRT_W16_MODELS}/yolov8n.onnx"
    TRT_IMAGE_PATH="${_TRT_W16_MODELS}/test_image.jpg"
    TRT_ENGINE_CACHE_DIR="${_TRT_ENGINE_CACHE}"
  )
  set_target_properties(trt_benchmark PROPERTIES
    BUILD_RPATH "${ONNXRUNTIME_ROOT}/lib")
else()
  message(WARNING "[trt] quant_core 缺失，跳过 trt_benchmark 构建。")
endif()
```

- [ ] **Step 3: 编译并跑基准，保存输出**

```bash
cmake --build build-gpu --target trt_benchmark -j$(nproc)
./build-gpu/02_Inference_Analysis/tensorrt/trt_benchmark | tee /tmp/claude-1000/-home-dev-code-Edge-AI-Genesis-2026/74206607-73bb-4eb7-8400-a43fecfe61e9/scratchpad/trt_m1_bench.md
```

期望：表格输出完整；TRT FP16 P50 预期落在 2~5ms 区间（trtexec 冒烟的 GPU Compute Time 可互验数量级；若明显偏离——如 >5.64ms——如实记录，M1 不做归因，量化归因是 M2/风险 3 的活）。

- [ ] **Step 4: Commit**

```bash
git add 02_Inference_Analysis/tensorrt/
git commit -m "feat(trt): M1 基准——TRT FP16 纯 infer vs CUDA EP FP32 引用基线

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 9: M1 文档收口 — 四路表骨架 + notes.md + 进度同步

**Files:**
- Create: `docs/benchmarks/trt_engine_report.md`
- Create: `02_Inference_Analysis/tensorrt/notes.md`
- Modify: `CLAUDE.md`（当前进度）
- Modify: `docs/Roadmap.md`（trt 交付物状态行）

- [ ] **Step 1: 写四路表骨架** `docs/benchmarks/trt_engine_report.md`（`<实测>` 占位从 Task 8 的 benchmark 输出抄真实数字；体积用 `ls -l` 实查）：

```markdown
# trt 四路对比报告（M1 骨架：TRT FP16 已实测，INT8 归 M2）

- 日期：2026-07-07（M1）
- 机器：RTX 3060 Laptop（SM 8.6）/ WSL2 / TRT 10.16.1 / CUDA 12.3
- 口径：纯 infer 含 H2D/D2H 与同步（对齐 ORT Run）；warmup=5 iters=20 P50；
  CPU EP / CUDA EP 列引用 `quant_int8_report.md` 同机数字，**不重测**
- mAP 口径：coco128，mAP50-95（quant 评估链路）

## 四路总表

| 路线 | 体积 | 纯 infer P50(ms) | mAP50-95 | 来源 |
|---|---:|---:|---:|---|
| CPU EP FP32 | 12.2MB（onnx） | 40.81 | 0.4454 | 引 quant |
| CPU EP INT8（QDQ MinMax） | <ls 实查>MB | 31.31 | 0.4285 | 引 quant |
| CUDA EP FP32 | 12.2MB（onnx） | 5.64 | ≈FP32（同权重） | 引 quant |
| CUDA EP INT8（QDQ MinMax） | <ls 实查>MB | 11.49（负优化 ~2×） | ≈CPU INT8 | 引 quant |
| **TRT FP16** | <实测>MB（plan） | **<实测>** | M2 评 | 本报告 M1 |
| TRT INT8（隐式 Calibrator） | M2 | M2 | M2 | 待做 |
| TRT INT8（显式 QDQ） | M2 | M2 | M2 | 待做 |

## M1 结论

- TRT FP16 vs CUDA EP FP32（5.64ms）：<实测差值与百分比>
- 一致性：TRT FP16 过 W16 ultralytics 参考对拍（框数相等 + IoU>0.9 + score±0.05）
- M2 待办：INT8 双路线（隐式/显式）+ mAP 闭环 + 「CUDA EP QDQ 慢 2× vs TRT
  显式量化」正面对比
```

- [ ] **Step 2: 写模块笔记** `02_Inference_Analysis/tensorrt/notes.md`（数据流 Mermaid 必画；只写 M1 已落地部分，M2/M3 留待各自收口时补）：

```markdown
# trt — TensorRT C++ Engine（M1：FP16 跑通）

上游 spec：`docs/superpowers/specs/2026-07-06-trt-design.md`
四路对比报告：`docs/benchmarks/trt_engine_report.md`

## 数据流（M1/M2 形态：前后处理在 CPU，测纯 infer）

```mermaid
flowchart LR
  A[JPEG] --> B["CPU letterbox<br/>(w10 复用)"]
  B --> C["H2D FP32 CHW<br/>~4.9MB"]
  C --> D["TRT FP16 engine<br/>enqueueV3"]
  D --> E["D2H 84×8400"]
  E --> F["CPU decode+NMS<br/>(w16 复用)"]
  F --> G[检测框]
```

M3 将切到全 GPU 流水线（H2D 上行缩小 ~5×，NMS 融进 engine），见 spec §5。

## M1 设计决策

- **错误风格**：抛 `std::runtime_error`（对齐 w16/quant，实施前核对过，非
  `std::expected`）；CUDA 调用全走 `TRT_CUDA_CHECK` 宏，显式报错不静默。
- **engine 缓存戳**：模型 FNV-1a 哈希 + `getInferLibVersion()` + 精度 + SM
  架构全部编入文件名——任一变化文件名即变，未命中自动重建，无 sidecar 元数据。
- **RAII**：TRT 10 接口支持直接 delete，`unique_ptr` 即可；`TrtEngine` 成员
  按 runtime→engine→context 声明，析构逆序天然满足 TRT 依赖顺序。
- **基准口径**：纯 infer 含 H2D/D2H 与同步，对齐 ORT `Run()`（CUDA EP 的
  5.64ms 同样含内部拷贝），保证四路表同口径可比。
- **M1 不编 .cu**：GPU buffer 用 cudart 主机 API；nvcc(+g++-12) 方案已冒烟
  验证，`enable_language(CUDA)` 留到 M3。

## 编译 / 运行

```bash
cmake --build build-gpu --target trt_engine_test trt_consistency_test trt_benchmark -j$(nproc)
ctest --test-dir build-gpu -R "Trt_" --output-on-failure
./build-gpu/02_Inference_Analysis/tensorrt/trt_benchmark
```

首次跑 engine 构建 1~4 分钟（落盘缓存 `build-gpu/.../engine_cache/`），之后秒级。

## M1 实测

<粘贴 Task 8 benchmark 输出表格>
```

- [ ] **Step 3: Mermaid 渲染验证（CLAUDE.md 规范：别凭眼睛猜语法）**

```bash
SCRATCH=/tmp/claude-1000/-home-dev-code-Edge-AI-Genesis-2026/74206607-73bb-4eb7-8400-a43fecfe61e9/scratchpad
npx -y @mermaid-js/mermaid-cli -i 02_Inference_Analysis/tensorrt/notes.md -o "$SCRATCH/notes-render.md"
```

期望：无报错，`$SCRATCH` 出现渲染出的 svg。报语法错就修到过为止。

- [ ] **Step 4: 进度同步**

CLAUDE.md「当前进度」段，把 Task 3 写的 `🟡 M1 进行中……` 换成一句话现状（模板，数字用实测替换）：

```
当前交付物：`trt`（TensorRT C++ Engine）🟡 M1 完成（2026-07-XX）——TRT 10.16 环境 + FP16 engine（C++ Builder 路径 + 缓存戳）+ 一致性对拍通过 + FP16 纯 infer <实测>ms vs CUDA EP 5.64ms（四路表骨架见 docs/benchmarks/trt_engine_report.md）；M2（INT8 双路线 + mAP 闭环）未开始。
```

`docs/Roadmap.md` 第 107 行 `#### 交付物 \`trt\` 🔴 …` 状态改为 `🟡`，并在该交付物块末尾加一行：

```
- **M1 完成（2026-07-XX）**：TRT 10.16 + FP16 engine 跑通 + 一致性对拍 + 四路表骨架（`docs/benchmarks/trt_engine_report.md`）；M2 INT8 双路线进行中
```

- [ ] **Step 5: Commit + push**

```bash
git add docs/benchmarks/trt_engine_report.md 02_Inference_Analysis/tensorrt/notes.md CLAUDE.md docs/Roadmap.md
git commit -m "docs(trt): M1 收口——四路表骨架 + 模块笔记 + 进度同步

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
git push origin dev
```

**注意**：只 push dev。合入 main 必须先问用户（CLAUDE.md 铁律）。

---

## 完成判据（对照 spec M1 节逐条）

- [x 对应任务] TRT 10.x 环境 + 版本锁定记 README → Task 1
- [x 对应任务] nvcc 宿主编译器方案确定（g++-12 冒烟）→ Task 2
- [x 对应任务] C++ Builder 路径走通 + FP16 engine 落盘缓存 → Task 5/6
- [x 对应任务] TRT FP16 vs `reference_detections.txt` 容差对拍 → Task 7
- [x 对应任务] TRT FP16 纯 infer vs CUDA EP FP32（5.64ms），CPU/CUDA EP 引用不重测 → Task 8/9
- [x 对应任务] 四路表骨架落 `docs/benchmarks/` → Task 9
