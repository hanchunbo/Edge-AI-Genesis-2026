# Quant YOLO 部署硬化 Profiling Baseline

> 日期：2026-07-02
> 命令：`./build/02_Inference_Analysis/quantization/quant_benchmark`

## 环境

- 构建：CMake 默认 `Release`，GCC 15，Ninja。
- ORT：`third_party/onnxruntime/onnxruntime-linux-x64-1.26.0` CPU 包。
- 模型：`02_Inference_Analysis/w16_yolo_detector/models/yolov8n.onnx`。
- 输入：`02_Inference_Analysis/w16_yolo_detector/models/test_image.jpg`。
- 采样：warmup=5，iters=20。
- CUDA：请求 CUDA EP 时回退 CPU，原因是 CPU ORT 包缺少 `libonnxruntime_providers_cuda.so`。

## 配置对照

| 配置 | use_iobinding | skip_non_finite | reserve_hint | max_det |
|---|---:|---:|---:|---:|
| W16 default | false | false | 0 | 0 |
| quant hardened | true | true | 512 | 300 |

## 纯 ORT Infer

| 模型 | 请求 EP | 实际 EP | 模式 | P50(ms) | P99(ms) | FPS |
|---|---|---|---|---:|---:|---:|
| fp32 | CPU | CPU | Run | 43.46 | 47.33 | 23.0 |
| fp32 | CPU | CPU | IOBinding | 43.87 | 45.88 | 22.8 |
| fp32 | CUDA | CPU | Run | 44.27 | 46.54 | 22.6 |
| fp32 | CUDA | CPU | IOBinding | 44.25 | 46.74 | 22.6 |

Run vs IOBinding：CPU 下 -0.9%，CUDA 请求但实际 CPU 下 +0.1%，均在 3% 噪声区间内。本环境不能证明 IOBinding 有稳定收益。

## 端到端 Baseline vs Hardened

| 配置 | 模型 | 请求 EP | 实际 EP | pre P50 | infer P50 | post P50 | total P50 | total P99 | FPS | dets | top score |
|---|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| W16 default | fp32 | CPU | CPU | 2.18 | 44.04 | 0.88 | 47.18 | 79.01 | 21.2 | 5 | 0.8902 |
| quant hardened | fp32 | CPU | CPU | 2.25 | 43.42 | 0.86 | 46.42 | 49.30 | 21.5 | 5 | 0.8902 |
| W16 default | fp32 | CUDA | CPU | 2.23 | 43.18 | 0.85 | 46.55 | 49.25 | 21.5 | 5 | 0.8902 |
| quant hardened | fp32 | CUDA | CPU | 2.41 | 43.90 | 0.89 | 47.48 | 49.98 | 21.1 | 5 | 0.8902 |

CPU 实际路径下，hardened 配置相对 W16 default 的 total P50 从 47.18ms 到 46.42ms，约 +1.6%；total P99 从 79.01ms 到 49.30ms。本轮样本较小，P99 改善只能作为 baseline 记录，不能归因到单个硬化项。

## 硬化项覆盖

- `skip_non_finite`：单测覆盖 3 个候选中含 NaN 坐标和 Inf score 的异常候选，开启后仅保留 1 个合法候选。
- `max_det`：单测覆盖 `max_det=0` 不限制、`max_det=2` 在全局 score 降序后截断。
- `reserve_hint`：结果等价测试已覆盖；当前未接自定义 allocator，暂未记录 realloc 次数。
- batch consistency：`Quant_BatchConsistencyTest` 覆盖 batch=4 每个输出切片与 batch=1 的框数、类别、score、xyxy 一致。

## Profiling 边界

本次没有运行 perf / Nsight。原因：当前是 CPU ORT 包环境，CUDA EP 回退 CPU；Nsight GPU 归因不成立。报告只记录 C++ harness 采集到的 `pre / infer / post / total` 分段和 ORT fallback 信息，不伪造 GPU profiling 结论。

后续差距：

- 输入输出双绑 + buffer 池尚未实现；当前 `use_iobinding=true` 只复用 W14 已有 IOBinding 路径，不能写成完整双绑。
- 需要在 GPU ORT 包环境重跑 CPU/CUDA 对比。
- 若要量化 allocator 层收益，需要新增分配计数或自定义 allocator 采样。
