# 待处理的工程问题

## quant 退出 UAF 已修复，W14/W16 入口已迁移，单元测试待处理（2026-09-11，2026-11-18 更新）

- **状态**：quant benchmark 与 quant eval 测试的显式 Env 路径已修复（2026-09-11）；W14/W16 的 demo / benchmark 入口已迁移，Valgrind 目标 UAF 2→0（2026-11-18，见 [生命周期报告「后续」节](benchmarks/quant_runtime_lifetime.md#后续w14w16-入口迁移2026-11-18)）。
- **触发记录**：运行 quant FP32/INT8 × CPU/CUDA × Run/IOBinding 的完整基准，一次尝试在输出全部表格后 SIGABRT，glibc 报 `corrupted double-linked list`。
- **定位**：Valgrind 在仅初始化 CUDA provider 时稳定发现退出 UAF。ORT 1.26.0 的 static Env 晚于 CUDA 注册表析构，Shutdown 再次 reset 注册表，访问并修改已释放的控制块。quant 已通过局部 Env 提前执行清理，修复前后目标上下文 2→0。
- **验证与边界**：三轮完整基准正常退出，10 个相关 CTest 目标通过。原来具体的 glibc SIGABRT 未再次出现；本次消除已复现的内存缺陷，不排除其他原因。Valgrind 剩余 WSL 驱动映射读报告单独保留，不能称全部内存检查零错误。详见 [生命周期报告](benchmarks/quant_runtime_lifetime.md)。
- **原始证据**：`build/quant-closeout-evidence/20260910-nsys/benchmark-3-aborted.log`，随 [profiling 报告](benchmarks/quant_profiling_report.md) 的证据包保存。
- **遗留**：W14/W16 单元测试（`w14_inference_engine_test`、`w16_yolo_detector_test`）仍走兼容的 `GlobalEnv()`，GPU 构建下同进程加载 CUDA provider 时可能触发同一顺序问题。W14 测试含 `GlobalEnv()` 单例用例，迁移需改用 gtest Environment 在 TearDown 中先释放 Env，并做同样的内存对照。新的调用方须传入由入口持有的 Env。

## 完整 ORT CI

现有 CI 仍用 GCC 13，未准备 ORT；仅扩展格式检查不等于建立推理回归。
后续固定 GCC 15/Ninja/ORT 环境，并分别验证无产物 skip 与有产物真实断言。

2026-11-18：`ubuntu-latest` 切到 26.04 后，系统 OpenCV 4.10 与 g++-13 链接失败
（`__cxa_call_terminate@CXXABI_1.3.15`），build-and-test 暂时固定 `ubuntu-24.04`。
升级到 GCC 15 时一并解除固定。
