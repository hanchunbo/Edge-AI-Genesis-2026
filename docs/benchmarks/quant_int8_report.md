# Quant YOLO FP32 vs INT8 PTQ Report

> 日期：2026-07-02
> 命令：`cmake --build build --target quant_benchmark quant_yolov8_static`

## 环境与输入

- 构建：CMake `Release`，GCC 15，Ninja。
- ORT C++：`third_party/onnxruntime/onnxruntime-linux-x64-1.26.0` CPU 包。
- ORT Python：项目 `.venv`，`onnxruntime` quantization API。
- FP32 模型：`02_Inference_Analysis/w16_yolo_detector/models/yolov8n.onnx`。
- INT8 模型：`build/02_Inference_Analysis/quantization/models/yolov8n.int8.{minmax,entropy}.onnx`。
- 校准数据：当前仅 1 张 `test_image.jpg`，用于验证工具链闭环，不代表正式校准集。
- 量化格式：static PTQ，QDQ，activation `QUInt8`，weight `QInt8`，per-channel weight，MinMax / Entropy 两种校准策略。

CUDA EP 在本环境回退 CPU：CPU ORT 包缺少 `libonnxruntime_providers_cuda.so`。下表的 CUDA 请求行实际仍是 CPU 路径，不用于 GPU 结论。

## 模型体积

| 模型 | 体积 |
|---|---:|
| FP32 | 13M |
| INT8 MinMax | 3.8M |
| INT8 Entropy | 3.8M |

体积约缩小到 FP32 的 29%。

## 纯 ORT Infer（CPU 实际路径）

| 模型 | 模式 | P50(ms) | P99(ms) | FPS |
|---|---|---:|---:|---:|
| FP32 | Run | 40.60 | 42.45 | 24.6 |
| FP32 | IOBinding | 40.99 | 43.95 | 24.4 |
| INT8 MinMax | Run | 17.85 | 22.61 | 56.0 |
| INT8 MinMax | IOBinding | 18.35 | 20.21 | 54.5 |
| INT8 Entropy | Run | 18.47 | 20.62 | 54.1 |
| INT8 Entropy | IOBinding | 18.09 | 20.01 | 55.3 |

INT8 纯推理 P50 约为 FP32 的 41%~46%，CPU 上延迟改善明显。

## 端到端 Hardened Pipeline（CPU 实际路径）

| 模型 | total P50(ms) | total P99(ms) | FPS | dets | top score |
|---|---:|---:|---:|---:|---:|
| FP32 | 48.08 | 50.97 | 20.8 | 5 | 0.8902 |
| INT8 MinMax | 21.23 | 22.88 | 47.1 | 0 | 0.0000 |
| INT8 Entropy | 20.42 | 23.16 | 49.0 | 0 | 0.0000 |

端到端延迟同样明显下降，但两个 INT8 模型在当前单图评估下均输出 0 个检测框。

## 精度口径

本报告第一版只做单图检测一致性检查，不是 mAP：

- FP32 baseline：5 个检测框，top score 0.8902。
- INT8 MinMax / Entropy：0 个检测框，说明当前 PTQ 配置或校准数据不足以保持 YOLO 检测输出。
- 因为没有 COCO subset 的 mAP 评估，不能声称 INT8 精度可用。

结论范围：量化工具链、模型体积下降、CPU 延迟下降已经跑通；精度侧当前是失败样本，需要扩大校准集与评估集后再判断。

## 后续项

- 用 COCO subset 或项目内小样本集做校准和近似 mAP / 检测一致性评估。
- 尝试更多校准图、固定输入 shape、排除检测头敏感节点或调整量化类型。
- 在 GPU ORT 包环境重跑 CPU/CUDA EP 差异。
- 后续 TRT INT8 calibrator 需要与 ORT PTQ 结果对照，解释 QDQ 与 TensorRT calibrator 的差异。
