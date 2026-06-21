# 图像预处理 / 后处理概念详解

> 可复用概念的「主题正文」，复习时进这里读。模块专属的设计/Mermaid/踩坑/测试在周笔记。
> 来源周：W15（分类端到端，已入）；W9/W10/W13 的双线性插值、Letterbox、坐标对齐、isContinuous 待毕业。

## 目录

- [分类预处理范式（resize 短边 → center-crop）](#分类预处理范式resize-短边--center-crop)
- [ImageNet 归一化](#imagenet-归一化)
- [BGR ↔ RGB 通道序](#bgr--rgb-通道序)
- [HWC ↔ CHW 内存布局](#hwc--chw-内存布局)
- [silent 预处理 bug（错了不报错只偏结果）](#silent-预处理-bug错了不报错只偏结果)

---

## 分类预处理范式（resize 短边 → center-crop）

**是什么**：图像分类的标准预处理几何步骤——先把**短边**缩放到固定值（如 256，保持长宽比），再从中心**裁剪**出正方形（如 224×224）。不是直接 resize 到 224×224。

**为什么 / 何时用**：直接 resize 到目标尺寸会**拉伸变形**（宽图被压扁、长图被拉长），破坏物体比例。短边 resize 保持长宽比不失真，center-crop 取中心区域（分类目标通常居中）。这套范式与 torchvision `Resize(256)+CenterCrop(224)` 一致，必须和模型训练时的预处理对齐。

```cpp
const float scale = float(resize_short) / std::min(h, w);  // 按短边算缩放比
cv::resize(bgr, resized, cv::Size(lround(w*scale), lround(h*scale)));
const int x = (nw - crop) / 2, y = (nh - crop) / 2;        // 中心裁剪起点
cropped = resized(cv::Rect(x, y, crop, crop)).clone();
```

**坑**：检测任务（YOLO）用的是 **Letterbox**（等比缩放 + 灰边填充，不裁剪）——两套范式不能混用：分类容忍裁掉边缘，检测必须保留全图否则目标框丢失。`cv::Rect` 取的是视图，跨步后续写入要 `.clone()` 拿独立连续内存。

> 实战出处：`02_Inference_Analysis/w15_classify_pipeline/notes.md`（预处理节，commit a232ac7）

---

## ImageNet 归一化

**是什么**：把像素值按 ImageNet 数据集统计出的均值/标准差做标准化：先 `/255` 到 0~1，再 `(v - mean) / std`，逐通道。标准常量（RGB 序）：

- `mean = {0.485, 0.456, 0.406}`
- `std  = {0.229, 0.224, 0.225}`

**为什么 / 何时用**：模型训练时输入就是这么归一化的，推理**必须用完全相同的 mean/std**，否则输入分布偏移、模型表现崩坏。这组数字是 ImageNet-1k 训练集上每个通道的真实统计量，几乎所有在 ImageNet 预训练的模型（ResNet/MobileNet/…）通用。

**坑**：① mean/std 是 **RGB 序**，若图像还是 BGR 就逐通道套错（R 的均值套到了 B 上）——必须先转 RGB 再归一化，或保证 mean/std 与当前通道序一致。② 别漏 `/255`：mean/std 是在 0~1 尺度上统计的，直接拿 0~255 的像素减 0.485 完全错位。③ 数值错了不会报错，只是结果偏（见下「silent 预处理 bug」）。

> 实战出处：`02_Inference_Analysis/w15_classify_pipeline/notes.md`（预处理参数节，消 tech-debt「归一化未定义」，commit a232ac7）

---

## BGR ↔ RGB 通道序

**是什么**：同一张彩色图，像素三通道的存储顺序有两种约定——OpenCV `imread` 默认 **BGR**（蓝绿红），而绝大多数深度模型按 **RGB**（红绿蓝）训练。需要在喂模型前转换。

**为什么 / 何时用**：通道序只是「约定」，数据本身不带标签。喂给 RGB 模型前必须 BGR→RGB，否则模型把「红色」当「蓝色」理解。转换可用 `cv::cvtColor(.., COLOR_BGR2RGB)`，或在逐像素拷贝时按 `src_c = 2 - c` 反取：

```cpp
// 输出通道 c 为 RGB 序（c=0→R,1→G,2→B）；OpenCV 像素 BGR：R=px[2],G=px[1],B=px[0]
const int src_c = to_rgb ? (2 - c) : c;
```

**坑**：弄反**不报错、不崩溃**，只是分类结果整体偏到无关类别——是最隐蔽的预处理 bug 之一。靠真实图眼检兜底（W15 里 BGR/RGB 弄反，Samoyed 就会变成乱七八糟的类别）。

> 实战出处：`02_Inference_Analysis/w15_classify_pipeline/notes.md`（关键设计 3，commit a232ac7）

---

## HWC ↔ CHW 内存布局

**是什么**：同样的图像数据，两种排布。**HWC**（OpenCV `cv::Mat` 默认）= 按像素存，每个像素的三通道挨着（R0G0B0 R1G1B1…）；**CHW**（多数模型输入要求）= 按通道存，整个 R 平面、再整个 G 平面、再 B 平面（R0R1…RnG0G1…）。

**为什么 / 何时用**：模型输入张量 shape `[1,3,224,224]` = NCHW，要求数据按通道平面连续。所以预处理最后一步要把 HWC 的 `cv::Mat` 重排成 CHW 连续 buffer：

```cpp
out[c * hw + pos] = v;   // c 选通道平面，pos 是平面内 y*W+x 偏移 → CHW
```

**坑**：写成 `out[pos*3 + c]`（HWC）喂给要 CHW 的模型，shape 对得上但数据排布错位，结果全乱且不报错。

> 实战出处：`02_Inference_Analysis/w15_classify_pipeline/notes.md`（预处理节，commit a232ac7）

---

## silent 预处理 bug（错了不报错只偏结果）

**是什么**：预处理里归一化系数、BGR/RGB 通道序、center-crop 偏移、HWC/CHW 布局——任何一处错了，程序**照常跑通、不抛异常**，只是模型输出悄悄偏到错误类别。区别于会崩溃的「响 bug」。

**为什么 / 何时用**：这类 bug 危险在于「假成功」——CI 绿、demo 不报错，但精度崩了没人知道。防御靠**双重兜底**：① 纯色图单测（手算归一化后的期望值，断言每个通道数值正确）；② 真实图眼检（语义对不对，如 Top-1=Samoyed 且 Top-5 全犬种，反证整条链路正确）。

**坑**：单元测试只测「数值算对」不够——通道序、布局这类错误在纯色图上可能照样通过（纯色三通道值接近时不敏感）。必须配真实图语义验证，两者缺一不可。

> 实战出处：`02_Inference_Analysis/w15_classify_pipeline/notes.md`（踩坑节，commit a232ac7 / 8f24e0d）
