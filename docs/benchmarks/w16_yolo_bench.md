# W16 YOLOv8n 推理基准（工程快测）

> **定位**：本周「机制实现 + 工程快测数字」，证明动态 batch / IOBinding / 线程配置可用且有收益。
> 专业瓶颈归因（哪段是 compute-bound / memory-bound、各阶段毫秒占比）留 **W18** 用 Nsight/Roofline 做，两者不重复。
>
> 环境：RTX 3060 Laptop + i5-12500H，ONNX Runtime 1.26.0（CPU 包 / GPU 包），CUDA 12.3 + cuDNN 9，WSL2。
> 方法：warmup 10 次（丢弃 cuDNN autotuning 冷启动）+ 计时 50 次取 P50/P99；输入为 `bus.jpg` 方形 640 letterbox 平铺 batch 份。
> 复现：`./w16_yolo_benchmark`（CPU）/ `build-gpu` + CUDA 库路径（GPU）。

## batch × EP × {Run, IOBinding}

| EP | batch | 模式 | P50(ms) | P99(ms) | 吞吐(img/s) |
|---|---|---|---|---|---|
| CPU | 1 | Run | 44.09 | 49.65 | 22.7 |
| CPU | 1 | IOBinding | 43.94 | 47.41 | 22.8 |
| CPU | 4 | Run | 172.72 | 184.09 | 23.2 |
| CPU | 4 | IOBinding | 175.17 | 183.05 | 22.8 |
| CUDA | 1 | Run | 5.85 | 7.10 | 170.9 |
| CUDA | 1 | IOBinding | 5.60 | 9.23 | 178.5 |
| CUDA | 4 | Run | 15.47 | 17.34 | 258.5 |
| CUDA | 4 | IOBinding | 16.01 | 17.46 | 249.9 |

## IntraOp 线程扫描（CPU, batch=1, Run）

| intra_op_threads | P50(ms) | 吞吐(img/s) |
|---|---|---|
| 1 | 101.63 | 9.8 |
| 2 | 63.78 | 15.7 |
| 4 | 51.15 | 19.5 |

## 结论

1. **CUDA EP 是单帧延迟的决定性因素**：batch=1 从 CPU 44ms → CUDA 5.85ms，约 **7.5×**。边缘实时检测（单路流）必须上 GPU EP。
2. **IOBinding 的收益集中在 CUDA batch=1**：5.85→5.60ms（吞吐 +4~9%）——此时 Device→Host 拷贝与每次 Run 的输出分配在总时间里占比最大，绑定复用省掉这部分抖动；batch=4 时计算占比上升，收益被淹没（且在噪声内）。CPU EP 无 H2D 拷贝，IOBinding 基本无差。
3. **batch 提吞吐不提延迟**：CUDA batch=1→4，单帧延迟 5.85→15.47ms（变慢），但吞吐 171→258 img/s（+51%）。**单路实时选 batch=1（保延迟），离线批处理选大 batch（保吞吐）**——这正是 W16 把 batch 瘦身到 1 vs 4 的理由：证明动态 shape 机制会用即可。
4. **IntraOp 次线性扩展**：1→2 线程提速 1.6×，1→4 提速 2.0×——单算子内并行受算子并行度与内存带宽限制，加到核数不会线性加速。InterOp 对 YOLOv8 这种近串行图无收益，故未铺。

> 实战出处：`02_Inference_Analysis/w16_yolo_detector/yolo_benchmark.cpp`；线程/IOBinding 机制见 `w14_ort_basics/inference_engine.{hpp,cpp}`（W16 加性扩展）。概念见 `docs/notes/inference.md`。
