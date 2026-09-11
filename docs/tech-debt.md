# 待处理的工程问题

## GPU 基准退出时偶发堆损坏（2026-09-10）

- **状态**：待定位；长期运行或发布前处理。
- **触发记录**：运行 quant FP32/INT8 × CPU/CUDA × Run/IOBinding 的完整基准，一次尝试在输出全部表格后 SIGABRT，glibc 报 `corrupted double-linked list`。
- **已知边界**：6 次无 profiler 尝试中 5 次正常退出、1 次异常；随后 Nsight 全量采集与独立 ORT probe 正常退出。不能用这些成功覆盖失败，也未确定是项目代码还是 ORT/cuDNN 等依赖的问题。
- **原始证据**：`build/quant-closeout-evidence/20260910-nsys/benchmark-3-aborted.log`，随 [profiling 报告](benchmarks/quant_profiling_report.md) 的证据包保存。
- **下一步**：在相同模型哈希与依赖版本下缩小到 session 创建/销毁、Run/IOBinding 的生命周期复现，获取退出栈或 ASAN 证据，再决定修复位置；不要直接更换驱动或用提前退出隐藏析构错误。

## 完整 ORT CI

现有 CI 仍用 GCC 13，未准备 ORT；仅扩展格式检查不等于建立推理回归。
后续固定 GCC 15/Ninja/ORT 环境，并分别验证无产物 skip 与有产物真实断言。
