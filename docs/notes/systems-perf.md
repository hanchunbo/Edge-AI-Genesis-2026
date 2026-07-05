# Systems Performance 概念详解

> 可复用性能分析概念的主题正文。模块专属 benchmark 数字放各模块 notes 或 `docs/benchmarks/`。

## 目录

- [P50 / P99 延迟](#p50--p99-延迟)
- [warmup / iters](#warmup--iters)
- [吞吐 img/s vs FPS](#吞吐-imgs-vs-fps)
- [Micro-benchmark vs End-to-end](#micro-benchmark-vs-end-to-end)
- [部署硬化 vs 后端优化](#部署硬化-vs-后端优化)
- [Benchmark / Eval Harness](#benchmark--eval-harness)
- [Profiling 报告方法](#profiling-报告方法)
- [fallback / Debug / skip 的结论边界](#fallback--debug--skip-的结论边界)

---

## P50 / P99 延迟

**是什么**：P50 是中位数延迟，代表典型体验；P99 是 99 分位延迟，代表尾延迟。采样排序后取分位点，比单次耗时或平均值更适合描述推理服务。

**为什么 / 何时用**：边缘推理既要看“通常多快”，也要看“偶发最慢会不会卡帧”。P50 能过滤少量抖动，P99 能暴露内存分配、调度、冷启动、IOBinding buffer 抖动等尾部问题。`quant::RollingStats` 用固定窗口保存最近 N 帧，避免长期运行无限增长。

**坑**：样本太少时 P99 很不稳定，不能过度解读；Debug/ASAN 下的 P50/P99 也不能代表部署性能。报告必须写 warmup、iters、构建类型、输入、模型、EP 状态。

> 实战出处：`02_Inference_Analysis/quantization/rolling_stats.{hpp,cpp}`；`docs/benchmarks/quant_yolo_hardening.md`

## warmup / iters

**是什么**：`warmup` 是预热轮数，先跑但不计入统计；`iters` 是 iterations 的缩写，指正式计时迭代次数。benchmark 常见流程是「预热 N 次 → 正式计时 M 次 → 对 M 个样本算 P50/P99/吞吐」。

**为什么 / 何时用**：第一次推理经常混入一次性开销：CUDA/cuDNN kernel 准备、autotuning、内存分配、CPU cache 未命中等。把 warmup 排除掉，统计值才更接近稳定运行状态。iters 决定样本量，太少会让 P99 和吞吐抖动大。

**坑**：warmup 不是作弊删慢样本，而是把冷启动和稳态吞吐分开看；如果业务关心首帧延迟，要单独报告 cold start。`iters=50` 只能算工程快测，不能替代长时间压测。

> 实战出处：`02_Inference_Analysis/w16_yolo_detector/yolo_benchmark.cpp`（`kWarmup=10`、`kIters=50`）

## 吞吐 img/s vs FPS

**是什么**：吞吐 `img/s` 是单位时间处理的图片总量，常按 `batch * 1000 / P50(ms)` 估算；FPS（frames per second）是端到端每秒完成多少帧，通常用于实时视频/摄像头体验。

**为什么 / 何时用**：二者单位都像「每秒多少张」，但统计范围和业务语义不同。W16 纯推理表的 `img/s` 只覆盖 ORT infer，回答「模型后端每秒能算多少张」；端到端 FPS 覆盖 preprocess + infer + postprocess，回答「真实检测链路每秒能出多少帧结果」。

**坑**：batch 会把吞吐和单帧等待时间拉开。batch=4 可能 `img/s` 更高，因为 18ms 出 4 张，平均每张更便宜；但第一张也要等整批 18ms 才出结果。单路实时摄像头优先看端到端延迟/FPS，离线批处理才优先看吞吐。

> 实战出处：`docs/benchmarks/w16_yolo_bench.md`（batch 1v4 + 端到端三段表）

## Micro-benchmark vs End-to-end

**是什么**：micro-benchmark 只测某一段（如纯 ORT infer）；end-to-end 测用户实际路径（preprocess + infer + postprocess + 可能的 IO）。两者回答的问题不同。

**为什么 / 何时用**：纯 infer 能看 EP、线程数、IOBinding 是否影响模型执行；端到端才能给真实 FPS。W16/quant 的 YOLO 路径里，pre/post 虽然比 infer 小，但足以影响真实吞吐，不能用纯 infer FPS 代替端到端 FPS。

**坑**：只报最快的 micro 数字会高估系统性能；只报端到端又难定位瓶颈。正确做法是两张表并列：纯 infer 表看后端，端到端表看交付体验。

> 实战出处：`02_Inference_Analysis/quantization/quant_benchmark.cpp`

## 部署硬化 vs 后端优化

**是什么**：部署硬化是让推理链路更稳定、可观测、可回滚、可解释；后端优化是换或改执行后端来提高算子/图执行效率。前者解决「上线能不能稳」，后者解决「算得能不能更快」。

**为什么 / 何时用**：两者经常都出现在部署工程里，但边界不同。quant 阶段的硬化包括 NaN/Inf 防护、`max_det` 上限、`reserve`、CUDA fallback reason、`DetectWithProfile()` 分段计时、P50/P99、batch consistency；TensorRT、CUDA kernel、GPU 端前后处理融图、FP16/INT8 engine 则属于后端优化，放到 `trt` 交付物更合理。

**坑**：别把每个性能相关改动都叫后端优化。`reserve` 或 IOBinding 复用可能影响延迟尾部，但它们主要是部署路径的稳定性和可测性改造；真正的后端优化要证明 EP/kernel/graph placement 发生了变化，并用 profiling 或 benchmark 支撑。

> 实战出处：`docs/Roadmap.md`（`quant` 与 `trt` 范围切分）；`02_Inference_Analysis/quantization/notes.md`（quant hardening）

## Benchmark / Eval Harness

**是什么**：harness 是把被测对象固定到同一套流程里运行的评估框架。它不是模型、不是量化算法、也不是推理引擎本身，而是负责「加载多个 case → warmup → 正式迭代 → 收集指标 → 输出结果」的测量台。

**为什么 / 何时用**：没有 harness，FP32、INT8 MinMax、INT8 Entropy 很容易在不同图片、不同 warmup、不同线程/EP 状态下被拿来比较，结论不可信。项目里的 `quant::EvalHarness` 输入 `ModelCase{name, model_path}` 和 `EvalConfig{detector,warmup,iters,stats_window}`，对每个模型复用 W16 检测流水线，记录 `active_ep`、fallback reason、检测框数、top score、pre/infer/post/total 分段延迟。

**坑**：harness 只能保证评估流程一致，不自动保证结论完整。单图 harness 只能做框级冒烟/一致性，不能替代 mAP；CPU ORT 包导致 CUDA fallback 时，harness 记录的是 CPU 结果；warmup 必须从统计里排除，否则冷启动、cache、CUDA autotuning 会污染分位数。

> 实战出处：`02_Inference_Analysis/quantization/eval_harness.{hpp,cpp}`；`02_Inference_Analysis/quantization/quant_benchmark.cpp`

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
