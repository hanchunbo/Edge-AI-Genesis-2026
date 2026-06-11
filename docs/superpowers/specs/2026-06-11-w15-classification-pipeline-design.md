# W15 — 分类推理端到端闭环 设计文档

> 状态：设计已与用户确认（路线 A）。日期：2026-06-11。
> 阶段：Q2 W15。前置：W14（ONNX Runtime C++ 基础闭环）已完成。

## 1. 目标

把当前 demo 的「随机输入 → 无语义 Top-1」升级为「**真实图片 → 真实 Top-5 分类标签**」，
完成从图片到识别结果的完整链路。同时消除 tech-debt 中的 🔴 项「归一化参数未定义」。

**非目标（明确排除）：**

- 目标检测 / YOLO 多输出头 / NMS —— 留 W16
- CUDA EP / GPU 推理 —— 留 W14.5 / 后续
- 批量多图吞吐 / 多线程预处理 —— 分类单图 demo 不需要，W13 的批量引擎留给 W16

## 2. 范围与路线决策

采用**路线 A**：新建独立 `w15` 模块，使用**分类专用预处理**（resize→center-crop 224→
归一化），复用 W14 `InferenceEngine` 不改动。

**为什么不复用 W13 ImagePipeline**：W13 是 640 letterbox 的检测味预处理 + 多线程批量引擎，
分类的标准流程（resize 256→center-crop 224→ImageNet 归一化）与之是两套路子，
硬塞入 W13 反而耦合。W13 原样保留给 W16 检测。

**为什么先做分类（而非直接 W16 检测）**：分类是低风险的功能闭环，先打通
预处理/推理/后处理三段基础设施，再在 W16 复用并扩展到检测。

## 3. 架构

新模块目录：`02_Inference_Analysis/w15_classify_pipeline/`，命名空间 `w15`。

```text
真实图片(jpg/png)
  → [Preprocess]  cv::imread → resize 256 → center-crop 224
                  → BGR2RGB → HWC2CHW → ImageNet 归一化(mean/std)
                  → std::vector<float> (3×224×224)
  → [Infer]       复用 w14::InferenceEngine.Run(span, shape={1,3,224,224})
  → [Postprocess] softmax → Top-K(partial_sort) → 映射 ImageNet-1000 标签
  → std::vector<TopK{label, score}>  → 打印 Top-5
```

### 3.1 组件（四个独立可测单元）

| # | 单元 | 形态 | 职责 | 依赖 |
|---|------|------|------|------|
| 1 | `Preprocess` | 自由函数 | `cv::Mat → std::vector<float>`：resize/centercrop/RGB/CHW/归一化 | OpenCV，（resize 可借鉴 W10 思路，但本模块独立实现分类路径） |
| 2 | `InferenceEngine` | 复用 W14 类 | 模型加载 + 推理，**不改一行** | onnxruntime |
| 3 | `Postprocess` | 自由函数 | `span<const float> logits → vector<TopK>`：Top-K + 标签映射 | 标签表 |
| 4 | `Classifier` | 编排类 | 持有 InferenceEngine + 配置（mean/std/labels），对外 `Classify()` | 上面 1/2/3 |

### 3.2 关键数据结构

```cpp
// 预处理配置（归一化参数显式定义 —— 消 tech-debt 🔴）
struct PreprocConfig {
  int resize_short = 256;   // 短边缩放到 256
  int crop = 224;           // center-crop 到 224×224
  std::array<float,3> mean{0.485f, 0.456f, 0.406f};  // ImageNet RGB
  std::array<float,3> std {0.229f, 0.224f, 0.225f};
  bool to_rgb = true;       // OpenCV 读入是 BGR，分类模型要 RGB
};

// 单条 Top-K 结果
struct TopK {
  int index;            // 类别索引
  float score;          // softmax 后概率（0~1）
  std::string label;    // ImageNet 标签文本
};
```

### 3.3 Classifier 接口（草案）

```cpp
class Classifier {
 public:
  Classifier(const std::string& model_path,
             const std::string& labels_path,
             PreprocConfig cfg = {});

  // 从文件路径分类（内部 cv::imread）
  [[nodiscard]] std::vector<TopK> Classify(const std::string& image_path,
                                           int top_k = 5);
  // 从已解码图像分类
  [[nodiscard]] std::vector<TopK> Classify(const cv::Mat& bgr, int top_k = 5);

 private:
  w14::InferenceEngine engine_;
  std::vector<std::string> labels_;   // 1000 行
  PreprocConfig cfg_;
};
```

## 4. 数据流关键点 & tech-debt 修复

- **归一化参数显式化**：`mean/std` 写进 `PreprocConfig`，闭环里实际生效 →
  消除 tech-debt「归一化参数未定义（🔴 高）」。
- **batch 维**：预处理输出连续 CHW buffer，`Run` 的 `shape` 传 `{1,3,224,224}` 即可表达 batch=1，
  **无需改 buffer，也无需改 W14**。
- **BGR→RGB**：OpenCV `imread` 默认 BGR；分类模型按 RGB 训练，显式转换。
- **center-crop**：短边 resize 到 256 后中心裁 224，是 ImageNet 分类标准（保持长宽比，避免拉伸失真）。

## 5. 错误处理

沿用 W14 风格（抛 `std::runtime_error` 带上下文）：

- 图片不存在 / `cv::imread` 返回空 → 抛 `std::runtime_error("[W15] 图片读取失败: ...")`
- 标签文件缺失 / 行数 != 模型输出维度 → 抛，提示行数不匹配
- 模型缺失 → **测试中**用 `GTEST_SKIP()` 优雅跳过（沿用 W14，CI/新机器初次拉取不报红）

## 6. 测试（GTest，命名 `W15_ClassifyPipelineTest`）

1. **Preprocess 单测**：
   - 输出尺寸恒为 `3*224*224 = 150528`
   - CHW 排布正确（构造已知像素图，验证通道/位置映射）
   - 归一化数值抽样校验（已知像素 → 手算 `(x/255 - mean)/std` 比对）
2. **Postprocess 单测**：
   - 给定构造 logits，Top-K 索引与降序正确
   - 标签映射正确（索引 → 文本）
   - softmax 概率和 ≈ 1
3. **端到端单测**（依赖模型 + 测试图，缺失则 `GTEST_SKIP`）：
   - 对 bundle 的已知类别图（如猫）跑 `Classify`，断言 Top-5 命中预期类
   - 至少保证确定性输出（同图同结果）

## 7. 新增资产

| 资产 | 说明 | 风险 |
|------|------|------|
| `models/imagenet_classes.txt` | ImageNet-1000 标签，每行一个 | **需与本 MobileNetV2 导出版本的类别索引对齐**（注意 1000 vs 1001/background 偏移坑） |
| `models/test_cat.jpg` | 已知类别测试图 | 选 ImageNet 类别明确的图，便于断言 |

## 8. 验收标准

```bash
./w15_classify_demo models/test_cat.jpg
# 期望输出形如：
# Top-1: Egyptian cat (0.82)
# Top-2: tabby        (0.11)
# ...
```

- demo 对真实图输出语义正确的 Top-5
- 3 组单测全绿（端到端组在有模型/图时跑，否则优雅跳过）
- tech-debt「归一化参数未定义」标记为 FIXED

## 9. 已知风险 / 注意点（开发期重点防）

1. **silent 预处理 bug**：归一化 / BGR-RGB / centercrop 任一错 → 不报错但分类悄悄偏。
   缓解：用已知像素的单测逐段验证，端到端用已知类别图兜底。
2. **标签索引对齐**：确认模型输出 1000 维与标签文件行数一致，warn/抛错而非静默错位。
3. **预处理与 W10/W13 部分重叠**：本模块独立实现分类预处理，接受少量 resize 逻辑重叠
   （分类 centercrop/normalize 是新逻辑），换边界清晰。W16 时若需要可再抽象统一预处理层。

## 10. 工作量估计

约 1–2 天。无新重依赖（OpenCV 已在用），无 CUDA/环境折腾。

## 11. 模块边界小结

- `w15` 依赖：OpenCV（预处理/解码）、`w14::InferenceEngine`（推理）、标签表
- `w15` 不改动 W14 及更早模块
- 对外可理解性：`Classifier.Classify(path) → Top-5`，使用者无需了解内部三段细节
