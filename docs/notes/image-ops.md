# 图像预处理 / 后处理概念详解

> 可复用概念的「主题正文」，复习时进这里读。模块专属的设计/Mermaid/踩坑/测试在周笔记。
> 来源周：W15（分类端到端 + ROI/stride，已入）、W16（Letterbox + 坐标反算，已入）；W9/W10/W13 的双线性插值待毕业。

## 目录

- [分类预处理范式（resize 短边 → center-crop）](#分类预处理范式resize-短边--center-crop)
- [cv::Mat ROI 视图 vs clone（stride / isContinuous）](#cvmat-roi-视图-vs-clonestride--iscontinuous)
- [ImageNet 归一化](#imagenet-归一化)
- [BGR ↔ RGB 通道序](#bgr--rgb-通道序)
- [HWC ↔ CHW 内存布局](#hwc--chw-内存布局)
- [silent 预处理 bug（错了不报错只偏结果）](#silent-预处理-bug错了不报错只偏结果)
- [Letterbox + 坐标反算（检测预处理范式）](#letterbox--坐标反算检测预处理范式)

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

**坑**：① resize 短边要缩到**比 crop 大一圈**（256 而非 224），留出余量再中心裁 224——裁剪率 256/224≈0.875 是约定常数，对应"切掉四周各 ~12.5% 边缘 + 画面略放大"。直接"短边→224 再裁 224"虽然不变形、能跑，但等于不裁边、看全图，构图和模型 eval 管线（torchvision `Resize(256)+CenterCrop(224)`）不符，Top-1 会有零点几个点的 silent 偏差。256 不是魔法数字，是"resize 尺寸 > crop 尺寸"这一套路的配套值（换模型可能是 `Resize(232)+CenterCrop(224)`）。② 检测任务（YOLO）用的是 **Letterbox**（等比缩放 + 灰边填充，不裁剪）——两套范式不能混用：分类容忍裁掉边缘，检测必须保留全图否则目标框丢失。③ `cv::Rect` 取的是视图，跨步后续写入要 `.clone()` 拿独立连续内存。

> 实战出处：`02_Inference_Analysis/w15_classify_pipeline/notes.md`（预处理节，commit a232ac7）

---

## cv::Mat ROI 视图 vs clone（stride / isContinuous）

**是什么**：`resized(cv::Rect(x, y, w, h))` 不返回新图，而是一个 **ROI 视图**（Region Of Interest，从父图里框出的子区域）。要分清三个东西：父图（整张 `resized`）、ROI（框出的子块，借父图内存）、`clone()` 后的独立图。ROI 与父图**共享同一块内存**，只改了起点指针和逻辑宽高，**行跨步 `step` 仍继承父图**。

**为什么 / 何时用**：`Mat` 定位第 r 行靠的是 `step`（每行字节数），不是逻辑宽度。父图 341 宽时 `step=341×3=1023`；从中框出 224 宽的 ROI，`step` 仍是 1023，于是 ROI 每行有效数据之间**夹着被裁掉的列**——内存一段段断开，`isContinuous()` 为 `false`。`clone()` 会**另开一块大小刚好 = 224×(224×3) 的新内存**（`step=672`，正好一行无余量），把 ROI 逐行紧凑拷过去，行间无空隙 → `isContinuous()` 变 `true`，整块可当一维连续数组遍历。判定式：`isContinuous()` ⟺ `step == 宽×通道`。

```cpp
cv::Mat roi = resized(cv::Rect(58, 16, 224, 224));  // 视图，step=1023，不连续
cv::Mat cropped = roi.clone();                      // 新内存，step=672，连续
```

**坑**：① 不 `.clone()` 直接把 ROI 当连续 buffer 遍历（如按 CHW 顺序写张量），会读到本该被裁掉的列，数据错位且不报错——又一个 silent bug。② ROI 生命周期绑在父图上：父图析构，ROI 悬空。③ 别把"父图连续"当"ROI 连续"——`resized` 整张是连续的，从它框出的子块才不连续。

> 实战出处：`02_Inference_Analysis/w15_classify_pipeline/notes.md`（预处理节 center-crop，commit a232ac7）

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

---

## Letterbox + 坐标反算（检测预处理范式）

**是什么**：Letterbox 是检测任务的标准预处理——等比缩放（`scale = min(W/w, H/h)`）后四周填灰边（YOLO 默认 114）补成方形，**保留全图不裁剪**。与分类的 center-crop 是两套不能混用的范式（分类容忍裁边，检测裁掉就丢目标）。配套产出 `LetterboxInfo{scale, pad_left, pad_top}`，供后处理把检测框从 letterbox 坐标系反算回原图：`orig = (lb_coord - pad) / scale`。

**为什么 / 何时用**：模型在方形输入上推理，输出框是 letterbox 坐标系的；要在原图上画框/算 mAP，必须反算回去。坐标反算是检测里**最高 bug 风险点**——pad 和 scale 任一搞反，框会整体偏移或缩放，且在「目标大致在中间」的图上偏移不明显，是典型 silent bug，必须单独单测（给定已知 `LetterboxInfo` 断言反算结果 <1px）。

**坑**：① **填充居中**：左右/上下均分，余数加到右/下（对齐 YOLOv5/v8 官方）；不居中虽然反算仍对，但喂给模型的像素分布变了，临界框检测结果会漂。② **对拍范式必须一致**：动态 H/W 的 ONNX 让 ultralytics（YOLOv8 的官方 Python 实现，对拍时当「标准答案」）默认走**矩形推理**（按 stride 补到非方形、几乎无填充），而自己的 C++ 流水线常固定喂方形 640×640。两端 letterbox 范式不一致时，高分目标对得上、临界框（score≈0.25）会差出整框——对拍前先确认两端都用方形（ultralytics 传 `rect=False`）。〔stride = 网络对输入的总下采样倍数，YOLOv8 最大 stride=32 → 输入宽高必须被 32 整除；矩形推理正是利用这点，短边只补到最近的 32 倍数而非补满方形，省灰边、算得快。〕③ YOLOv8 的归一化只有 `/255`（无 ImageNet mean/std），别把分类那套均值方差套上来。④ Letterbox 按输入通道顺序展平，BGR→RGB 要在 letterbox **之前**做。

> 实战出处：`02_Inference_Analysis/w16_yolo_detector/notes.md`（设计/踩坑节）；算子实现 `01_Linux_CPP_Foundations/w10_resize/custom_resize.cpp`（`Letterbox` / `LetterboxToTensor`）
