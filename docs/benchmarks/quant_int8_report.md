# Quant YOLO FP32 vs INT8 PTQ Report

> 日期：2026-07-02
> 量化命令：`python 02_Inference_Analysis/quantization/tools/quantize_yolov8_static.py --model <fp32> --calib-dir datasets/coco128/images/train2017`
> benchmark 命令：`./build/02_Inference_Analysis/quantization/quant_benchmark <image> <fp32> <int8.minmax> <int8.entropy>`

## 环境与输入

- 构建：CMake `Release`，GCC 15，Ninja。
- ORT C++：`third_party/onnxruntime/onnxruntime-linux-x64-1.26.0` CPU 包。
- ORT Python：项目 `.venv`，`onnxruntime` quantization API。
- FP32 模型：`02_Inference_Analysis/w16_yolo_detector/models/yolov8n.onnx`。
- INT8 模型：`build/02_Inference_Analysis/quantization/models/yolov8n.int8.{minmax,entropy}.onnx`。
- 校准数据：coco128 子集（`--calib-limit 32`，32 张 `train2017`），替代早期单张 `test_image.jpg`。注意 INT8 0 框根因是检测头整图量化导致 per-tensor scale≈2.5 把分数坍缩，**与校准集大小无关**（详见 `notes.md`）；真正的修复是下面的检测头保 FP32，扩大校准集只是顺带改善激活范围估计。
- 量化格式：static PTQ，QDQ，activation `QUInt8`，weight `QInt8`，per-channel weight，MinMax / Entropy 两种校准策略。
- **检测头保 FP32**：节点名含 `/model.22/` 的 YOLOv8 检测头节点排除量化（脚本 `--exclude-pattern` 默认值）。这是 INT8 0 检测框根因修复的核心（commit `37abca5`），代价是模型变大、量化加速收益缩水。

CUDA EP 在本环境回退 CPU：CPU ORT 包缺少 `libonnxruntime_providers_cuda.so`。下表的 CUDA 请求行实际仍是 CPU 路径，不用于 GPU 结论。

> 测量口径：WSL2 无隔离环境，P50 逐次运行波动约 ±2ms。下表取代表性单次运行数字（3 次运行中位一次），首次冷启动运行已丢弃。

## 模型体积

| 模型 | 体积 |
|---|---:|
| FP32 | 13M |
| INT8 MinMax | 6.2M |
| INT8 Entropy | 6.2M |

体积约缩小到 FP32 的 48%。未达全量化理论的 ~25%，因为检测头 `/model.22/` 保留 FP32 未量化。

## 纯 ORT Infer（CPU 实际路径）

| 模型 | 模式 | P50(ms) | P99(ms) | FPS |
|---|---|---:|---:|---:|
| FP32 | Run | 40.81 | 43.71 | 24.5 |
| FP32 | IOBinding | 41.31 | 42.88 | 24.2 |
| INT8 MinMax | Run | 31.31 | 33.72 | 31.9 |
| INT8 MinMax | IOBinding | 31.89 | 34.39 | 31.4 |
| INT8 Entropy | Run | 30.82 | 34.21 | 32.5 |
| INT8 Entropy | IOBinding | 31.52 | 36.13 | 31.7 |

INT8 纯推理 P50 约为 FP32 的 76%，CPU 上快约 24%。注意这**不是**全量化模型的翻倍加速——检测头保 FP32 牺牲了大部分量化收益，换取检测输出的正确性。

Run vs IOBinding 差异全部落在 ±2% 噪声区间：当前 `use_iobinding=true` 只切换 ORT 调用路径，未实现真正的输入输出双绑 + buffer 池，故无稳定收益（见后续项）。

## 端到端 Hardened Pipeline（CPU 实际路径）

| 模型 | total P50(ms) | total P99(ms) | FPS | dets | top score |
|---|---:|---:|---:|---:|---:|
| FP32 | 44.87 | 48.00 | 22.3 | 5 | 0.8902 |
| INT8 MinMax | 34.60 | 37.55 | 28.9 | 5 | 0.8884 |
| INT8 Entropy | 34.59 | 37.37 | 28.9 | 5 | 0.8884 |

端到端延迟下降约 22%；两个 INT8 模型均输出 **5 个检测框，与 FP32 一致**，top score 0.8884 vs 0.8902（仅降约 0.002）。

## 精度口径

本报告只做单图检测一致性检查，不是 mAP：

- FP32 baseline：5 个检测框，top score 0.8902。
- INT8 MinMax / Entropy：5 个检测框，top score 0.8884——框数与 FP32 一致、top score 仅降约 0.002，单图检测一致性守住。
- 修复路径：检测头 `/model.22/` 排除量化保 FP32 + coco128 校准（commit `37abca5`），`Quant_Int8ConsistencyTest` 作为框级对齐守护，防回归。
- 单图/单例一致性**不等于** COCO mAP：没有 mAP 评估前，不能声称 INT8 精度整体可用。

结论范围：量化工具链、模型体积下降、CPU 延迟下降、单图检测一致性已经跑通；mAP 掉点评估是当前最大缺口。

## 后续项

- 用 COCO subset 做近似 mAP 评估，量化单图一致性到掉点数字（`tools/eval_map_coco128.py` 已在推进）。
- 检测头保 FP32 是「保精度、牺牲加速」的权衡：后续可做逐节点敏感度分析，尝试量化部分检测头节点以恢复更多加速。
- 在 GPU ORT 包环境重跑 CPU/CUDA EP 差异（当前 CUDA 请求实际回退 CPU）。
- 实现真正的 IOBinding 双绑 + buffer 池，再复测 Run vs IOBinding 收益。
- 后续 TRT INT8 calibrator 需要与 ORT PTQ 结果对照，解释 QDQ 与 TensorRT calibrator 的差异。
