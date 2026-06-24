# W16 — YOLOv8n 检测 Demo（NMS + 坐标反算 + ORT 进阶）设计文档

> **补录说明**：本设计当时在 harness 的 plan mode 下完成（计划写在 `~/.claude/plans/`，
> plan mode 禁止改仓库其他文件），故未像 W15 那样落到 specs。此文档于 2026-06-23 W16 完成后
> 按实际实现 + 专家复盘补录，作为单一事实源。

## 1. 目标

把 W15 的「分类单输出」升级为「检测多框输出」：部署 YOLOv8n，手写检测头解析 + NMS +
letterbox 坐标反算，端到端对拍 ultralytics；并完成 ORT 进阶（线程配置 / IOBinding /
动态 batch benchmark）。是 Q2 第一阶段 P0 核心周（🔴，18h）。

## 2. 范围与路线决策（用户确认）

- **完整 Q2 全做**：不止最小 demo，含性能测量（W18 再用 Nsight/Roofline 做专业归因，定位不同）。
- **环境**：本地 RTX 3060，CUDA EP 可用；CPU/GPU 双 ORT 包（`-DONNXRUNTIME_ROOT` 切换）。
- **需求优化（覆盖原 Q2 措辞）**：坐标反算提为一等成功指标；对拍改显式定量容差；batch 瘦身 1 vs 4；
  benchmark 加 CUDA warmup；NMS<1ms@1000 框标注为 stress test；W16 导出 YOLO、W17 导出 ResNet。

## 3. 架构

### 3.1 组件（分两层，便于隔离测试）

- **`w16_detect_core`（纯 C++，不依赖 ORT/OpenCV）**：`nms`（IoU + 逐类）、`decode`
  （YOLOv8 头解析 + 坐标反算 + 边界 clamp）。最高 bug 风险点（NMS / 坐标反算）隔离单测。
- **`w16_yolo_lib`（依赖 W14 + OpenCV + w10）**：`YOLODetector` 编排 + benchmark。

### 3.2 关键数据结构

```cpp
struct Detection { float x1,y1,x2,y2; float score; int class_id; };  // detection.hpp
struct DetectorConfig { int input_size=640; float conf_thresh=0.25f;
                        float iou_thresh=0.45f; w14::Ep ep=kCuda; };
```

### 3.3 接口（实际）

```cpp
// 纯 C++ core
float IoU(const Detection&, const Detection&);
std::vector<Detection> Nms(std::vector<Detection> dets, float iou_thresh);  // 逐类
std::vector<Detection> DecodeYolov8(std::span<const float> out, int num_classes,
    int num_anchors, float conf, float scale, int pad_left, int pad_top,
    int img_w, int img_h);  // img_w/h 用于 clamp（专家复盘补）

// 编排（组合，非继承 W14）
class YOLODetector { std::vector<Detection> Detect(const cv::Mat& bgr); ... };
```

## 4. 数据流关键点 & 复用

`BGR → cvtColor RGB → w10::LetterboxToTensor(640,info) → InferenceEngine::Run
→ output[1,84,8400] → DecodeYolov8(+坐标反算+clamp) → Nms → Detection`

- **复用 Q1 W10**：`LetterboxToTensor` 自带 `/255`（无 mean/std，正合 YOLOv8）+ `LetterboxInfo`。
- **三个必处理的坑**：① 通道序——LetterboxToTensor 按输入序展平，必须先 BGR→RGB；
  ② 归一化只 `/255`，勿套 ImageNet mean/std；③ YOLOv8 输出**无 objectness**、channels-major 需转置。

## 5. 错误处理

- 模型/图片缺失、输出形状非 `[1,C,A]`、张量大小不符 → 抛 `runtime_error`/`invalid_argument`。
- CUDA 不可用 → W14 引擎层优雅回退 CPU（`ActiveEp()` 可查）。

## 6. 测试（GTest）

| 套件 | 覆盖 |
|---|---|
| `W16_NmsTest` (6) | IoU 手算对拍、重叠抑制、跨类不抑制、降序、1000 框 stress<1ms@Release |
| `W16_DecodeTest` (5) | 合成张量解码、conf 过滤、**坐标反算<1px**、**边界 clamp**、错误尺寸抛异常 |
| `W16_DetectorTest` (1) | 真图对拍 ultralytics（框数一致 + 每框 IoU>0.9 同类 + score<0.05），模型缺失 SKIP |

## 7. 新增资产

- `02_Inference_Analysis/w16_yolo_detector/`：core + lib + demo + benchmark + tools + notes。
- W14 加性扩展：`SessionConfig`（线程）+ `RunIoBinding`（持久 binding，形状固定契约）。
- `docs/benchmarks/w16_yolo_bench.md`；主题库 inference.md（YOLO头/NMS/IntraOp·InterOp/IOBinding）+
  image-ops.md（Letterbox 毕业）。

## 8. 验收标准

- `ctest -R W16_` 全绿；逐框 score 与 ultralytics 对齐 <0.001。
- RTX 3060：CUDA 端到端 ~94 FPS（纯推理 ~7×）；IntraOp 1→4 次线性。
- clang-format-21 clean；CPU/CUDA EP 均跑通。

## 9. 已知风险 / 注意点（专家复盘，已记 tech-debt）

- **[已修] benchmark 只测纯推理 → 加端到端三段表（真实 FPS 94 vs 纯推理上限 168）**。
- **[已修] decode 未 clamp 边界框 → 加 img_w/h + clamp + 单测**。
- [OPEN] W14 线程/IOBinding 旋钮未接进 `DetectorConfig`（造了没接）。
- [OPEN] 动态 batch 只验证"不崩"、未验证"批内每张都对"。
- [OPEN] YOLOv8 是单一合并输出，"多输出头"学习目标只达成一半。
- [OPEN] 对拍仅 bus.jpg 单图，鲁棒性证据薄。
- **IOBinding 收益为噪声级**——不当稳定优化项（详见 bench 文档）。

## 10. 工作量

实际约 18h：core+TDD ~4h、detector+对拍调试（rect=False 发现）~3h、ORT 进阶+benchmark ~4h、
文档+知识库+spec ~4h、专家复盘修复 ~3h。

## 11. 模块边界小结

`w16_detect_core` 纯算法可独立测；`YOLODetector` 组合 W14 引擎；W14 加性扩展默认参数保持
W15/W16 零改动。W17 将把 W14–W16 整合为 `inference_engine` 库。
