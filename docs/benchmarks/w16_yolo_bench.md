# W16 YOLOv8n 推理基准（工程快测）

> **定位**：本周「机制实现 + 工程快测数字」，证明动态 batch / IOBinding / 线程配置可用且有收益。
> 专业瓶颈归因（哪段是 compute-bound / memory-bound、各阶段毫秒占比）留 **W18** 用 Nsight/Roofline 做，两者不重复。
>
> 环境：RTX 3060 Laptop + i5-12500H，ONNX Runtime 1.26.0（CPU 包 / GPU 包），CUDA 12.3 + cuDNN 9，WSL2。
> 方法：warmup 10 次（丢弃 cuDNN autotuning 冷启动）+ 计时 50 次取 P50/P99；输入为 `bus.jpg` 方形 640 letterbox 平铺 batch 份。
> 复现：`./w16_yolo_benchmark`（CPU）/ `build-gpu` + CUDA 库路径（GPU）。

## 纯推理 batch × EP × {Run, IOBinding}

> ⚠️ **此表只测 `engine.Run`（纯推理），不含 preprocess/postprocess**——「吞吐」是推理上限，
> **不是真实 FPS**（真实 FPS 见下面「端到端三段」表，约为此处的 55%）。

| EP | batch | 模式 | P50(ms) | P99(ms) | 吞吐(img/s) |
|---|---|---|---|---|---|
| CPU | 1 | Run | 44.06 | 47.66 | 22.7 |
| CPU | 1 | IOBinding | 44.74 | 48.90 | 22.4 |
| CPU | 4 | Run | 170.76 | 179.28 | 23.4 |
| CPU | 4 | IOBinding | 173.96 | 189.77 | 23.0 |
| CUDA | 1 | Run | 5.93 | 8.34 | 168.6 |
| CUDA | 1 | IOBinding | 5.49 | 10.92 | 182.0 |
| CUDA | 4 | Run | 16.00 | 17.77 | 250.1 |
| CUDA | 4 | IOBinding | 15.98 | 17.98 | 250.4 |

## 端到端三段（batch=1，含前后处理 = 真实 FPS）

| EP | preprocess(ms) | infer(ms) | postprocess(ms) | total P50(ms) | FPS |
|---|---|---|---|---|---|
| CPU | 2.25 | 42.75 | 0.76 | 45.83 | 21.8 |
| CUDA | 1.75 | 8.26 | 0.59 | 10.59 | **94.4** |

> 注：端到端表里的 `infer`（CUDA 8.26ms）高于纯推理表（5.5ms）——因为真实流水线在每次 Run 之间
> 夹了 CPU 的 cvtColor/letterbox/decode/NMS，GPU 无法背靠背提交、拿不到驱动层的连续 Run 流水化收益。
> **这正是"孤立 micro-benchmark 会低估真实延迟"的实证**，也是 W18 要用 Nsight 看 timeline 的原因。

## IntraOp 线程扫描（CPU, batch=1, Run）

| intra_op_threads | P50(ms) | 吞吐(img/s) |
|---|---|---|
| 1 | 101.63 | 9.8 |
| 2 | 63.78 | 15.7 |
| 4 | 51.15 | 19.5 |

## 结论

1. **真实 FPS 看端到端，不看纯推理**：CUDA 端到端单帧 10.59ms（**94 FPS**），preprocess+postprocess 占 ~22%（2.34/10.59ms）。纯推理表的「168 img/s」是上限、不是 FPS，差 ~44%。边缘实时部署务必按端到端报数。
2. **CUDA EP 是单帧延迟的决定性因素**：纯推理 batch=1 从 CPU 44ms → CUDA 5.9ms 约 7×；端到端 CPU 45.8ms(22 FPS) → CUDA 10.6ms(94 FPS) 约 4.3×。单路实时检测必须上 GPU EP。
3. **IOBinding 收益是噪声级、不稳定**：CUDA batch=1 多次 run 在 5.5~6.1ms 间，IOBinding 与 Run 的 P50 优劣**会翻转**（本次 5.49 vs 5.93 略优，上一轮反而略慢），P99 同理。结论：对 yolov8n 这种小模型、且仍需把输出拷回 CPU 解码的场景，IOBinding 的固定开销节省进了噪声——**别把它当稳定优化项**，真要量化收益得上更大模型或用 W18 Nsight 逐段归因。CPU EP 无 H2D 拷贝，更无差异。
4. **batch 提吞吐不提延迟**：CUDA batch=1→4，单帧延迟 5.9→16ms（变慢），但吞吐 169→250 img/s（+48%）。**单路实时选 batch=1（保延迟），离线批处理选大 batch（保吞吐）**——这正是 W16 把 batch 瘦身到 1 vs 4 的理由：证明动态 shape 机制会用即可。
5. **IntraOp 次线性扩展**：1→2 线程提速 ~1.6×，1→4 提速 ~2.1×——单算子内并行受算子并行度与内存带宽限制，加到核数不会线性加速。InterOp 对 YOLOv8 这种近串行图无收益，故未铺。

> 实战出处：`02_Inference_Analysis/w16_yolo_detector/yolo_benchmark.cpp`；线程/IOBinding 机制见 `w14_ort_basics/inference_engine.{hpp,cpp}`（W16 加性扩展）。概念见 `docs/notes/inference.md`。
