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

> 实战出处：`02_Inference_Analysis/w14_ort_basics/notes.md`（生命周期时序图 EP 分支 + 编译与运行节 + W14.5 坑）
