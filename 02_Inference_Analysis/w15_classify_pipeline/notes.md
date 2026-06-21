# W15 — 分类推理端到端闭环

> Q2 第二步。把 W14 的「随机输入 → 无语义 Top-1」升级为「真实图片 → 真实 Top-5 分类标签」。
> 复用 W14 `InferenceEngine` 不改动；新建分类专用预处理 + 后处理 + Classifier 编排。
>
> 通用概念去主题库：预处理几何/归一化/通道序/布局/silent bug 见 [`docs/notes/image-ops.md`](../../docs/notes/image-ops.md)；
> softmax/Top-K 见 [`docs/notes/inference.md`](../../docs/notes/inference.md)。本笔记只留本模块怎么用 + 设计/踩坑/测试。

## 闭环结果

| 项 | 实际值 |
|---|---|
| 模型 | MobileNetV2（复用 W14 的 `mobilenetv2.onnx`，不重复存放） |
| 输入 | `[1,3,224,224] float32`，ImageNet 归一化后的 CHW |
| 预处理 | resize 短边 256 → center-crop 224 → BGR2RGB → /255 → 归一化 |
| 后处理 | softmax → Top-K（partial_sort）→ ImageNet-1000 标签映射 |
| 测试图 | pytorch 示例 `dog.jpg`（实为 Samoyed 犬） |
| **端到端输出** | **Top-1: Samoyed (0.6458)**，Top-2 Pomeranian、Top-3 West Highland white terrier……全为犬种 |

Top-1 语义正确（Samoyed），且 Top-5 全是犬种，**反证预处理与标签对齐均正确**——
若 BGR/RGB 弄反或归一化错误，输出会是无关类别（详见 image-ops.md「silent 预处理 bug」）。

## 数据流（Mermaid）

```mermaid
flowchart LR
  A["图片路径"] -->|cv::imread| B["cv::Mat (BGR/HWC)"]
  B --> C["Preprocess:<br/>resize256→crop224<br/>→BGR2RGB→/255→归一化<br/>→HWC2CHW"]
  C --> D["float buffer<br/>[1,3,224,224]"]
  D -->|零拷贝 span| E["w14::InferenceEngine<br/>::Run"]
  E --> F["logits[1000]"]
  F --> G["Softmax<br/>→概率"]
  G --> H["partial_sort<br/>Top-K"]
  H -->|labels[id]| I["Top-5<br/>标签+概率"]
```

## 本模块设计决策

1. **复用 W14 推理零改动**：`Classifier` 持有 `w14::InferenceEngine`，预处理输出的 CHW buffer 经
   `std::span<const float>` 零拷贝喂入 `Run`，shape 传 `{1,3,224,224}`——batch 维由 shape 表达，
   无需改 buffer 或动 W14。（概念见 inference.md「零拷贝张量输入」）
2. **分类预处理独立于 W13 检测路径**：W13 `LetterboxToTensor` 是 640 letterbox + `/255`（对 YOLO 正确），
   分类走 resize256→centercrop224→归一化，两套路径分离、边界清晰。W13 原样留给 W16 检测。
   （两种范式为何不能混用见 image-ops.md「分类预处理范式」）
3. **本模块预处理参数**（`PreprocConfig`，ImageNet 标准、RGB 序，消 tech-debt🔴「归一化未定义」）：
   `mean={0.485,0.456,0.406}`、`std={0.229,0.224,0.225}`、`resize_short=256`、`crop=224`、`to_rgb=true`。
   （这组常量的来历与坑见 image-ops.md「ImageNet 归一化」）

## 模块结构

```
w15_classify_pipeline/
  preprocess.{hpp,cpp}   # PreprocConfig + Preprocess(cv::Mat→CHW float)
  postprocess.{hpp,cpp}  # Softmax + TopKResults + LoadLabels
  classifier.{hpp,cpp}   # Classifier 编排：preprocess + W14 推理 + postprocess
  classify_demo.cpp      # ./w15_classify_demo <img> 打印 Top-5
  models/                # imagenet_classes.txt(1000 行) + test_cat.jpg
```

## 踩坑（带 commit）

- **标签对齐**（commit 28a3377）：`imagenet_classes.txt` 用 torchvision/ONNX Model Zoo 的
  ImageNet-2012 顺序（index 0=tench，999=toilet tissue），与 MobileNetV2 输出顺序一致。错位会让 Top-1 标签全偏。
- **标签文件末行无换行**（commit 251f70d）：最后一行无 `\n`，`wc -l` 显示 999、实际 1000 条；
  `std::getline` 读取正常（按行分隔符切，不依赖末尾换行）。`LoadLabels` 另兼容 Windows `\r`。
- **silent 预处理 bug**（commit a232ac7 / 8f24e0d）：归一化/BGR-RGB/centercrop/CHW 任一错都不报错只偏结果——
  靠纯色图单测（手算归一化值）+ 真实图眼检（Samoyed）双重兜底。通用机理见 image-ops.md「silent 预处理 bug」。

## 测试 / 基准

| 测试目标 | 内容 | 状态 |
|---|---|---|
| `W15_PreprocessTest` | 纯色图手算归一化值校验、CHW 布局 | ✅ |
| `W15_PostprocessTest` | softmax 和为 1、Top-K 降序、维度不一致抛异常 | ✅ |
| `W15_ClassifierTest` | 真实图端到端 Top-1=Samoyed、坏路径抛异常 | ✅ |

```bash
ctest --test-dir build -R "W15_" --output-on-failure   # 3 个全绿（CPU 推理）
```

## 编译运行命令

```bash
# 编译本周全部目标
cmake --build build --target w15_preprocess_test w15_postprocess_test \
      w15_classifier_test w15_classify_demo -j$(nproc)

# 跑 demo（真实图 → Top-5）
./build/02_Inference_Analysis/w15_classify_pipeline/w15_classify_demo \
  02_Inference_Analysis/w15_classify_pipeline/models/test_cat.jpg
```

## 与下一步衔接

- **W16**：YOLO 检测，复用 W13 letterbox 路径 + 多输出头 + NMS + 坐标反算。
- **W14.5（已完成）**：CUDA EP 在 W14 引擎层验证（`ActiveEp()==kCuda`）；W15 `Classifier` 未透传 `Ep`，
  分类闭环目前仅 CPU 跑过——「真实图 + CUDA」端到端组合未触达任何新代码路径，暂不补（见 inference.md「EP 与优雅回退」）。
