# 开发日志

> 记录每日操作、有效命令、踩坑解决方案和环境变更。
> 日期倒序（最新在最上方）。
> 平时在「草稿区」随手记，定期说"帮我整理一下 devlog 草稿"让 Claude 归类整理。

---

## 草稿区

<!-- 在这里随手记零散内容，不用管格式 -->

---

## 2026-07-01（W16 检测 demo 源码精读 — 预处理→推理→decode→NMS→可视化全链路答疑）

### 操作摘要
- 逐段精读 W16 YOLOv8 检测 demo：命名空间结构 → `LetterboxToTensor` 预处理 → `Detect` 推理（含 W14 `InferenceEngine::Run` 零拷贝）→ `DecodeYolov8` 解码反算 → `Nms` 去重 → demo 可视化输出；跑通 demo（CUDA .so 缺失自动回退 CPU）
- **无代码改动，纯源码精读 + 一次 demo 实操**；本条沉淀今日澄清的 6 处理解短板
- 沉淀产物：`docs/notes/image-ops.md`（新增「图像坐标系 y 朝下 + cxcywh↔xyxy + clamp」「cv::imwrite 相对路径落 CWD」）、`docs/notes/cpp-core.md`（新增「匿名 vs 具名命名空间」「默认实参 + 透传」）、`docs/notes/inference.md`（新增「NCHW 四维含义」）、`docs/interview_faq.md` Q43-Q46（答题角度 + 链接，含 YOLO AGPL 授权）、`docs/README.md` FAQ 计数 42→46、`docs/notes/README.md` 索引

### 今日深讲内容
- **命名空间**：detector 用具名 `w16`（库代码，名字 = API 契约，hpp/cpp 同名是同一个分两处写），demo 用匿名 `namespace {}`（main 叶子文件，藏 `LoadLabels`，内部链接 = 文件级 static，防链接冲突）
- **LetterboxToTensor（V4）链路**：Letterbox(等比缩放+灰边) → ÷255 → HWC→CHW；灰边 `(114,114,114)` 来自**默认实参**，经 `LetterboxToTensor→Letterbox→copyMakeBorder` 三层透传；info{scale,pad_left,pad_top} 是坐标反算的钥匙
- **推理两层**：detector 只转交（`engine_.Run` 拿裸指针+shape）；engine 真干活 = 零拷贝 `CreateTensor`(借用 buffer，`const_cast` 对接 C ABID 只读) → 按名指定 I/O → `session_->Run` 跑网络
- **shape `{1,3,640,640}` = NCHW**：第一个 1 = batch 维，一次推一张也必须显式写；输出 `[1,C,A]`：C=4+类别数=84、A=anchor 数=8400、channels-major（`out[ch*A+a]` 跨步取）
- **DecodeYolov8 四步**：取每 anchor 最高类分(v8 无 objectness) → conf 筛 → cxcywh→xyxy → `(lb-pad)/scale` 反算 + clamp 夹边界
- **NMS**：score 降序贪心，删与基准框 IoU>阈值的**同类**框；IoU 算交集负宽高要 `max(0,...)`

### 今日暴露的短板 / 困惑（已提成 FAQ Q43-Q46）
- **图像坐标系 y 朝下（最强短板）**：直觉以为左上角 y 该用加法；实则图像 y 向下增大，左上角 y = `cy - h/2`（减法）。x 左右符直觉，唯 y 上下翻转 → **Q43**
- **匿名 vs 具名命名空间**「为啥不一样」：库 vs 入口、API 契约 vs 内部链接 → **Q44**
- **NCHW 第一个 1**「还是不懂」：batch 维，哪怕一张也不能省 → **Q45**
- **`[1,C,A]` 是啥**：已在主题库 YOLOv8 检测头小节覆盖，今日补 NCHW 维 + 链接
- **没传 pad_color 哪来的灰边**：默认实参 + 三层透传（cpp-core 新概念）
- **YOLO 授权**：yolov8n 是 Ultralytics AGPL-3.0，免费但传染性强，闭源商用需买授权或换 YOLOX(Apache) → **Q46**

### 实操记录
- 跑 `w16_yolo_demo`：CUDA provider .so 缺失 → ORT 报 `Failed to load libonnxruntime_providers_cuda.so` → **自动回退 EP=CPU**，检测正常完成（3 person + 1 bus + 1 person，conf 0.436~0.890），存图成功
- **找不到输出文件的真相**：`cv::imwrite("w16_output.jpg")` 是相对路径，落在**运行命令的 CWD**（仓库根 `~/code/Edge-AI-Genesis-2026/`）而非 binary 目录；文件实际已生成（353KB，时间戳对得上）

### 待办
- 进入 `quant`（Phase 0）：INT8 量化 + 部署硬化 + Profiling 报告（逐算子 kernel placement 在此深挖）
- （承前）ResNet18 对比、VPS CPU EP 环境

### 关联
- W16 模块 notes：`02_Inference_Analysis/w16_yolo_detector/notes.md`
- 概念正文（唯一事实源）：`docs/notes/image-ops.md`（坐标系/imwrite）、`docs/notes/cpp-core.md`（命名空间/默认实参）、`docs/notes/inference.md`（NCHW）
- 答题视角：`docs/interview_faq.md` Q43-Q46

---

## 2026-06-22（Session 5 — W14 源码精读 + benchmark 实操，W15 启动前）

### 操作摘要
- 复盘 W14：逐段精读 `InferenceEngine` 构造函数、`ort_basics_demo.cpp`、`inference_benchmark.cpp`；跑通 demo + CPU/GPU benchmark 对比；为进入 W15 做知识体检
- **无代码改动，纯复习 + 实操**；本条主要沉淀今日暴露的三处理解短板
- **附带修复 study-log 技能与主题库「单一事实源」冲突**：原 study-log 联动只写 FAQ、漏主题库，违反 CLAUDE.md「可复用概念首次即入库主题库」。改为三级路由（先判可复用→主题库唯一正文→FAQ 只留答题角度+链接），并回填今天三概念正文进主题库、FAQ 瘦身
- 沉淀产物：`docs/notes/inference.md`（EP 加 ActiveEp≠全图、Top-K 加 argmax 退化）、`docs/notes/cpp-core.md`（新增 size_t 小节）、`docs/notes/README.md` 索引、`docs/interview_faq.md` Q40-Q42（瘦身为答题角度+链接）、`docs/README.md` FAQ 计数 39→42、`.claude/skills/study-log/SKILL.md`（第 3 步三级路由 + Common Mistakes）

### 今日深讲内容（构造函数 + demo 数据流）
- **构造函数四阶段**：① 路径预检（`filesystem::exists` 提前给友好错误）→ ② 全局 `Ort::Env`（**必须先于 CUDA append**，否则默认日志器未注册抛 "DefaultLogger but none registered"，CUDA 可用却被误判回退；该 bug 只在进程内首次构造复现，单测因前序用例已建 Env 而假绿）→ ③ EP **两级优雅回退**（CUDA 失败静默记 `ep_fallback_reason_` 降级，CPU 保底也失败才真抛 `runtime_error`）→ ④ I/O 元数据一次性缓存
- **name 缓存 vs typeinfo(shape/dtype) 缓存分工**：`name` 给 `Run()` 按名指定喂/取哪个口（ORT Run API 强制要 `const char*` 名字数组）；`shape`/`dtype` 给调用方做输入准备 + 校验 + 输出解析；都是热路径外缓存，避免 `Run` 反复触发 ORT 内部分配
- **demo 数据驱动推导链（133-135）**：声明形状 `{-1,3,224,224}` →`ResolveDynamicShape` 动态维消 1→`ElementCount` 连乘 150528→`vector<float>` 分配连续 buffer；换输入尺寸不同的模型零改动
- **输出解析（152-170）**：`Ort::Value&` 引用避免拷贝 → 取 `GetTensorData<float>()` 裸指针 + shape + count → `max_element` + 指针差做 argmax → 打印 Top-1；整段被 `:102` 的 try/catch 罩住，退出码 0(help)/1(运行失败)/2(参数错) 分语义

### 今日暴露的短板 / 困惑（已提成 FAQ Q40-Q42）
- **分类输出语义不清（重点短板）**：起初不理解「1000 个 logit」= ImageNet 1000 类的原始得分；模型对输入只做机械前向，不在乎真图还是噪声；argmax 取最大 logit 的索引 = 预测类别。本 demo 喂固定种子随机数，故 Top-1=892 **无现实语义**，只验证链路通。logit 未过 softmax——只为比大小找 Top-1 不需要 softmax → **Q40**
- **`static_cast<size_t>` 必要性**：71 行 `int64_t`→`size_t` 不只是风格——消 signed→unsigned 隐式转换告警（CI 零容忍）、对齐 vector/size 类型、跨平台计算一致；负维安全性靠上游 `ResolveDynamicShape` 已消负维这个**隐式契约**兜底 → **Q41**
- **`ActiveEp()==CUDA` ≠ 全图在 GPU**：benchmark 那条 `VerifyEachNodeIsAssignedToAnEp` 黄色警告印证 `.hpp:52-53` 注释——ORT 故意把 shape 类小算子留 CPU；逐算子 kernel placement 是 W16/W18 profiling 的事 → **Q42**

### 实操记录
- **demo（CPU）**：MobileNetV2 输入 `[-1,3,224,224]`→解析 `[1,3,224,224]` 150528 元素 → 输出 `[1,1000]` → Top-1 索引 892 (logit=5.2391，随机输入无语义，固定种子可复现)
- **benchmark CPU vs GPU**（warmup 20 / iters 100，取 min 最接近真实算力）：
  - CPU：avg 2.57ms / **min 2.25** / max 3.11
  - CUDA：avg 1.98ms / **min 1.49** / max 4.50
  - 结论：GPU≈**1.5× CPU**，并非数量级提升。原因：MobileNetV2 小模型 + batch=1 算力需求低、H2D/D2H 搬运开销、非全图上 GPU；GPU 的 max 抖动反而更大（偶发 kernel 调度/同步）

### 命令备忘
```bash
# demo（CPU 构建，从仓库根跑，默认模型路径相对根目录）
./build/02_Inference_Analysis/w14_ort_basics/w14_ort_basics_demo
# benchmark CPU vs GPU 对比（GPU 用 build-gpu + --cuda）
./build/02_Inference_Analysis/w14_ort_basics/w14_inference_benchmark
./build-gpu/02_Inference_Analysis/w14_ort_basics/w14_inference_benchmark --cuda
```

### 待办
- **进入 W15（分类推理端到端闭环）**：真实图片预处理（resize/归一化）+ ImageNet 标签映射，把今天「Top-1 索引无语义」补成有语义的类别名；前后处理也会用上构造期缓存的 shape/dtype
- （承前）ResNet18 对比、VPS CPU EP 环境

### 关联
- W14 模块 notes：`02_Inference_Analysis/w14_ort_basics/notes.md`
- 概念正文（唯一事实源）：`docs/notes/inference.md`（EP/Top-K）、`docs/notes/cpp-core.md`（size_t）
- 答题视角：`docs/interview_faq.md` Q40-Q42（已链接主题库，不含概念正文）
- 关联前序：本日志 2026-05-25 条（W14 闭环落地）

---

## 2026-05-25（Session 4 — W14 ONNX Runtime 闭环）

### 操作摘要
- W14 启动，Q2 第一周：在 `02_Inference_Analysis/w14_ort_basics/` 落地 `InferenceEngine` 类 + demo + 5 个 GTest 单测
- 走 **B 路径（本地 CPU 单环境）**：暂跳 CUDA 全栈安装，先把代码闭环跑通；CUDA EP 推至 W14.5 / W15
- 模型范围裁剪：只跑 MobileNetV2，ResNet18 留到装 torch 时一并补
- 沉淀产物：`02_Inference_Analysis/w14_ort_basics/{inference_engine.hpp,.cpp,ort_basics_demo.cpp,inference_engine_test.cpp,CMakeLists.txt,notes.md,models/README.md}`、根 `CMakeLists.txt` 追加 Q2 子目录、`.gitignore` 加 ORT / 模型排除、`README.md` 补 ORT 依赖与 Q2 进度表、`CLAUDE.md` 当前进度更新

### 实操记录
- ORT 1.26.0 CPU 预编译包（8.2MB tar → 22MB so）解到 `third_party/onnxruntime/`，CMake 用手工 IMPORTED 目标接入（绕过上游 packaging bug，见下）
- demo 跑通：MobileNetV2 输入 `[-1,3,224,224] float32` → 解析动态 batch 为 1 → 150528 元素随机输入 → 输出 `[1,1000]` → Top-1 索引 892 (logit=5.2391，随机输入下无 ImageNet 语义)
- 5 个单测全绿（ctest 0.13s 完成）：LoadsModel / QueriesIoMetadata / RunsZeroCopyInference / FailsOnMissingModel / EnvIsSingleton
- 错误路径手测：`--model /tmp/不存在.onnx` 退出码 1，错误消息 `[W14] 模型文件不存在: ...`，符合 RAII + `std::runtime_error` 设计

### 今日深讲内容
- **`Ort::Env` 全局唯一的工程根因**：Env 持有进程级日志器 + 内部线程池资源，多份会重复分配数百 KB 且日志交错；函数内 `static Ort::Env env(...)` 拿到 C++11+ 线程安全初始化 + 程序退出有序析构，比全局变量稳、比手工单例精简；测试用 `&GlobalEnv() == &GlobalEnv()` 地址相等做反证
- **零拷贝输入的 C ABI 边界**：`CreateTensorWithDataAsOrtValue` 借用外部 buffer 不复制；`std::span<const float>` 对调用方承诺只读，内部 `const_cast<float*>(span.data())` 是 C ABI 兼容（ORT 推理路径事实只读）；调用方必须保证 buffer 存活到 `Run` 返回（测试里显式控制 `std::vector<float>` 生命周期来验证这个约束）
- **I/O 元数据热路径外缓存**：`GetInputNameAllocated` / `GetInputTypeInfo` 都走 ORT 内部分配，构造期一次性查完缓存到 `std::vector<IoInfo>`；`Run` 仍要现搭一次 `std::vector<const char*>`，相对毫秒推理可忽略，换 API 简洁度值得

### 踩坑
- **ORT 1.26 `onnxruntimeConfig.cmake` packaging bug**：导出文件硬编码 `lib64/`，实际 tarball 是 `lib/`，`find_package(... CONFIG REQUIRED)` 直接报"installation package was faulty"。绕开 find_package，改用手工 `add_library(... SHARED IMPORTED)` + `set_target_properties(... IMPORTED_LOCATION ... INTERFACE_INCLUDE_DIRECTORIES ...)`，既显式也不受上游修复节奏影响
- **版本查询 API 名误记**：最初写 `Ort::GetApi().GetVersionString()` —— `OrtApi` 没这个成员；正确是顶层 `Ort::GetVersionString()` 返回 `std::string`。教训：1.26 头里 `grep -nE "Signature"` 直接确认签名再写，比靠记忆稳
- **RPATH 缺失会强制 `LD_LIBRARY_PATH`**：ORT 在 `third_party/` 不在系统库路径，`set_target_properties(... BUILD_RPATH "${ONNXRUNTIME_ROOT}/lib")` 让二进制直接知道库位置，免去跑 demo / test 都要 export

### 命令备忘

```bash
# ORT CPU 包一次性下载（步骤 2）
mkdir -p third_party/onnxruntime && cd third_party/onnxruntime
curl -L -O https://github.com/microsoft/onnxruntime/releases/download/v1.26.0/onnxruntime-linux-x64-1.26.0.tgz
tar xzf onnxruntime-linux-x64-1.26.0.tgz && rm onnxruntime-linux-x64-1.26.0.tgz

# MobileNetV2 ONNX 直链下载（步骤 4，14MB）
curl -fLo 02_Inference_Analysis/w14_ort_basics/models/mobilenetv2.onnx \
  https://github.com/onnx/models/raw/main/validated/vision/classification/mobilenet/model/mobilenetv2-12.onnx

# 跑 demo / test
cmake --build build --target w14_ort_basics_demo w14_inference_engine_test -j$(nproc)
./build/02_Inference_Analysis/w14_ort_basics/w14_ort_basics_demo
ctest --test-dir build -R "W14_" --output-on-failure
```

### 待办
- **W14.5 / W15 起手补 CUDA**：装 CUDA Toolkit 12.x for WSL（`wsl-ubuntu` 仓库，非普通 Linux 仓库）+ cuDNN 9 + 下 `onnxruntime-linux-x64-gpu-1.26.0.tgz`；加 `LoadsModelWithCudaEp` 单测
- **ResNet18 对比**：装 torch（~800MB）后 `torchvision.models.resnet18().eval()` → `torch.onnx.export(... opset=17)`；InferenceEngine 已经支持任意 ONNX，加测试用例即可
- **VPS CPU EP 环境**：W15 起手做，与本地 CPU 路径配置对比沉淀到 README 双环境节
- **W15 主线**：前后处理流水线（HWC2CHW + Normalize + Top-K），复用 W9/W10 的 BGR2Gray / Resize / Letterbox 模块

### 关联
- W14 模块代码：`02_Inference_Analysis/w14_ort_basics/`
- W14 技术笔记：`02_Inference_Analysis/w14_ort_basics/notes.md`
- Q2 路线规格：`docs/Q2.md` 行 84-116
- 上一次 study-log：devlog.md 2026-05-25 Session 3（同日 W10/W9 Q1 review）

---

## 2026-05-25

### 操作摘要
- Session 3（W10 Resize 数学 + W9 mdspan / cv::Mat 内存模型）—— Q1 review 收尾
- 沉淀产物：`docs/interview_faq.md` 新增 Q36-Q39，`docs/q1_self_test.md` 历史成绩 / A9 / A10 同步，`docs/README.md` FAQ 计数同步至 39 道

### 今日深讲内容
- **W10 双线性 4 邻权重**：两步线性插值（先水平合并、后垂直合并）等价于 4 邻加权和、权重和恒为 1；记忆诀窍"对角点权重 = 自己到对方两方向距离的乘积"
- **Letterbox + bbox 反推**：长边定 scale → 短边补 pad → 推理 → 反推时"先减单边 pad、再除 scale"；99% 翻车在符号反 或 单边 pad 写成总 pad
- **Asymmetric vs HalfPixel 坐标对齐**：两套世界观（像素 = 网格交点 vs 像素 = 带中心方格），1920→640 差 1 列像素（src[1917] vs src[1918]）；PyTorch / OpenCV / TRT / 新 ONNX 默认 HalfPixel；导出 ONNX 显式设 `coordinate_transformation_mode` 防默认值漂
- **cv::Mat 内存模型**：`step[0]` 行字节宽（含 padding），不恒等于 `cols * elemSize()`；isContinuous() 不恒 true 来自两条——行对齐 padding 和 ROI（事实 padding = 有效内容 < stride）；铁律"永远用 `step[0]` 和 `ptr<T>(row)`"
- **std::mdspan**：多维非拥有 view，零开销抽象；HWC ↔ CHW 转换；**Debug 模式 mdspan 退化 >1000×**（V3 0.5ms → 560ms），零开销的前提是必须开优化

### 自测结果
6 题：
- W10 Q1 邻权重 ✅ 数值对（用了 (row,col) 命名约定，和 (x,y) 不同但等价）
- W10 Q2 Letterbox 参数 ✅ 总 pad 算对（416 − 416/800·600 = 104）
- W10 Q3 bbox 反推 ❌ y 方向写成 `(200 + 104)/scale`——符号反 + 用总 pad 而非单边 pad（应为 `(200 − 52)/scale = 284.6`）
- W9 Q1 step[0] ✅ 思路对（"不确定，看是否对齐"），具体到 1920 是 5760 / continuous
- W9 Q2 roi 不连续 ✅ 抓到根因（roi 继承父 step[0]，事实上的 padding）
- W9 Q3 mdspan shift ⚠️ 语法对、4 bugs：(a) typo `x = dx` 应为 `x + dx`，(b) if/else 分支完全一致，**漏 0-fill**——题目核心要求，(c) 边界检查方向反（应判 src 是否越界，不是 dst），(d) 原地读写会污染源像素，应用独立 dst buffer
- **短板暴露**：边界处理意识（bbox 漏 pad、shift 漏 0-fill），mdspan 语法 OK 但工程细节差一截

### 待办
- 进 W14（ONNX Runtime 集成）—— Q1 review 收尾完成
- 短板补强：W14 接 ONNX preprocess 时主动复测 bbox 反推 + 边界 0-fill + Resize 模式对齐
- 后续若有空：W10 自定义 Resize 同时实现 Asymmetric 和 HalfPixel，与 ONNX 算子做精确对齐验证

### 关联
- 深讲补充题：interview_faq.md Q36-Q39
- 自测题库：q1_self_test.md A9 / A10
- 上一次 study-log：devlog.md 2026-05-21

---

## 2026-05-21

### 操作摘要
- Session 2 下半场（W11 调试三件套深讲 + 三项实操）—— Session 2 收口
- 新建 `study-log` skill（`.claude/skills/study-log/`）：每日学习总结入库工作流，本条目即其第一次试跑产出
- 沉淀产物：`interview_faq.md` 新增 Q33-Q35，`q1_self_test.md` / `README.md` 同步更新

### 今日深讲内容
- **Valgrind 四种泄漏**：按"退出时还能否 reach"判定 —— definitely（必修）/ indirectly（被连累，修根即消）/ possibly（指针指中间，人工查）/ still reachable（长命服务 RSS 涨时才是真凶）；长服务该用 massif 而非 memcheck
- **GDB attach 抓死锁**：`CPU 0%` = 死锁或 IO；`gdb -p PID -batch -ex "info threads" -ex "thread apply all bt"`；栈顶 `__lll_lock_wait`/`futex_wait` = 等锁，`read`/`recv` = IO
- **perf + 火焰图**：stat 答"快不快"、record 答"慢在哪"；火焰图横轴 = 采样占比（**不是时间**）、纵轴 = 栈深，找又宽又平的方块 = 热点

### 实操记录
- **GDB attach**：`w11_buggy_lab 2` 死锁后 attach，`thread apply all bt` 拍到 ThreadA 等 `<mutex_b>`、ThreadB 等 `<mutex_a>`，循环等待闭环
- **perf stat**：buggy O(n²) vs fixed O(n log n)，task-clock 10.05s vs 0.037s（≈370×）；GCP VM 无硬件 PMU → `cycles <not supported>`
- **火焰图**：`perf record --call-graph dwarf` + FlameGraph，`FindTarget` 铺满 100% 宽度 = 热点；`fp` 收栈在 `-O0` 内层循环走飞，换 `dwarf` 解决
- 踩到三个真实环境坑：`ptrace_scope=1`（attach 提权）、`perf_event_paranoid=4`（采样被拦，临时降到 1）、云主机虚拟掉 PMU

### 命令备忘
```bash
# GDB attach 抓死锁
sudo gdb -p <PID> -batch -ex "info threads" -ex "thread apply all bt"

# perf 采样 + 火焰图（需先 git clone brendangregg/FlameGraph）
sudo sysctl kernel.perf_event_paranoid=1          # 放开采样权限（临时，重启复原）
perf record -e task-clock -F 250 --call-graph dwarf -o perf.data ./prog
perf script -i perf.data | ~/FlameGraph/stackcollapse-perf.pl > out.folded
~/FlameGraph/flamegraph.pl out.folded > flame.svg
```

### 待办
- **Session 3**（W10 Resize 数学 + W9 mdspan）—— 待补
- Session 3 完成后进 W14（ONNX Runtime 集成）

### 关联
- 深讲补充题：interview_faq.md Q33-Q35
- 自测题库：q1_self_test.md A11

---

## 2026-05-20

### 操作摘要
- Session 2 上半场（W5 深讲）+ `std::expected` 概念再深化 —— 接续 2026-05-18 Q1 自测复盘
- 沉淀产物：`docs/interview_faq.md` 新增 Q31（expected / 错误码 / sum type 选型）、Q32（W5 jthread / stop_token / 伪共享）

### 今日深讲内容
- **std::expected 再深化**：
  - 错误码"输出参数"陷阱 —— 返回值被错误码占用，结果挤进 `out` 参数，逼调用方先 `Model m;` 构造一个"空对象"（造两次 / 逼出默认构造函数 / "已构造但无效"危险窗口 / 违反 RAII）
  - expected ≠ "返回多类型工具" —— 返回多值/多类型早有 pair/tuple/variant/optional；expected 的不可缺特征是「互斥 + 有方向 + 错误处理 API」
  - expected 的真正对手是异常与错误码，不是 pair
- **W5（Session 2 上半场）**：
  - jthread = thread + RAII 自动 join + 自带停止机制
  - stop_token vs atomic<bool> 本质区别 —— atomic 是被动数据，唤不醒睡在 `cv.wait` 里的线程；stop_token 是通知框架（`condition_variable_any` 已接入 + `stop_callback`）
  - alignas(64) 伪共享 —— 两个无关变量同处一条 64B cache line，多核写各自变量却引发 cache line 乒乓；`std::hardware_destructive_interference_size`

### 待办
- **Session 2 下半场**（W11 调试三件套：Valgrind 三种 leak + Perf + 火焰图横纵轴）—— 今日未完成，待补
- **Session 3**（W10 Resize 数学 + W9 mdspan）—— 待补
- 完成 W11 + Session 3 后进 W14（ONNX Runtime 集成）

### 关联
- 自测题库：docs/q1_self_test.md（历史成绩表已加 2026-05-20 行）
- 深讲补充题：docs/interview_faq.md Q27-Q32

---

## 2026-05-18

### 操作摘要
- Q1 W1-W13 知识自测复盘（W14 ONNX Runtime 集成启动前的最后体检）
- 完成 13 题自测 + 评分 + 4 红色短板诊断 + Session 1（W2 noexcept move + W3 Concepts/expected）深讲
- 沉淀产物：`docs/q1_self_test.md`（quiz 自测，答案折叠）+ `docs/interview_faq.md` Q27-Q30（深讲补充题）

### 自测结果
- **整体成绩**：GPA ≈ C+（65-70 分）
- **扎实周次**：W1 / W6 / W7 / W8
- **半懂周次**：W4 / W5 / W9
- **红色短板（必补）**：
  - **W2**：误以为 "move 后 x 已销毁"（实际为 valid but unspecified state）
  - **W3**：Concepts + std::expected 概念忘了
  - **W10**：Resize 双线性 4 邻权重 + Letterbox padding 计算不会
  - **W11**：Valgrind 三种 leak + 火焰图横纵轴释义不会

### 待办（Session 2 + 3 排期）
- **Session 2**（~45min，待安排）：W5 stop_token 与 atomic<bool> 本质区别 + W11 调试三件套（Valgrind + Perf + 火焰图）
- **Session 3**（~30min，待安排）：W10 Resize 数学（双线性 + Letterbox）+ W9 mdspan 复活
- 完成全部 3 个 Session 后再进 W14

### 命令备忘
```bash
# 自测复盘文档位置
docs/q1_self_test.md          # quiz 格式自测
docs/interview_faq.md         # Q27-Q30 深讲
```

### 关联
- 副产出：Obsidian inbox/2026-05-18-edge-ai-q1-cpp20-self-test.md（已存档）

---

## 2026-04-03

### 操作摘要
- 排查 VSCode Remote SSH 反复掉线问题，定位出两条独立根因并修复

### 问题与排查

**现象**：白天 VSCode 连 VPS 正常，晚上回来就掉线，已复现多次

**根因 1（OOM Killer，有日志铁证）**：
- tmux 内的 claude 进程运行了 1 天 6 小时，内存峰值膨胀至 1.6G + Swap 2G
- VPS 只有 1.9Gi RAM，触发 OOM Killer，SSH 进程被连带杀掉
- 日志时间线：`08:04:26 OOM killed tmux scope → 08:04:28 SSH Connection closed`

**根因 2（SSH 无 Keepalive，NAT 超时静默断线）**：
- `/etc/ssh/sshd_config` 中 `ClientAliveInterval` 和 `TCPKeepAlive` 均被注释，实际禁用
- 内核 TCP keepalive 为 7200s，远超运营商 NAT 空闲超时（通常 5~30 分钟）
- 连接空闲时 NAT 表项过期，双端无感知，连接悄悄死掉

### 修复方案

**Fix 1（已执行）**：开启 sshd keepalive
```bash
# /etc/ssh/sshd_config 修改项：
# TCPKeepAlive yes
# ClientAliveInterval 60   （每 60s 探测一次）
# ClientAliveCountMax 10   （连续 10 次无响应约 10 分钟才断）
sudo systemctl reload sshd
```

**Fix 2（待执行）**：本地 `~/.ssh/config` 也加 keepalive（客户端双保险）
```
Host <VPS_IP>
    ServerAliveInterval 60
    ServerAliveCountMax 10
```

**Fix 3（建议）**：给 telegram tmux 的 claude 进程加内存上限，防止下次 OOM
```bash
# 重启 telegram 会话时用 systemd-run 包住，限制最大内存
systemd-run --user --scope -p MemoryMax=600M \
  tmux new-session -d -s telegram 'claude --channels plugin:telegram@claude-plugins-official --dangerously-skip-permissions'
```

### 命令备忘
```bash
# 查看 SSH 断线日志
journalctl -u ssh --since "today" --no-pager | grep -E "Disconnect|timeout|killed"

# 查看 OOM 事件
journalctl --since "today" --no-pager | grep -E "OOM|oom-killer"

# 查看各进程内存排名
ps aux --sort=-%mem | head -15

# 验证 sshd keepalive 已生效
sudo grep -E "ClientAlive|TCPKeepAlive" /etc/ssh/sshd_config
```

---

## 2026-04-02

### 操作摘要
- 更新 `01_Linux_CPP_Foundations/w13_image_pipeline/notes.md`，将全部 ASCII 图替换为 Mermaid（flowchart / sequenceDiagram / stateDiagram-v2）
- 配置 Telegram Claude 插件的持久化启动方案（tmux）
- 重组 `docs/` 目录结构：拍平单文件子目录、归档 tech-debt、删除 claude-usage.md、新增 README.md 导航索引
- 新建 `docs/devlog.md` 开发日志系统
- 在 BotFather 新建 bot `@ChunClaudebot`，替换旧 token
- 更新 `~/.claude/channels/telegram/.env` 为新 token（`8636641025:...`）
- 终止遗留的旧 token 进程（PID 16817），避免双进程冲突
- 将 dmPolicy 临时切换为 `pairing`，完成新 Telegram 账号（`8627270441`）配对
- 锁回 `allowlist` 模式，allowlist 现有两个账号：`8200284523`（旧）、`8627270441`（新）

### 问题与排查
- **现象**：更换 bot token 后发消息无回复
- **根因 1**：旧进程仍在运行并持有旧 token，与新进程并存导致 getUpdates 争抢
- **根因 2**：新 Telegram 账号 ID 不在 allowlist，消息被静默丢弃
- **解法**：kill 旧进程 → 切 pairing 模式 → 配对新账号 → 锁回 allowlist

### 环境变更
- 安装 tmux 3.6a（`sudo apt-get install -y tmux`）

### 命令备忘
```bash
# 查看 Telegram 插件进程是否在运行
ps aux | grep telegram | grep -v grep

# tmux 后台启动 Telegram 插件（终端关闭后仍运行）
tmux new-session -d -s telegram 'claude --channels plugin:telegram@claude-plugins-official --dangerously-skip-permissions'

# 查看所有 tmux 会话
tmux ls

# 重启 Telegram 插件
tmux kill-session -t telegram
tmux new-session -d -s telegram 'claude --channels plugin:telegram@claude-plugins-official --dangerously-skip-permissions'

# 进入会话查看日志（Ctrl+B D 退出但不关闭）
tmux attach -t telegram

# 检查并清理重复的 telegram 进程
ps aux | grep telegram | grep -v grep
kill <旧PID>

# 配对新账号
# 1. 切换为 pairing 模式（Claude Code 内执行）：/telegram:access policy pairing
# 2. 新账号 DM bot，获得 6 位码后执行：/telegram:access pair <code>
# 3. 锁回 allowlist：/telegram:access policy allowlist
```
