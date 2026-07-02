# Systems Performance 概念详解

> 可复用性能分析概念的主题正文。模块专属 benchmark 数字放各模块 notes 或 `docs/benchmarks/`。

## 目录

- [P50 / P99 延迟](#p50--p99-延迟)
- [Micro-benchmark vs End-to-end](#micro-benchmark-vs-end-to-end)
- [Profiling 报告方法](#profiling-报告方法)
- [fallback / Debug / skip 的结论边界](#fallback--debug--skip-的结论边界)

---

## P50 / P99 延迟

**是什么**：P50 是中位数延迟，代表典型体验；P99 是 99 分位延迟，代表尾延迟。采样排序后取分位点，比单次耗时或平均值更适合描述推理服务。

**为什么 / 何时用**：边缘推理既要看“通常多快”，也要看“偶发最慢会不会卡帧”。P50 能过滤少量抖动，P99 能暴露内存分配、调度、冷启动、IOBinding buffer 抖动等尾部问题。`quant::RollingStats` 用固定窗口保存最近 N 帧，避免长期运行无限增长。

**坑**：样本太少时 P99 很不稳定，不能过度解读；Debug/ASAN 下的 P50/P99 也不能代表部署性能。报告必须写 warmup、iters、构建类型、输入、模型、EP 状态。

> 实战出处：`02_Inference_Analysis/quantization/rolling_stats.{hpp,cpp}`；`docs/benchmarks/quant_yolo_hardening.md`

## Micro-benchmark vs End-to-end

**是什么**：micro-benchmark 只测某一段（如纯 ORT infer）；end-to-end 测用户实际路径（preprocess + infer + postprocess + 可能的 IO）。两者回答的问题不同。

**为什么 / 何时用**：纯 infer 能看 EP、线程数、IOBinding 是否影响模型执行；端到端才能给真实 FPS。W16/quant 的 YOLO 路径里，pre/post 虽然比 infer 小，但足以影响真实吞吐，不能用纯 infer FPS 代替端到端 FPS。

**坑**：只报最快的 micro 数字会高估系统性能；只报端到端又难定位瓶颈。正确做法是两张表并列：纯 infer 表看后端，端到端表看交付体验。

> 实战出处：`02_Inference_Analysis/quantization/quant_benchmark.cpp`

## Profiling 报告方法

**是什么**：profiling 报告要写清“环境 + 输入 + 模型 + 方法 + 数字 + 结论边界”。先用分段计时定位大头，再用 perf/Nsight 等工具进一步归因到函数、算子或 GPU kernel。

**为什么 / 何时用**：没有环境和方法的数字不可复现；没有结论边界的数字容易被误读。比如 CPU ORT 包环境请求 CUDA 会回退 CPU，此时不能写 GPU profiling 结论。

**坑**：工具缺失时要直说，不能补脑。若没有 perf/Nsight，就只能说“C++ harness 分段显示 infer 占主导”，不能说“瓶颈是某个 kernel”。若没有 allocator 计数，就不能把 `reserve_hint` 写成已证明减少 realloc。

> 实战出处：`docs/benchmarks/quant_yolo_hardening.md`

## fallback / Debug / skip 的结论边界

**是什么**：fallback 是请求的后端没有实际生效；Debug 是非部署构建；skip 是测试因资产或环境缺失未执行。这些状态都不是“失败”，但会限制结论范围。

**为什么 / 何时用**：工程报告要能被复现和审计。`ActiveEp()==CPU` 且 `EpFallbackReason()` 非空时，所有 CUDA 请求行都只能当 CPU 结果；GTest 的 `GTEST_SKIP()` 表示测试未覆盖该环境；Debug/ASAN 数字只能看正确性或相对趋势，不能看绝对性能。

**坑**：不要把 skip 当 pass，不要把 fallback 当 CUDA，不要把 Debug 数字写进简历。报告中要把这些状态显式列出来。

> 实战出处：`w14::InferenceEngine::EpFallbackReason()`；`Quant_BatchConsistencyTest` 资产缺失 skip 策略；`docs/benchmarks/quant_int8_report.md`
