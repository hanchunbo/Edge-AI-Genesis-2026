# W15 — 分类推理端到端闭环

> Q2 第二步。把 W14 的「随机输入 → 无语义 Top-1」升级为「真实图片 → 真实 Top-5 分类标签」。
> 复用 W14 `InferenceEngine` 不改动；新建分类专用预处理 + 后处理 + Classifier 编排。

## 闭环结果

| 项 | 实际值 |
|---|---|
| 模型 | MobileNetV2（复用 W14 的 `mobilenetv2.onnx`，不重复存放） |
| 输入 | `[1,3,224,224] float32`，ImageNet 归一化后的 CHW |
| 预处理 | resize 短边 256 → center-crop 224 → BGR2RGB → /255 → 归一化 |
| 后处理 | softmax → Top-K（partial_sort）→ ImageNet-1000 标签映射 |
| 测试图 | pytorch 示例 `dog.jpg`（实为 Samoyed 犬） |
| **端到端输出** | **Top-1: Samoyed (0.6458)**，Top-2 Pomeranian、Top-3 West Highland white terrier……全为犬种 |
| 单测 | 3 个测试目标全绿：预处理 2 + 后处理 3 + 端到端 2 用例 |

Top-1 语义正确（Samoyed），且 Top-5 全是犬种，**反证预处理（BGR→RGB、归一化）与标签对齐均正确**——
若 BGR/RGB 弄反或归一化错误，输出会是无关类别。

## 预处理参数（消 tech-debt 🔴「归一化未定义」）

`PreprocConfig` 显式定义（ImageNet 标准，RGB 序）：

- `mean = {0.485, 0.456, 0.406}`
- `std  = {0.229, 0.224, 0.225}`
- `resize_short = 256`，`crop = 224`，`to_rgb = true`

## 关键设计

1. **分类预处理独立于 W13 检测路径**：W13 `LetterboxToTensor` 是 640 letterbox + `/255`（对 YOLO 正确），
   分类走 resize256→centercrop224→归一化，两套路径分离，边界清晰。W13 原样留给 W16 检测。
2. **batch 维由 shape 表达**：预处理输出连续 CHW buffer，`Run` 传 `shape={1,3,224,224}` 即可，无需改 buffer 或 W14。
3. **BGR→RGB**：OpenCV `imread` 默认 BGR；分类模型按 RGB 训练。输出通道 c（RGB 序）取源通道 `2-c`。
4. **复用 W14 推理**：`Classifier` 持有 `w14::InferenceEngine`，零拷贝喂入 `std::span<const float>`。

## 模块结构

```
w15_classify_pipeline/
  preprocess.{hpp,cpp}   # PreprocConfig + Preprocess(cv::Mat→CHW float)
  postprocess.{hpp,cpp}  # TopK + Softmax + LoadLabels + TopKResults
  classifier.{hpp,cpp}   # Classifier 编排：preprocess + W14 推理 + postprocess
  classify_demo.cpp      # ./w15_classify_demo <img> 打印 Top-5
  models/                # imagenet_classes.txt(1000 行) + test_cat.jpg
```

## 踩坑 / 注意

- **标签对齐**：`imagenet_classes.txt` 用 torchvision/ONNX Model Zoo 的 ImageNet-2012 顺序
  （index 0 = tench，999 = toilet tissue），与 MobileNetV2 输出顺序一致。错位会让 Top-1 标签全偏。
- **末行无换行**：标签文件最后一行无 `\n`，`wc -l` 显示 999，实际 1000 条；`std::getline` 读取正常。
- **silent 预处理 bug 风险**：归一化/BGR-RGB/centercrop 任一错都不报错只是结果偏——
  靠纯色图单测（手算归一化值）+ 真实图眼检（Samoyed）双重兜底。

## 与下一步衔接

- W16：YOLO 检测，复用 W13 letterbox 路径 + 多输出头 + NMS + 坐标反算。
- W14.5：CUDA EP（GPU 推理对比），环境就绪后随时可插入。
