# W10 自定义 Resize 算子

> **目标**：从零实现图像缩放算子，理解推理引擎（ONNX Runtime / TensorRT）的
> 坐标对齐细节，以及 YOLO 系列的 Letterbox 预处理原理。

---

## 实现一览

| 版本 | 算法 | 核心特点 |
|------|------|---------|
| V1 | 最近邻（Nearest Neighbor） | 速度最快，仅一次整数查表，无插值 |
| V2 | 双线性（Bilinear，Q8 定点） | 质量中等，Q8 定点避免 float 乘法 |
| V3 | Letterbox | YOLO 标准预处理，等比缩放 + 灰色填充 |

---

## 核心概念

### 1. 为什么需要自定义 Resize？

`cv::resize` 足够快，但有两个工程问题：

1. **坐标对齐不透明**：ONNX Runtime 默认 `asymmetric`，TensorRT 默认 `half_pixel`，
   两者在相同 scale 下坐标映射不同，混用会导致边界像素偏移 0.5 个像素。
   自定义实现可以精确控制对齐模式，方便与推理引擎的 Resize 算子对齐验证。

2. **嵌入式平台无 FPU**：部分 MCU（如 Cortex-M4 无 FPU 配置）没有浮点硬件，
   float 乘法由软件模拟，极慢。Q8 定点实现全程整数运算，可直接移植。

### 2. 坐标映射：Asymmetric vs HalfPixel

反向映射（dst → src）公式：

```
Asymmetric（ONNX 默认）：
  src_x = dst_x * (src_w / dst_w)
  解释：dst 左上角 (0,0) 对齐 src 左上角 (0,0)

HalfPixel（TensorRT / PyTorch 默认）：
  src_x = (dst_x + 0.5) * (src_w / dst_w) - 0.5
  解释：像素中心对齐；dst 像素中心 (0.5,0.5) 对齐 src 像素中心 (0.5,0.5)
```

**实际影响**：1920→640 缩放时，dst 最后一个像素 (639) 的 src 坐标：
- Asymmetric：639 × (1920/640) = 1917.0 → src 第 1917 列
- HalfPixel：(639+0.5) × 3.0 - 0.5 = 1918.0 → src 第 1918 列

差了 1 列，在检测模型右边缘会引入系统性偏差。

### 3. Q8 双线性插值定点推导

传统 float 双线性：
```
result = (1-wy)*[(1-wx)*P00 + wx*P10]
       + wy   *[(1-wx)*P01 + wx*P11]
```

Q8 定点（避免 float 乘法）：
```
wx_q  = int(wx * 256 + 0.5)   // 小数权重量化为 [0, 256] 整数
wx0_q = 256 - wx_q

row0 = wx0_q * P00 + wx_q * P10   // int32，最大 256*255 = 65280
row1 = wx0_q * P01 + wx_q * P11

result_q16 = wy0_q * row0 + wy_q * row1   // 最大 256*65280 = 16711680 < 2^24
result = result_q16 >> 16                  // 等价除以 65536（= 256×256）
```

误差分析：量化误差 ≤ 0.5 LSB，最终结果与 float 版本 MAE < 1。

### 4. Letterbox 原理与坐标反算

**为什么用 Letterbox 而不是直接 resize？**

直接 resize 会改变长宽比，导致物体变形，检测框比例失真。
Letterbox 等比缩放后填充，保留原始比例。

**填充量计算（以 1920×1080 → 640×640 为例）**：
```
scale = min(640/1920, 640/1080) = min(0.333, 0.593) = 0.333
new_w = round(1920 × 0.333) = 640
new_h = round(1080 × 0.333) = 360
pad_top = (640 - 360) / 2 = 140
pad_bottom = 640 - 360 - 140 = 140
pad_left = 0, pad_right = 0
```

**检测框坐标反算**（推理输出 → 原始图像坐标）：
```cpp
src_x = (det_x - pad_left) / scale;
src_y = (det_y - pad_top)  / scale;
```

---

## Benchmark 结果（1920×1080 → 640×640，50 次均值，Release 模式）

> 在 WSL2 / Intel i7 上实测，仅供参考（生产环境请在目标硬件上重测）

| 实现 | 耗时 | 对比 cv::resize |
|------|------|----------------|
| V1 Nearest (asymmetric) | ~3.2 ms | ~1.4× 慢 |
| V1 Nearest (half_pixel) | ~3.2 ms | ~1.4× 慢 |
| V2 Bilinear (asymmetric) | ~18 ms | ~5× 慢 |
| V2 Bilinear (half_pixel) | ~18 ms | ~5× 慢 |
| V3 Letterbox | ~3.8 ms | （内部调用 cv::resize） |
| cv::resize NEAREST | ~2.3 ms | 基准 |
| cv::resize LINEAR | ~3.6 ms | 基准 |

> **V2 慢的根本原因**：纯 C++ 标量双线性内循环有 6 次整数乘法 × 3 通道 × 每像素，
> 编译器自动向量化效果有限（非 contiguous 访问模式）。
> OpenCV 的 `INTER_LINEAR` 内部使用 SIMD + 行预计算优化（先算水平插值行，再做垂直），
> 比我们的朴素双循环快约 5×。
> 工程上 Resize 直接用 `cv::resize`；自研版本的价值在于理解算法原理和坐标语义。

---

## Q&A

**Q: 我该用 asymmetric 还是 half_pixel？**

看你对接的推理引擎：
- ONNX Runtime (opset ≥ 11)：`coordinate_transformation_mode = "asymmetric"` 是默认值
- TensorRT 8+：默认 `half_pixel`（对应 PyTorch `align_corners=False`）
- 如果自己写 C++ 预处理接在推理引擎前，保持和引擎的 Resize 算子模式一致即可

**Q: Letterbox 的 pad 颜色为什么是 114？**

YOLOv5/v8 训练时用 `(114,114,114)` 填充，是 ImageNet 均值附近的灰色，
对背景特征影响最小。推理预处理必须和训练预处理完全一致，否则准确率下降。

**Q: V2 Q8 定点实现值得在生产用吗？**

对于有 FPU 的现代 CPU/GPU，Q8 不比 float 快（乘法延迟相同）。
Q8 的价值场景：
1. 无 FPU 的 MCU（Cortex-M0/M3）
2. DSP/NPU 只有整数乘法单元
3. 为量化推理（INT8 模型）实现配套预处理时保持数据类型一致

**Q: 最近邻、双线性、Letterbox 三者是什么关系？**

这是两个层次的问题：

- **最近邻 / 双线性**：解决"目标像素取什么颜色"的问题，是**插值算法**，两者平级可互换。
  - 最近邻：直接取最近源像素，快但锯齿明显
  - 双线性：对周围 4 个源像素加权平均，慢但平滑
- **Letterbox**：解决"整张图怎么放进固定正方形而不变形"的问题，是**预处理策略**，
  内部仍需调用最近邻或双线性来完成等比缩放这一步。

```
直接 Resize（拉伸）：1920×1080 → 640×640  人物比例失真 ❌
Letterbox：          1920×1080 → 640×360 → 上下加灰边 → 640×640  比例保持 ✅
```

**Q: `LetterboxToTensor` 为什么输出是一个数组而不是 B/G/R 三个数组？**

推理引擎（ONNX Runtime / TensorRT）的 C API 接受的是
**一个连续内存指针 + 一个 shape 描述符**：

```cpp
session.Run(tensor.data(), {1, 3, H, W}, "input");
```

shape `{1, 3, H, W}` 告诉引擎如何解读这块内存：前 H×W 个 float 是 B 通道，
中间是 G 通道，最后是 R 通道。三个"面"物理上是同一块内存的不同偏移段，
一块连续内存比三个散块对 CPU cache 和 GPU `cudaMemcpy` 都更友好。

**Q: 为什么 `cv::resize` 不支持选 Asymmetric / HalfPixel，这是工程问题怎么理解？**

`cv::resize` 的签名只暴露插值方法（`INTER_NEAREST` / `INTER_LINEAR`），
坐标映射方式被硬编码在内部，用户无法控制。

工程问题在于：推理链路上存在**两个独立发生的 Resize**：

```
你的 C++ 预处理          推理引擎内部模型
  cv::resize()    →     ONNX Resize 算子（asymmetric / half_pixel 可声明）
  坐标模式不透明          坐标模式明确
```

若两端坐标模式不一致，1920→640 时右边缘像素会系统性偏移 1 列，
导致右侧小目标漏检，肉眼完全看不出，只有精确对比像素坐标才能发现。
自定义实现的价值就在于能**精确控制坐标语义，与推理引擎零误差对齐**。


