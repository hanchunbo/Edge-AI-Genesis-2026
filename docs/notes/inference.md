# 推理引擎（ONNX Runtime）概念详解

> 可复用概念的「主题正文」，复习时进这里读。模块专属的设计/Mermaid/踩坑/测试在周笔记。
> 来源周：W14（ORT 基础闭环）、W15（分类端到端）。

## 目录

- [ONNX vs ORT vs 任务](#onnx-vs-ort-vs-任务)
- [shape / tensor / Ort::Value 三者关系](#shape--tensor--ortvalue-三者关系)
- [Env / Session / Engine 三者角色](#env--session--engine-三者角色)
- [Ort::Env 全局唯一（函数内 static 单例）](#ortenv-全局唯一函数内-static-单例)
- [零拷贝张量输入 + buffer 存活契约](#零拷贝张量输入--buffer-存活契约)
- [MemoryInfo 与 CPU 内存 / GPU 显存 / Host→Device](#memoryinfo-与-cpu-内存--gpu-显存--hostdevice)
- [I/O 元数据构造期一次性缓存](#io-元数据构造期一次性缓存)
- [Execution Provider 与优雅回退](#execution-provider-与优雅回退)
- [Softmax（数值稳定版）](#softmax数值稳定版)
- [Top-K 选择（partial_sort）](#top-k-选择partial_sort)
- [NCHW 四维含义（batch / channel / H / W）](#nchw-四维含义batch--channel--h--w)
- [检测术语小表（Ultralytics / score / thresh / anchor）](#检测术语小表ultralytics--score--thresh--anchor)
- [YOLOv8 检测头布局（无 objectness + 转置）](#yolov8-检测头布局无-objectness--转置)
- [NMS / IoU（逐类非极大值抑制）](#nms--iou逐类非极大值抑制)
- [IntraOp vs InterOp 线程](#intraop-vs-interop-线程)
- [IOBinding（绑定 I/O 复用缓冲）](#iobinding绑定-io-复用缓冲)
- [PTQ / MinMax / Entropy](#ptq--minmax--entropy)
- [对称/非对称 + Per-Tensor/Per-Channel](#对称非对称--per-tensorper-channel)
- [QDQ vs QOperator](#qdq-vs-qoperator)
- [量化 vs 剪枝 vs 蒸馏](#量化-vs-剪枝-vs-蒸馏)

---

## ONNX vs ORT vs 任务

**是什么**：ONNX 是**模型文件格式**（类比 `.mp4`），`.onnx` 里存网络结构 + 权重 + I/O 规格，本身不规定任务、也不会自己运行；ORT（ONNX Runtime）是**加载并执行 `.onnx` 的引擎**；图片分类/检测/语音等是**模型本身的任务**，由模型决定，与格式无关。

**为什么 / 何时用**：ONNX 是「开放神经网络**交换**」格式——PyTorch/TF 各自训练格式不通用，导出成 ONNX 后即可在任何支持 ORT 的引擎/硬件上跑，解耦框架与部署。链路：`ONNX(格式) → ORT(引擎执行) → 具体任务(分类/检测/…)`。

**坑**：别把三者混为一谈——不能说「ONNX 是用来做图片分类的」（就像不能说「mp4 是用来放电影的」）。分类是我们这次放进 ONNX 里的 MobileNetV2 干的；换 YOLO 就是检测、Whisper 就是语音。

> 实战出处：`02_Inference_Analysis/w14_ort_basics/notes.md`（概念澄清节 + 端到端数据流图）

---

## shape / tensor / Ort::Value 三者关系

**是什么**：
- `shape`：一串数字（如 `{1,3,224,224}`），只描述**数据怎么排布**，本身不含数值。
- `tensor`：数学**概念**——多维数组，= 数据(data) + shape + 数据类型(dtype) 三者合一。
- `Ort::Value`：tensor 在 ORT 里的 **C++ 容器实现**（落地的具体类型）。

**为什么 / 何时用**：`CreateTensor<float>(mem_info, data, size, shape, ...)` 干的事，就是把「数据 + shape + dtype」打包进一个 `Ort::Value`，得到一个能喂给 `Session::Run` 的 tensor。进出 ORT 的张量一律用 `Ort::Value` 表示（输入是它，`Run` 返回 `vector<Ort::Value>` 也是它）。

**坑**：data 在内存里是一维连续的，**shape 决定怎么"看"它**——同样 150528 个 float，配 `{1,3,224,224}` 是一张图，配 `{150528}` 是一根长向量。严格说 `Ort::Value` 是「通用值容器」，理论上也能装 map/sequence，但视觉模型 I/O 全是 tensor，可按「`Ort::Value` = tensor 的 C++ 实现」理解。

> 实战出处：`02_Inference_Analysis/w14_ort_basics/notes.md`（设计决策 2 + Run 热路径时序）

---

## Env / Session / Engine 三者角色

**是什么**：
- `Ort::Env`：进程级**运行环境**，持有日志器 + 内部线程池/分配器。是「环境」不是「模型」。
- `Ort::Session`：加载好的**一个模型** + 执行能力。构造时读 `.onnx`、按 EP/优化选项准备好；既能查 I/O 元数据，又能 `Run()` 把输入算成输出。**推理的真正执行者**。
- `InferenceEngine`：我们对 Session 的**易用封装**（RAII、元数据缓存、零拷贝、EP 回退）。

**为什么 / 何时用**：数量关系是关键——**Env 全进程 1 个（共享），Session 每模型 1 个，Engine 想要几个有几个**。同时跑 MobileNetV2 + YOLO = 2 个 Session、共用 1 个 Env。

**坑**：模型和算力都在 `Session` 里，`InferenceEngine` 只是壳。创建 Session 必须传 Env（`new Ort::Session(env, path, options)`），所以 Env 是 Session 的前提依赖。

> 实战出处：`02_Inference_Analysis/w14_ort_basics/notes.md`（生命周期时序图）

---

## Ort::Env 全局唯一（函数内 static 单例）

**是什么**：用「静态成员函数 + 函数内静态局部变量」叠出单例，保证全进程只有一个 `Ort::Env`：

```cpp
Ort::Env& InferenceEngine::GlobalEnv() {   // 静态成员函数：不依赖对象，类名:: 直接调
  static Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "EdgeAI-W14");  // 函数内 static：唯一+懒加载+线程安全
  return env;                              // 返回引用（非拷贝），拿到本体
}
```

**为什么 / 何时用**：Env 持有进程级资源，多份并存会重复分配数百 KB、日志交错混乱。函数内 static（C++11+）保证线程安全 + 仅初始化一次 + 程序退出自动析构，比全局变量安全、比手写单例精简。验证：`&GlobalEnv() == &GlobalEnv()` 地址相等。

**坑**：**必须在 `AppendExecutionProvider_CUDA` 之前先建 Env**——Env 构造时注册进程级默认日志器，而 append CUDA 内部要用这个日志器。若 Env 未建就 append，抛 "DefaultLogger but none registered" → CUDA 可用却误回退 CPU。单测因前序用例已建 Env 而侥幸通过（顺序依赖假绿），demo 单次构造才暴露真相。

> 实战出处：`02_Inference_Analysis/w14_ort_basics/notes.md`（设计决策 1 + W14.5 坑 2，commit 319d4ae）
> 关联：[static 四种用法](cpp-core.md#static-的四种用法)

---

## 零拷贝张量输入 + buffer 存活契约

**是什么**：`CreateTensor` 的语义是「**借用**外部 buffer」——只拿数据指针，不复制数据。tensor 内部指向调用方原始内存，与之共用同一块。

```cpp
Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
    mem_info, const_cast<float*>(input.data()), input.size(), shape.data(), shape.size());
//                          ↑ 只传指针，不拷贝那 600KB
```

**为什么 / 何时用**：省掉每次推理一整块 `memcpy`（MobileNetV2 输入约 600KB）。边缘 AI 内存紧、要高帧率，所以优先零拷贝 + `std::span`。

**坑**：既然只借指针、没留副本，**原始 buffer 必须在 `Run` 返回前一直存活、别动**，否则 tensor 指向已释放/改写的内存 = 悬空。典型错误：在函数里用局部 `vector` 建 tensor 然后 `return tensor`——局部 vector 出作用域销毁，tensor 悬空。`std::span<const float>` 表达「只读」承诺，内部 `const_cast` 仅为对接 C ABI（见 [ABI vs API](cpp-core.md#abi-vs-api)），推理路径实际只读。

> 实战出处：`02_Inference_Analysis/w14_ort_basics/notes.md`（设计决策 2 + Run 时序「span 必须存活」注）

---

## MemoryInfo 与 CPU 内存 / GPU 显存 / Host→Device

**是什么**：`mem_info` 是「内存身份标签」，告诉 ORT「待会给你的数据指针指向哪种内存」（CPU 普通内存 / GPU 显存 / 分配器类型）。`CreateCpu(...)` = 声明数据在 CPU。

**为什么 / 何时用**：零拷贝只给了裸指针，光看指针数值分不清 CPU/GPU。ORT 靠 `mem_info` 决定能否直接读、要不要做设备间拷贝。**Host = CPU（+ RAM），Device = GPU（+ 显存 VRAM）**，是两块物理上独立的内存；普通声明的变量（`vector`、栈、堆）全在 CPU，要放显存得 `cudaMalloc`（裸指针、手动管）。

**坑**：用 `--cuda` 时，我们的 `vector<float>` **仍在 CPU**——代码全程没碰 `cudaMalloc`，`mem_info` 一直是 `CreateCpu`。GPU 怎么算的？**ORT 内部偷偷 Host→Device 拷贝**（CPU→显存）→ 算 → Device→Host 拷回。这就是小模型上 GPU 只快 ~1.3× 的根因：搬运开销占比大，GPU 算力喂不饱。想省掉每次拷贝要自己 `cudaMalloc` 让数据常驻显存 + 用 GPU 版 MemoryInfo（W14/W15 未做）。

> 实战出处：`02_Inference_Analysis/w14_ort_basics/notes.md`（设计决策 2 + 闭环结果「GPU 只快 1.3×」注）

---

## I/O 元数据构造期一次性缓存

**是什么**：模型的输入/输出名字、形状、类型这些**加载即固定**的信息，构造期一次性问 Session 查出来，缓存进 `inputs_`/`outputs_`（`std::vector<IoInfo>`）；`Run` 热路径只读缓存。

**为什么 / 何时用**：`GetInputNameAllocated` 这类查询每次都走 ORT 内部分配（贵）。固定信息查一次足矣——用空间换时间，避免每次推理重复昂贵查询。一个 `IoInfo` = 一张「档案卡」（name + shape + dtype），vector 是「一摞卡」，天然支持 0/1/N 个 I/O（分类 1 个、BERT 2~3 个、YOLO 多输出）。

**坑**：保留一个**可忽略的小代价**——每次 `Run` 仍要把缓存的名字临时排成 ORT C API 要的 `const char*` 数组（两次微型 vector 分配）。相对毫秒级推理可忽略（差千倍量级），为代码简洁而保留，不值得为它再加一层缓存状态。

> 实战出处：`02_Inference_Analysis/w14_ort_basics/notes.md`（设计决策 3）

---

## Execution Provider 与优雅回退

**是什么**：EP（Execution Provider）= ORT 执行算子的「后端」，CPU EP / CUDA EP 等。我们的 `InferenceEngine` 加可选 `Ep` 参数（默认 `kCpu`），请求 CUDA 但环境不可用时**整段 Session 优雅回退 CPU**，不抛异常；`ActiveEp()` 查实际生效、`EpFallbackReason()` 查回退原因。

**为什么 / 何时用**：让同一份代码在「有 GPU / 纯 CPU」环境都能跑——CI、新机器、VPS 缺 GPU 时自动降级而非崩溃。EP 在**构造期**就定死（依赖链接的是 CPU 包还是 GPU 包），不是运行时 `--cuda` 开关能切。

**坑**：CPU 包（`onnxruntime-linux-x64`）不含 `libonnxruntime_providers_cuda.so`，用它跑 `--cuda` 必然回退（报 "Failed to load shared library"）。要真上 GPU 须用 **GPU 包**配置，且 build 目录配置/编译/运行三步前后一致。回退判断要 try 包住「append + Session 创建」整体（失败可能在 append，也可能在 cudnn/cublas 的 dlopen）。

**`ActiveEp()==CUDA` ≠ 整张图都在 GPU 跑**：它只表示 Session 成功**请求/注册**了 CUDA EP，不证明每个算子都落到 GPU。ORT 会**故意**把 shape 相关的小算子留在 CPU（搬到 GPU 反而更慢），运行时打印 `VerifyEachNodeIsAssignedToAnEp` 警告就是在说这件事——所以一张图常是 GPU/CPU 混合执行，逐算子的 kernel placement 严格确认是后续 profiling（W16/W18）的任务。这也解释了 W14.5 benchmark 实测 GPU 只比 CPU 快约 **1.5×**（min 1.49 vs 2.25ms）而非数量级：MobileNetV2 是小模型 + batch=1 算力需求低、有 H2D/D2H 搬运开销、且非全图上 GPU。结论：小模型/小 batch GPU 收益有限，换大模型/大 batch 才拉得开。

> 实战出处：`02_Inference_Analysis/w14_ort_basics/notes.md`（生命周期时序图 EP 分支 + 编译与运行节 + W14.5 坑）

---

## Softmax（数值稳定版）

**是什么**：把模型输出的一串裸分数（logits，可正可负、无上下界）换算成「概率分布」——每个值落在 0~1、全部加起来等于 1。公式 `softmax(x_i) = exp(x_i) / Σ exp(x_j)`。

**为什么 / 何时用**：分类模型最后一层输出的 logits 只有「相对大小」有意义（谁大谁更可能），数值本身不可读。softmax 把它变成「65% 像萨摩耶」这种可解释的概率，也方便取 Top-K 时附概率。

**坑**：必须用**数值稳定版**——先减去最大值再取 exp：

```cpp
const float max_v = *std::max_element(logits.begin(), logits.end());
out[i] = std::exp(logits[i] - max_v);   // 减 max 防 exp 溢出
```

直接 `exp(x)` 当 x 较大（如 88+）时 `float` 直接溢出成 `inf`，结果变 `nan`。减最大值后最大指数项变成 `exp(0)=1`，其余都 ≤1，不溢出；而分子分母同乘 `exp(-max)` 数学上恒等，概率值不变。空输入要先判空再取 `max_element`（否则解引用 end 迭代器 UB）。

> 实战出处：`02_Inference_Analysis/w15_classify_pipeline/notes.md`（后处理节，commit 251f70d）

---

## Top-K 选择（partial_sort）

**是什么**：从 N 个概率里挑出最大的 K 个并排好序（K 通常 ≪ N，如 1000 选 5）。用 `std::partial_sort` 而非全排序。

**为什么 / 何时用**：只要前 K 名时，全排序（`sort`，O(N log N)）是浪费——`partial_sort` 只保证前 K 个有序、其余乱序，复杂度 O(N log K)。1000 选 5 差出两个量级。做法是排「索引」而非排概率本身（避免丢失「第几类」这个信息）：

```cpp
std::vector<int> idx(probs.size());
std::iota(idx.begin(), idx.end(), 0);                  // idx = 0,1,2,...
std::partial_sort(idx.begin(), idx.begin() + k, idx.end(),
                  [&](int a, int b){ return probs[a] > probs[b]; });  // 按概率降序排索引
```

**坑**：`k` 要先 `std::min(k, N)` 夹一下——请求的 K 超过类别数时 `idx.begin()+k` 越界。比较器用 `>`（降序）才是「最大的 K 个」，写成 `<` 会取到最小的 K 个且不报错（silent bug）。

**argmax（Top-1）是 K=1 的退化**：只要最像的那一类时，不必 `partial_sort`，直接 `std::max_element` 找最大 logit 的位置、再用「指针差」得索引即可（`idx = max_it - data`），索引就是预测类别编号。注意两个认知点：① **比大小取 Top-1 不需要先 softmax**——softmax 不改变谁最大，只在要「概率」时才做。② **模型对输入只做机械前向**，不在乎喂的是真图还是噪声；W14 demo 喂固定种子随机数，所以 Top-1 索引**无现实语义**，只验证「推理链路通、输出能解析」——把「链路正确性」和「识别精度」解耦，是先搭骨架的工程节奏。

> 实战出处：`02_Inference_Analysis/w15_classify_pipeline/notes.md`（后处理节，commit 251f70d）；argmax 退化见 `w14_ort_basics/ort_basics_demo.cpp`（`max_element` + 指针差）

---

## NCHW 四维含义（batch / channel / H / W）

**是什么**：视觉模型输入张量 shape `{1, 3, 640, 640}` 是 **NCHW** 四维：**N**=batch（这次喂几张图）、**C**=channel（通道数，RGB=3）、**H**=height、**W**=width。第一个 `1` 就是 batch 维——一次推理 1 张图。

**为什么 / 何时用**：维度是模型 I/O 的固定契约，**哪怕只推一张也必须显式写出 batch 维**（不能因为「只有一张」就省成三维）。`1×3×640×640=1228800` 正好等于 `LetterboxToTensor` 产出的 float 数；那个 `1` 在连乘里不改变总数，但它告诉引擎「这坨数据是 **1 张**图的 3×640×640，batch 在最外层」。输出端同理要校验 `out_shape[0]==1`（喂 1 张要还 1 张）。

**坑**：① batch 是**可调旋钮**不是常量摆设——批量推理提吞吐时把 `1` 改成 `N` 并在最外层拼 N 张图的张量（W16 benchmark 的 batch 1v4 即此）。② N 在**最外层**：NCHW 内存里先排满第 0 张图的全部 CHW，再第 1 张；别和 NHWC（TF 默认，通道在最内）搞混。③ 输入 shape 与张量 buffer 元素数必须自洽，否则 ORT 报维度错。

> 实战出处：`02_Inference_Analysis/w16_yolo_detector/yolo_detector.cpp:36`（`shape{1,3,input,input}`）；HWC↔CHW 排布见 image-ops.md

---

## 检测术语小表（Ultralytics / score / thresh / anchor）

**Ultralytics**：YOLOv8 的主要维护方之一，也是 `ultralytics` Python 包 / CLI 的提供者。W16 用它做两件事：① 从 `yolov8n.pt` 导出 `yolov8n.onnx` + `coco_classes.txt`；② 生成 Python 侧参考检测结果，与 C++ 端逐框对拍。注意授权：Ultralytics YOLOv8 代码和权重走 AGPL-3.0 / Enterprise 双授权，学习 demo 没问题，闭源商用要单独评估。

**score / confidence / detection score**：检测分数、置信度。它不是 bbox 坐标，而是“这个候选框作为某类目标有多可信”。在 W16 的 YOLOv8 中，`score = 80 个类别分数里的最大值`，`class_id = 最大分数对应的类别`。因为 YOLOv8 无 objectness，所以这里不是 YOLOv5 常见的 `objectness * class_prob`。

**thresh / threshold**：阈值。代码里常写缩写 `conf_thresh`、`iou_thresh`。两个阈值方向不同：`score < conf_thresh` 表示模型不够自信，候选框直接丢；`IoU > iou_thresh` 表示两个同类框重叠太高，NMS 会抑制低分框。

**anchor-based / anchor-free**：anchor-based 检测器会在每个位置预设若干默认框（anchor boxes），模型预测“这个默认框该平移/缩放多少 + 是什么类”；YOLOv8 是 anchor-free，更接近每个预测点直接输出 `cx, cy, w, h` 和类别分数。因此 W16 里 `[1,84,8400]` 的 `8400` 更严谨叫 **候选预测数 / prediction points**，不要按旧 YOLO 习惯说成“8400 个 anchor 框”。代码变量里保留 `num_anchors` 是检测代码里的历史命名习惯。

> 实战出处：`02_Inference_Analysis/w16_yolo_detector/tools/export_yolov8n.py`（Ultralytics 导出）；`decode.cpp`（score/conf 阈值 + anchor-free 输出解析）；`nms.cpp`（IoU 阈值）

---

## YOLOv8 检测头布局（无 objectness + 转置）

**是什么**：YOLOv8 检测模型的单输出头形状是 `[1, C, A]`，其中 `C = 4 + num_classes`（COCO 为 84 = 4+80）、`A = 候选预测数`（640 输入对应 8400）。前 4 通道是 `cx, cy, w, h`（letterbox 输入像素坐标），后 `num_classes` 通道是各类别概率（导出时已含 sigmoid）。每个候选预测的 score = 这 80 个类概率的最大值，对应类别即 argmax。

**为什么 / 何时用**：解析检测头是把「一坨 float」变成「框」的第一步。两个易被旧经验带偏的点：① **YOLOv8 没有 objectness**——v5/v3 的输出每个 anchor 多一维「有无物体」置信度，score = obj × cls；v8 取消了 obj，score 直接取类概率最大值。套 v5 的「乘 objectness」公式会多乘一个不存在的维度。② **布局是 channels-major**：内存里同一通道的 8400 个候选预测连续排（`out[ch*A + a]`），不是「每个候选预测的 84 个值连续」。解码要按这个 stride 转置遍历。

**坑**：`num_classes` 别写死 80——从输出形状反推 `shape[1] - 4`，换模型才不崩。conf 阈值过滤要在「取完 max 类概率」之后做。框坐标此时还是 letterbox 坐标系，**必须再做坐标反算**回原图（见 image-ops.md「Letterbox + 坐标反算」）。**conf 筛的是「确定程度」不是「类别」**——conf = 80 类里最高那个分（v8 无 objectness，「有没有东西」与「是哪类」合一），低于阈值=模型自己没把握，一律刷掉，与具体是人是车无关；类别信息要到下游**逐类 NMS** 才登场。常见误解：以为 conf 阈值在挑保留哪些类别。

> 实战出处：`02_Inference_Analysis/w16_yolo_detector/notes.md`（`decode.{hpp,cpp}`，合成张量单测）

---

## NMS / IoU（逐类非极大值抑制）

**是什么**：检测模型对同一目标会吐出多个高度重叠的框，NMS（Non-Maximum Suppression）按 score 降序贪心保留最高分框、抑制与它 IoU 超阈值的同类框。IoU（Intersection over Union）= 两框交集面积 / 并集面积，是「重叠程度」的标准度量。

**为什么 / 何时用**：anchor-based 检测器天然冗余输出，不去重的话一个人会画出十几个框。NMS 是检测后处理的标配。**逐类**很关键：只在同 `class_id` 内部抑制——同一位置可以同时是「人」和「背包」，跨类抑制会把正确的重叠目标误删。

**原理（为什么这套度量+流程有效）**：
- **IoU 为何除以并集**：光看「交集面积」会被框大小骗——两个小框重叠 50 像素几乎是同一物体，一大一小框重叠 50 像素却是九牛一毛。除以「并集面积」做归一化，结果与框尺寸无关，0.8 就是 0.8 的重叠程度，才有统一尺子。
- **NMS 为何「排序+删高 IoU」就能去重**：建立在两个假设上——① 一个真实目标会被相邻格子重复预测，这些框**彼此高 IoU 抱团**（→高 IoU = 同一物体的重复）；② 一堆重复框里**自信分最高的框得最准**（→从它当基准、其余是劣质副本）。两假设合起来，「从最高分开始、删与它高 IoU 的同类框」恰好就是「每个物体只留一个最好框」，且删掉的必是冗余、不会误伤别的物体（否则早被前面的基准 IoU 判定删了）。

**坑**：① IoU 算交集时，宽/高要 `max(0, ...)` 夹一下，否则无交集时负宽×负高会得正面积（假 IoU）。② 阈值方向：抑制的是 IoU **大于**阈值的（重叠太多 = 同一目标）；iou_thresh 越小抑制越狠。③ 性能：朴素逐类 O(n²) 在过完 conf 阈值后通常只剩数十~数百框，足够快（1000 框 stress test <1ms@Release）；真要更快可用空间分桶，但别过早优化。④ conf 阈值（先筛掉低分候选）和 iou 阈值（NMS 去重）是两个独立旋钮，别混。

> 实战出处：`02_Inference_Analysis/w16_yolo_detector/notes.md`（`nms.{hpp,cpp}`，IoU 手算对拍 + 逐类 + stress 单测）

---

## IntraOp vs InterOp 线程

**是什么**：ORT `SessionOptions` 的两个线程旋钮。**IntraOp**（`SetIntraOpNumThreads`）= 单个算子**内部**的并行度，比如一个大 MatMul/Conv 切多线程一起算。**InterOp**（`SetInterOpNumThreads`）= 计算图里**多个算子之间**的并行度，仅当图有并行分支（同时能跑的独立节点）时才有意义。

**为什么 / 何时用**：CNN/检测网络是近乎串行的链（一层喂下一层），并行机会几乎全在「单个算子内部」——所以 IntraOp 是主旋钮，InterOp 对 YOLOv8 这种串行图基本无收益。实测 yolov8n CPU batch=1：IntraOp 1→2 线程提速 1.6×、1→4 提速 2.0×（W16 bench）。

**坑**：① **次线性扩展**——加到 CPU 核数不会线性加速，受算子并行度上限和内存带宽限制，过了拐点甚至变慢（线程调度/缓存争用）。② 别盲目把 InterOp 调大期待加速，串行图上它只增开销。③ 0 = 用 ORT 默认（通常 = 物理核数），不是「禁用线程」。④ 线程数要结合部署环境定：边缘多进程共享 CPU 时，单 session 吃满核反而拖累整体。

> 实战出处：`docs/benchmarks/w16_yolo_bench.md`（IntraOp 扫描）；机制 `w14_ort_basics/inference_engine.{hpp,cpp}`（`SessionConfig`）

---

## IOBinding（绑定 I/O 复用缓冲）

**是什么**：`Ort::IoBinding` 把输入/输出张量**预先绑定**到固定缓冲，跨多次 `Run` 复用，替代「每次 Run 现搭 I/O 名称数组 + 让 ORT 现分配输出 `Ort::Value`」。CUDA EP 下还能把输出绑定到设备/复用的主机缓冲，减少每次 Device→Host 拷贝的分配抖动。

**为什么 / 何时用**：理论上高频小模型推理里「每次 Run 的输出分配 + D2H 拷贝」这类**固定开销**占比可观，绑定复用把它摊掉。但**实测要诚实**：yolov8n CUDA batch=1 多次 run，IOBinding 与 Run 的 P50 在 5.5~6.1ms 间**优劣翻转**，差异进了噪声——因为模型小、且输出仍需拷回 CPU 解码。结论：对这类场景 IOBinding **不是稳定优化项**；真要量化收益得上更大模型，或用 Nsight 逐段归因。batch 越大计算占比越高、收益越被淹没；CPU EP 无 D2H 拷贝更无差。

**坑**：① **持久输出绑定假定输出形状固定**——绑定一次复用，换 batch / 输入尺寸（输出形状变）必须重建 binding，否则 ORT 抛 `OrtValue shape verification failed`（W16 benchmark 踩过：一个 engine 跨 batch=1/4 复用 binding 直接崩，改成每个 batch 独立 engine）。② 不是银弹——计算密集时固定开销占比小，IOBinding 收益进噪声。③ 输入仍可零拷贝绑定，但输入 buffer 每次不同要重绑（`ClearBoundInputs` + `BindInput`）。

> 实战出处：`docs/benchmarks/w16_yolo_bench.md`（Run vs IOBinding）；机制 `w14_ort_basics/inference_engine.cpp`（`RunIoBinding`，持久 binding + 形状契约）

---

## PTQ / MinMax / Entropy

**是什么**：PTQ（Post-Training Quantization）是在训练完成后，把 FP32 权重/激活映射到 INT8 等低精度表示的部署优化。Static PTQ 需要校准数据跑一遍模型，统计激活范围；MinMax 直接取观测到的最小/最大值，Entropy 用 KL/直方图方法选阈值，试图在截断异常值与保留分布细节之间折中。

**为什么 / 何时用**：不重训、成本低，适合先验证端侧延迟/体积收益。YOLOv8n 在 CPU ORT 上实测 INT8 纯 infer P50 从约 40ms 降到 18ms，模型从 13M 降到 3.8M，说明工程链路有价值。

**坑**：校准数据决定激活范围。单张图只能验证工具链，不能代表真实分布；本项目第一版 INT8 模型虽然更快，但单图检测输出变成 0 框，说明 PTQ 精度失败，不能写成 mAP 结论。动态 shape 的 YOLO 导出模型在 ORT `quant_pre_process` 里可能 symbolic shape 推导失败，需要跳过 symbolic shape、保留普通 shape inference。

> 实战出处：`02_Inference_Analysis/quantization/tools/quantize_yolov8_static.py`；`docs/benchmarks/quant_int8_report.md`

## 对称/非对称 + Per-Tensor/Per-Channel

**是什么**：对称量化用 0 作为实数零点，常见公式是 `real ~= scale * int8`；非对称量化多一个 `zero_point`，公式是 `real ~= scale * (uint8 - zero_point)`，能覆盖非零中心分布。Per-Tensor 是整个张量共享一个 scale/zero_point；Per-Channel 是每个输出通道一套量化参数。

**为什么 / 何时用**：权重常用对称 + per-channel，因为卷积不同输出通道分布差异大，分通道能减少量化误差；激活常用非对称 per-tensor，因为运行时范围随输入变，硬件/算子支持也更统一。

**坑**：量化参数越细，精度越好但算子支持和部署复杂度越高。不是所有后端都支持任意 per-channel 激活量化；要以 ORT/TensorRT/目标硬件支持矩阵为准。调量化策略时必须同步记录精度口径，否则只看延迟会把不可用模型当优化成功。

> 实战出处：`02_Inference_Analysis/quantization/tools/quantize_yolov8_static.py`（QDQ static PTQ，weight per-channel）

## QDQ vs QOperator

**是什么**：QDQ 格式在图里显式插入 `QuantizeLinear` / `DequantizeLinear` 节点，让原算子周围标注量化边界；QOperator 格式把算子替换成量化算子（如 `QLinearConv`）。

**为什么 / 何时用**：QDQ 更适合现代后端优化器识别和融合，也更容易保留原图结构，便于调试哪些张量被量化。ORT static quantization 推荐优先用 QDQ，TensorRT 等后端也更容易从 QDQ 图做融合。

**坑**：QDQ 图不代表每个节点都真的用 INT8 kernel 跑，最终仍要看 EP 支持和 runtime placement。若某些算子不支持 INT8，后端可能插回 dequant 或落回 FP32，延迟收益会打折；需要 benchmark 和 profiling 证明。

> 实战出处：`02_Inference_Analysis/quantization/tools/quantize_yolov8_static.py`（`QuantFormat.QDQ`）

## 量化 vs 剪枝 vs 蒸馏

**是什么**：量化降低数值精度（FP32→INT8/INT4），主要减小模型体积和算力/带宽；剪枝删除不重要的权重、通道或结构，目标是减少实际计算图规模；蒸馏用大模型指导小模型训练，让小模型学到更好的输出分布。

**为什么 / 何时用**：三者经常同时出现在部署 JD 里，但解决的问题不同。量化是部署后处理，最容易接现有 ONNX/ORT/TRT 流水线；剪枝通常需要训练/微调和结构化约束；蒸馏更偏训练策略，用来得到本来就更小的学生模型。

**坑**：别把“模型变小”都叫量化。量化不改变层数和通道数，剪枝会改变结构或稀疏性，蒸馏产出的是另一个模型。面试表达时要说清优化对象、是否需要训练、部署后端是否真能利用。

> 实战出处：`docs/Roadmap.md` 的 `quant` 交付物只实现 PTQ；剪枝/蒸馏本阶段作概念覆盖。
