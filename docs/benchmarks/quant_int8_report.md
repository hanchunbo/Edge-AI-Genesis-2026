# Quant YOLO FP32 vs INT8 PTQ Report

> 日期：2026-07-02
> 量化命令：`python 02_Inference_Analysis/quantization/tools/quantize_yolov8_static.py --model <fp32> --calib-dir datasets/coco128/images/train2017`
> benchmark 命令（CPU，`build`）：`./build/02_Inference_Analysis/quantization/quant_benchmark <image> <fp32> <int8.minmax> <int8.entropy>`
> benchmark 命令（GPU，`build-gpu`，`-DONNXRUNTIME_ROOT=...-gpu-1.26.0`）：`./build-gpu/02_Inference_Analysis/quantization/quant_benchmark <同上>`

## 环境与输入

- 构建：CMake `Release`，GCC 15，Ninja。
- ORT C++：CPU 数字用 `onnxruntime-linux-x64-1.26.0`（CPU 包，`build`）；GPU 数字用 `onnxruntime-linux-x64-gpu-1.26.0`（GPU 包，`build-gpu`，CUDA EP 实际激活）。
- ORT Python：项目 `.venv`，`onnxruntime` quantization API。
- FP32 模型：`02_Inference_Analysis/w16_yolo_detector/models/yolov8n.onnx`。
- INT8 模型：`build/02_Inference_Analysis/quantization/models/yolov8n.int8.{minmax,entropy}.onnx`。
- 校准数据：coco128 子集（`--calib-limit 32`，32 张 `train2017`），替代早期单张 `test_image.jpg`。注意 INT8 0 框根因是检测头整图量化导致 per-tensor scale≈2.5 把分数坍缩，**与校准集大小无关**（详见 `notes.md`）；真正的修复是下面的检测头保 FP32，扩大校准集只是顺带改善激活范围估计。
- 量化格式：static PTQ，QDQ，activation `QUInt8`，weight `QInt8`，per-channel weight，MinMax / Entropy 两种校准策略。
- **检测头保 FP32**：节点名含 `/model.22/` 的 YOLOv8 检测头节点排除量化（脚本 `--exclude-pattern` 默认值）。这是 INT8 0 检测框根因修复的核心（commit `37abca5`），代价是模型变大、量化加速收益缩水。

GPU 环境：本机 RTX 3060 Laptop（6GB）+ CUDA 12.3 + cuDNN 9（WSL2）。`build`（CPU 包）跑不了 CUDA EP（缺 `libonnxruntime_providers_cuda.so`）会回退 CPU，所以下面「CPU 实际路径」的表都用 `build`；GPU 数字一律用 `build-gpu`（GPU 包），其 CUDA 行实际 EP 均为 CUDA、0 回退（见 GPU 节）。

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

## GPU（CUDA EP，build-gpu）实测

| 模型 | CPU Run P50 | CUDA Run P50 | CUDA 端到端 total P50 | 端到端 FPS |
|---|---:|---:|---:|---:|
| FP32 | 40.81 | 5.64 | 11.01 | 90.9 |
| INT8 MinMax | 31.31 | 11.49 | 13.77 | 72.6 |
| INT8 Entropy | 30.82 | 11.12 | 16.31 | 61.3 |

- FP32 GPU 纯 infer 5.64ms（CPU 40.8ms，约 **6.7×**），端到端 91 FPS——与 W16 记录（5.9ms / 94 FPS）一致，CUDA EP 真激活、0 回退。
- **反直觉但关键：INT8 在 CUDA EP 上反而比 FP32 慢约 2×**（纯 infer 11ms vs 5.6ms，端到端同向）。根因：ORT 对 QDQ 图在 CUDA EP 上插入大量 Memcpy 节点（本次日志 45 个），叠加检测头保 FP32 造成的 INT8↔FP32 类型边界；CUDA EP 对 INT8 QDQ 的支持本就有限。
- 结论：**INT8 的 GPU 加速不能靠 CUDA EP，得靠 TensorRT INT8 EP**（下一交付物 `trt`）。CPU 上「INT8 更快」与 GPU 上「INT8 更慢」并存——量化收益强依赖 EP。
- IOBinding（CUDA）仍噪声级：FP32 Run/IOBinding 5.64/5.67，INT8 两模型优劣翻转（minmax IOBinding 快 10.8%、entropy 慢 11.5%），印证 W16「IOBinding 对 yolov8n 别当稳定优化项」。
- GPU 上检测一致：三模型均 5 框，INT8 top 0.8832 vs FP32 0.8902（CPU 侧 INT8 为 0.8884，CUDA/CPU 的 QDQ 数值路径略有差异，但框数与精度均守住）。

## 精度口径

两级验证：单图检测一致性（快速回归守护）+ coco128 mAP（量化掉点）。

**单图检测一致性**（`Quant_Int8ConsistencyTest` 守护，防回归）：

- FP32：5 框，top score 0.8902。
- INT8 MinMax / Entropy：5 框，top score 0.8884，框数与 FP32 一致、top score 仅降约 0.002。
- 修复路径：检测头 `/model.22/` 排除量化保 FP32 + coco128 校准（commit `37abca5`）。

**coco128 mAP**（128 图，ultralytics `model.val`，CPU；命令 `tools/eval_map_coco128.py`）：

| 模型 | mAP50 | mAP50-95 | 掉点(mAP50-95) |
|---|---:|---:|---:|
| FP32 | 0.6054 | 0.4454 | — |
| INT8 MinMax | 0.5833 | 0.4285 | −0.0170（−3.8%） |
| INT8 Entropy | 0.5833 | 0.4285 | −0.0170（−3.8%） |

- INT8 mAP50-95 仅掉约 1.7 个点（相对 3.8%），精度损失可接受——**推翻早期整图量化 0 框「精度失败」的结论**，检测头保 FP32 后 INT8 精度可用。
- MinMax 与 Entropy mAP 完全相同：检测头既保 FP32，backbone 两种激活校准策略对最终 mAP 的影响在 coco128 上无法区分；两者等价时选 MinMax 更省（校准不建直方图、不吃内存）。
- 口径边界：coco128 是训练集子集，mAP 绝对值偏乐观，此处只取 FP32 vs INT8 的**相对掉点**，不代表 COCO val 泛化精度。

结论范围：量化工具链、模型体积下降、CPU 延迟下降、单图一致性、coco128 mAP 掉点已全部跑通。

## 后续项

- coco128 是训练集子集，mAP 偏乐观；如需泛化结论可在 COCO val 子集上复测——评估本身是逐张推理、**不吃内存**（实测评估阶段内存平稳，与校准无关）。
- 若未来想进一步压掉点或扩校准集：Entropy 校准受内存墙限制（32 图峰值 RSS 5.24GB / 本机 WSL 仅 7.7GB），需换大内存机器或改用 MinMax（不建直方图、内存友好）。但当前 INT8 只掉 1.7 点已可用，扩校准集非必需。
- 检测头保 FP32 是「保精度、牺牲加速」的权衡：后续可做逐节点敏感度分析，尝试量化部分检测头节点以恢复更多加速。
- INT8 的 GPU 加速留给 `trt` 交付物：CUDA EP 上 INT8 QDQ 是负优化（见 GPU 节），需 TensorRT INT8 EP 才能在 GPU 上兑现量化收益——这正是 TRT INT8 calibrator 的动机。
- IOBinding 完整双绑不再投入：CPU（无 D2H 拷贝）与 GPU（W16 + 本次 quant 实测均噪声级）都证明现有 `RunIoBinding`（输出持久绑定 + 输入零拷贝）对 yolov8n 已够，完整 device buffer / pinned memory 双绑对这个小模型无稳定收益。
- 后续 TRT INT8 calibrator 需要与 ORT PTQ 结果对照，解释 QDQ 与 TensorRT calibrator 的差异。
