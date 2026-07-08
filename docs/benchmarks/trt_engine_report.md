# trt 四路对比报告（M1 骨架：TRT FP16 已实测，INT8 归 M2）

- 日期：2026-07-07（M1）
- 机器：RTX 3060 Laptop（SM 8.6）/ WSL2 / TRT 10.16.1 / CUDA 12.3
- 口径：纯 infer 含 H2D/D2H 与同步（对齐 ORT Run）；warmup=5 iters=20 P50；
  CPU EP / CUDA EP 列引用 `quant_int8_report.md` 同机数字，**不重测**
- mAP 口径：coco128，mAP50-95（quant 评估链路）

## 四路总表

| 路线 | 体积 | 纯 infer P50(ms) | mAP50-95 | 来源 |
|---|---:|---:|---:|---|
| CPU EP FP32 | 13M（onnx） | 40.81 | 0.4454 | 引 quant |
| CPU EP INT8（QDQ MinMax） | 6.2M（onnx） | 31.31 | 0.4285 | 引 quant |
| CUDA EP FP32 | 13M（onnx） | 5.64 | ≈FP32（同权重） | 引 quant |
| CUDA EP INT8（QDQ MinMax） | 6.2M（onnx） | 11.49（负优化 ~2×） | ≈CPU INT8 | 引 quant |
| **TRT FP16** | 8.9M（plan） | **2.93** | M2 评 | 本报告 M1 |
| TRT INT8（隐式 Calibrator） | M2 | M2 | M2 | 待做 |
| TRT INT8（显式 QDQ） | M2 | M2 | M2 | 待做 |

## M1 结论

- TRT FP16 vs CUDA EP FP32（5.64ms）：**2.93ms，-48.0%（约 1.9×）**；P99 3.74ms，341 FPS。
  同模型 trtexec GPU Compute Time 均值 2.615ms（不含 H2D/D2H）互验，口径差合理。
- 一致性：TRT FP16 过 W16 ultralytics 参考对拍（5/5 框，IoU>0.9 + score±0.05 严格阈值未放宽）。
- yolov8n.onnx 为全动态维度导出，engine 以单 optimization profile（min=opt=max=1×3×640×640）锁死，等效静态。
- M2 待办：INT8 双路线（隐式 Calibrator / 显式 QDQ）+ mAP 闭环 + 「CUDA EP QDQ 慢 2× vs TRT
  显式量化」正面对比。
