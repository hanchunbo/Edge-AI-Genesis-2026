# Quant 退出内存错误定位与修复（2026-09-11）

quant benchmark 已改为由入口持有局部 `Ort::Env`，全部会话和输出先销毁，Env 随后
销毁。修复前可稳定检测到的退出释放后访问（UAF）为 **2 处，修复后为 0 处**。
模型、CUDA、cuDNN、ORT 和驱动保持原版本；没有用提前结束进程或泄漏对象绕过清理。

原始 `corrupted double-linked list` 的具体 SIGABRT 没有在本轮再次出现。本报告确认的是
同一退出路径上可复现、会修改已释放堆块的缺陷及其修复；不据此排除所有潜在崩溃原因，
也不把几轮正常退出等同于长期稳定性验收。

## 从现象到证据

2026-09-10 的完整基准共尝试 6 次，5 次正常、1 次在打印全部表格后退出崩溃。
历史性能数据仍见 [profiling 报告](quant_profiling_report.md)，本轮没有改写其测量结果。

开始排查前，原始 benchmark、FP32/INT8 模型与测试图的 SHA-256 均与原始记录一致。
代码基线为 `ed97a30`。环境为 WSL Ubuntu 24.04.4、GCC 15.2、Ninja、ORT GPU 1.26.0、
RTX 3060 Laptop 6 GiB、驱动 546.30、CUDA runtime 12.3.101、cuDNN 9.23.1.3，
glibc 保持 `2.39-0ubuntu8.8`。

| 检查 | 观察 | 能证明什么 |
|---|---|---|
| 原逻辑，GDB 完整基准 3 轮 | 均正常退出 | 未抓到原 SIGABRT；不能证明没有错误 |
| 原逻辑，GDB 短基准 10 轮 | 均正常退出 | 减少迭代也没有稳定触发 abort |
| 原逻辑，ASAN 短基准 3 轮、完整基准 1 轮 | 均正常退出，无 ASAN 报错 | 预编译后端未插桩，工具有覆盖盲区 |
| Valgrind，仅创建 CUDA 会话、不推理 | Env 析构访问已释放的 64 字节控制块 | 错误无需卷积、decode 或 NMS 即可发生 |
| Valgrind，只有 Env 与 CUDA provider 初始化 | static Env 有 UAF，局部 Env 无此 UAF | 进一步隔离到 provider 注册表与 Env 退出顺序 |

### 为什么会访问已经释放的内存

旧路径先调用 `GlobalEnv()`，再加载 CUDA provider。后加载的库注册了更晚的静态析构：

1. 进程退出，CUDA provider 的静态 `s_kernel_registry` 先析构，释放 shared_ptr 控制块。
2. 随后旧 static `Ort::Env` 析构，触发 `UnloadSharedProviders()`。
3. provider 的 `Shutdown()` 调用 `DeleteRegistry()`，对已析构的注册表再次 `reset()`。
4. Valgrind 报告控制块释放后访问；库内偏移 `0x261812` 的指令为 `lock xadd`，
   会修改已释放控制块的引用计数。它可能损坏空闲块信息，即使本次没有立即 abort。

对应 ORT v1.26.0 源码：[注册表与 DeleteRegistry](https://github.com/microsoft/onnxruntime/blob/v1.26.0/onnxruntime/core/providers/cuda/cuda_execution_provider.cc#L3051)、
[CUDA Shutdown](https://github.com/microsoft/onnxruntime/blob/v1.26.0/onnxruntime/core/providers/cuda/cuda_provider_factory.cc#L292)、
[ProviderLibrary::Unload](https://github.com/microsoft/onnxruntime/blob/v1.26.0/onnxruntime/core/session/provider_bridge_ort.cc#L1979)。
Linux CUDA provider 禁用实际 `dlclose` 也不能避开前面的 `Shutdown()`。

通用的 Env 共享与生命周期契约见 [推理主题库](../notes/inference.md#ortenv-共享与退出顺序)。

## 改动范围

- `SessionConfig` 和 `DetectorConfig` 末尾新增可选的 `Ort::Env* env`，表达借用关系。
- 引擎仅在没有显式 Env 时调用旧 `GlobalEnv()`；检测器把指针继续传给引擎。
- quant benchmark 的纯推理和完整流水线共用入口局部 Env。quant eval harness 的
  CPU/CUDA 测试也显式传 Env，避免前序 CPU 用例偷偷初始化旧 static Env。
- 新增独立进程测试 `Quant_RuntimeLifetimeTest`。默认模式执行真实 CPU Run、IOBinding、
  detector 与 harness；检查输出元素数、有限值和检测框数。`--cuda-registry` 模式只
  创建/销毁引擎与检测器，再初始化 CUDA 注册表，专门用于 Valgrind 退出回归。

新配置默认 `nullptr`，保留旧调用方式。**未迁移的 W14/W16 历史入口仍可能走同一
static Env 风险路径**；本次修复覆盖 quant 入口，不代表全仓库完成迁移。继续使用
EvalHarness 的新调用方也应自行持有并传递 Env。

## 修复前后对照

先添加配置字段和回归程序，但保持引擎/检测器旧选择逻辑，得到 red；再落实指针
透传，重跑同一 construction-only 命令得到 green。均为 RelWithDebInfo 构建。
red 中 `inference_engine.cpp` 与 `yolo_detector.cpp` 的选择逻辑与 `ed97a30` 一致，
前者仍调用 `GlobalEnv()`，后者未传 `cfg.env`；其他测试结构与 green 相同。

| 同一 Valgrind 回归 | 修复前 red | 修复后 green |
|---|---:|---:|
| 进程退出码 | 0 | 0 |
| XML 中全部错误上下文 | 17 | 15 |
| 已释放堆块且栈含 `Ort::Env::~Env` 的上下文 | **2** | **0** |

两边都正常退出，说明只看退出码会漏掉这个问题。剩余 15 个上下文为 WSL `libcuda`
映射地址读取报告，地址被标为不属于 stack/malloc/free；与目标已释放控制块报告分开
保留。本轮没有消除这些驱动报告，也没有证明它们全部属于误报。未启用泄漏检查。

最初两次让 Valgrind 模拟完整神经网络推理的尝试过慢，手动终止，日志仍保留；
它们不是有效的 red 或 green。有效对照仅使用 `lifetime-*-construction.*`。

实际基准和回归结果见本报告末尾的验证记录。

## 复现命令与读法

准备 ORT GPU 包及 W16 模型/测试图后，在仓库根目录运行：

```bash
cmake -S . -B build-quant-lifetime -G Ninja -DCMAKE_CXX_COMPILER=g++-15 \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DONNXRUNTIME_ROOT="$PWD/third_party/onnxruntime/onnxruntime-linux-x64-gpu-1.26.0" \
  -DQUANT_MODELS_DIR="$PWD/build/02_Inference_Analysis/quantization/models"
cmake --build build-quant-lifetime --target quant_runtime_lifetime_test -j2

# 普通模式：真实推理与框数检查；缺模型/图片返回 77，由 CTest 标为跳过。
ctest --test-dir build-quant-lifetime -R '^Quant_RuntimeLifetimeTest$' --output-on-failure

# 诊断模式：专门检查进程退出，不执行神经网络推理。
valgrind --tool=memcheck --xml=yes --xml-file=lifetime.xml \
  --leak-check=no --show-leak-kinds=none --undef-value-errors=no \
  --num-callers=30 --keep-debuginfo=yes \
  ./build-quant-lifetime/02_Inference_Analysis/quantization/quant_runtime_lifetime_test \
  --cuda-registry
```

XML 中先看 `kind` 和 `what`，再看访问栈、释放栈和分配栈。目标报告应同时具备
“已释放块”与 Env 析构调用链。可用以下脚本辅助计数，但仍需检查完整栈：

```python
import xml.etree.ElementTree as ET

errors = ET.parse("lifetime.xml").findall("error")
target = [e for e in errors
          if "free'd" in "".join(e.itertext())
          and "Ort::Env::~Env" in "".join(e.itertext())]
print("全部上下文:", len(errors), "Env 退出 UAF:", len(target))
```

此过滤依赖 RelWithDebInfo 的函数符号，不适用于剥离符号的 Release 包。不能把没有
CUDA、缺模型而跳过，或缺符号而匹配不到，当作修复通过。先确认旧逻辑的阳性对照。

GDB/Valgrind 与匹配 glibc 调试符号的安装方法见
[README](../../README.md#可选退出内存错误诊断工具)。ASAN 的 CUDA 兼容设置和覆盖边界见
[调试章节](../../README.md#调试专用asan--tsan)。

## 证据位置

本机完整记录：`../quant-profiling-artifacts/20260911-heap-debug/`，位于仓库外，
避免构建清理时丢失。关键文件：

- `original-artifacts.json`、`original-abort.log`：原始输入/二进制哈希与崩溃日志。
- `valgrind-cuda-create.log`、`valgrind-registry-{static,local}.log`：缩小复现的对照。
- `lifetime-{red,green}-construction.xml` 及对应 command/result JSON、日志：有效回归对照。
- `fixed-benchmark-command.json`、`fixed-benchmark-results.json` 和三份 benchmark 日志：
  修复后二进制哈希、实际命令、退出结果及完整表格。
- `fixed-ctest-gpu.log`、ASAN 验证日志：测试详情。

## 验证记录

- 无 profiler 完整基准：固定原模型/图片，warmup=20、iters=200，3/3 正常退出。
  总运行时间为 105.07、104.96、109.18 秒；每轮 8 个 CUDA 行均实际启用 CUDA，
  没有 fallback；每轮全部 8 个流水线行均检测出 5 框。此处验证退出和输出，
  不是新的加速对比实验（第三轮期间还进行了 ASAN 构建）。
- ORT GPU 包：10/10 CTest 目标通过，含 38 个通过的 GTest、2 个跳过的 CPU fallback
  专用 GTest、4 个通过的 Python 参数测试，以及新增独立生命周期测试。
- ORT CPU 包：10/10 CTest 目标通过，39 个 GTest 通过、1 个真实 CUDA 用例按环境跳过，
  4 个 Python 参数测试及新增独立生命周期测试通过；CPU fallback 断言实际执行。
- 修复后的 ASAN 生命周期程序：默认真实 CPU 推理模式与 CUDA 注册表模式均退出 0，
  无 ASAN 报告。使用 `detect_leaks=0:halt_on_error=1:abort_on_error=1:protect_shadow_gap=0`，
  不覆盖泄漏检查或未插桩的第三方库内部访问；目标 UAF 的主要证据仍是 Valgrind。
- 未重新量化、重跑 mAP 或重做 Nsight 性能采集；独立代码审查未发现阻塞问题。
