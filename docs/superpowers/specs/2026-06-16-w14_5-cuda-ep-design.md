# W14.5 设计：InferenceEngine CUDA EP 接入 + 可回退测试

> 状态：已 brainstorm，待 writing-plans
> 日期：2026-06-16
> 范围：W14 遗留任务清单（`02_Inference_Analysis/w14_ort_basics/notes.md` 「待补」节）第 1、3 项

## 1. 目标与边界

补齐 W14 当周裁掉的 GPU 路径：给 `w14::InferenceEngine` 接入 ONNX Runtime CUDA
ExecutionProvider，并补一个在纯 CPU 环境能优雅跳过、在本地 GPU 能真跑的单测。

**做**：
- 环境：装 CUDA 12.3 工具链 + cuDNN 9，换 ORT GPU 1.26 包。
- 代码：`InferenceEngine` 支持可选 CUDA EP，请求 CUDA 但不可用时优雅回退 CPU。
- 测试：`LoadsModelWithCudaEp`（GPU 真跑 / CPU 跳过）+ 默认 CPU 构造行为不变断言。
- 文档：README「前提条件」补 WSL CUDA/cuDNN/ORT GPU 包安装与排障命令。

**不做（YAGNI）**：
- ResNet18 对比（遗留清单第 2 项，与 EP 接入正交，留待后续）。
- TensorRT EP。
- CUDA provider 的高级参数（arena / stream / workspace 调优）—— 留 W16+ profiling。
- VPS CPU EP 环境搭建（遗留清单第 4 项，本次明确待定）。

## 2. 环境现状（2026-06-16 实测）

- GPU：RTX 3060 Laptop（6GB），驱动 546.30，host CUDA 12.3 能力，WSL passthrough
  的 `libcuda.so` 在 `/usr/lib/wsl/lib/`。
- 无 CUDA Toolkit：`nvcc` 不存在，`/usr/local/cuda*` 不存在。
- 当前 ORT 为 CPU 包：`third_party/onnxruntime/onnxruntime-linux-x64-1.26.0/`，
  目录无 `libonnxruntime_providers_cuda.so`。

## 3. 环境供给方案

### 3.1 版本 pin（强约束）

| 组件 | 版本 | 理由 |
|------|------|------|
| CUDA Toolkit | **12.3** | 对齐 host 驱动 546.30 的 CUDA 12.3 能力上限；ORT 1.26 GPU 构建在 CUDA 12.x 家族内兼容 |
| cuDNN | **9** | ORT 1.26 GPU 要求 cuDNN 9；cuDNN 8/9 ABI 不可混用 |
| ONNX Runtime | GPU **1.26.0** | 与现有 CPU 包同版本，便于对照 |

> 装前先核对 ORT 1.26 release notes / CUDA EP requirements 页确认精确的 CUDA/cuDNN
> 小版本要求，避免装错导致运行时 `dlopen` 失败。
> 参考：
> - https://onnxruntime.ai/docs/execution-providers/CUDA-ExecutionProvider.html
> - https://docs.nvidia.com/cuda/cuda-toolkit-release-notes/index.html

### 3.2 apt 安装（WSL 关键约束）

WSL 里 GPU driver 归 Windows host 管，**Linux 侧绝不装 driver**：

- 装具体工具链包 `cuda-toolkit-12-3`，**不要**装 `cuda` 或 `cuda-drivers`
  这类会拉 Linux driver 的 meta package。
- 「全量 Toolkit」理解为**工具链 / 运行时全量**，不是 driver 全量。
- cuDNN 9 走 NVIDIA repo 的 cuDNN 包单独装。

### 3.3 ORT GPU 包

- 下 `onnxruntime-linux-x64-gpu-1.26.0.tgz`，与现有 CPU 包**并排**放在
  `third_party/onnxruntime/` 下（不删 CPU 包，留作对照与 CPU-only 回退验证）。
- GPU 包是超集：自带 CPU EP + `libonnxruntime_providers_cuda.so` +
  `libonnxruntime_providers_shared.so`。

## 4. 代码设计：InferenceEngine

### 4.1 API 变更

```cpp
enum class Ep { kCpu, kCuda };

explicit InferenceEngine(const std::string& model_path, Ep ep = Ep::kCpu);

[[nodiscard]] Ep ActiveEp() const;                       // 实际生效的 EP
[[nodiscard]] const std::string& EpFallbackReason() const;  // 回退原因，未回退为空
```

- 构造参数 `ep` **默认 kCpu** → W15 `Classifier`、demo、旧测试源码零改动。
- 新增私有成员 `Ep active_ep_;`、`std::string ep_fallback_reason_;`。
- **库层不打日志**（不 `std::cerr`）：回退原因存进 `ep_fallback_reason_`，
  由 demo / test 决定是否打印。保持 InferenceEngine 是干净的库，不写死日志策略。

### 4.2 构造逻辑（fallback 包住整个 Session 创建）

失败可能发生在 `AppendExecutionProvider_CUDA`，也可能发生在 `Ort::Session` 创建阶段
（如 `libonnxruntime_providers_cuda.so` 找到了，但 `libcudnn.so.9` / `libcublas*`
dlopen 失败）。因此 try 块必须包住 **append + session 创建**整体：

```
active_ep_ = Ep::kCpu;            // 默认
if (ep == Ep::kCuda) {
  try {
    Ort::SessionOptions cuda_options;
    cuda_options.SetGraphOptimizationLevel(ORT_ENABLE_ALL);
    OrtCUDAProviderOptions cuda_opts{};          // device_id=0 默认
    cuda_options.AppendExecutionProvider_CUDA(cuda_opts);
    session_ = make_unique<Session>(GlobalEnv(), path, cuda_options);  // 失败点也可能在这
    active_ep_ = Ep::kCuda;
  } catch (const Ort::Exception& e) {
    ep_fallback_reason_ = e.what();              // 记录原因，落到 CPU 重建
  }
}
if (!session_) {                                 // 默认 CPU 或 CUDA 失败回退
  Ort::SessionOptions cpu_options;
  cpu_options.SetGraphOptimizationLevel(ORT_ENABLE_ALL);
  session_ = make_unique<Session>(GlobalEnv(), path, cpu_options);
}
// 之后照旧查询并缓存 I/O 元数据
```

- 模型文件不存在的前置检查、I/O 元数据缓存、`Run()` 零拷贝路径全部不变。
- CUDA 路径不新增对外抛出语义（回退而非抛错）；仅模型文件缺失 / CPU 路径本身失败
  仍按原逻辑抛 `std::runtime_error`。

### 4.3 ActiveEp() 语义（写进头文件注释）

`ActiveEp()` 只表示「Session 成功请求 / 注册了 CUDA EP」，**不能证明每个算子都落到
CUDA**。MobileNetV2 大概率全落 CUDA，但严格确认 kernel placement 要等 W16/W18 用
profiling / logging 看。

## 5. CMake 与构建

- `ONNXRUNTIME_ROOT` 指向哪个包就用哪个（GPU 包 / CPU 包并排，保留 `-D` 覆盖能力）。
- `libonnxruntime_providers_cuda.so` 由 ORT 运行时 dlopen，和 `libonnxruntime.so`
  同目录，现有 `BUILD_RPATH "${ONNXRUNTIME_ROOT}/lib"` 已覆盖。
- CUDA / cuDNN runtime 库走 ldconfig 或 `/usr/local/cuda/lib64`（apt 安装注册），
  运行时可达。
- 新增 `LoadsModelWithCudaEp` 编译进现有 `w14_inference_engine_test`。

## 6. 测试设计（保 CI 绿）

`inference_engine_test.cpp` 新增：

1. **`LoadsModelWithCudaEp`**：以 `Ep::kCuda` 构造引擎。
   - CUDA 不可用 → 引擎回退，`ActiveEp()==kCpu` → `GTEST_SKIP("CUDA EP 不可用，已回退
     CPU：" + EpFallbackReason())`。
   - CUDA 可用 → 断言 `ActiveEp()==kCuda`，跑一次推理，输出形状/数值合理。
2. **默认构造行为不变断言**：`InferenceEngine engine(kModelPath);`（不传 ep）→
   `ActiveEp()==kCpu` 且 `EpFallbackReason()` 为空。护住 W15 / 旧码零改动。

纯 CPU 的 VPS / CI 永远走 skip 分支，测试套保持绿。

## 7. 验收标准

1. **CPU 包 / CPU 环境**：所有 W14 / W15 测试仍绿，`LoadsModelWithCudaEp` 进入 skip。
2. **GPU 包 + CUDA 12.3 + cuDNN 9 环境**：`LoadsModelWithCudaEp` 不 skip，
   `ActiveEp()==Ep::kCuda`，推理跑通。
3. **README**：记录 WSL CUDA 12.3 / cuDNN 9 / ORT GPU 包的安装与排障命令，至少含
   `nvidia-smi`、`nvcc --version`、`ldconfig -p | grep cudnn` 三条验证命令。
