# C++ 核心特性概念详解

> 可复用概念的「主题正文」，复习时进这里读。模块专属的设计/Mermaid/踩坑/测试在周笔记。
> 来源周：W1/W2/W3/W9/W10 等（RAII、移动语义、span、Concepts、expected、format 等待陆续毕业入此）。

## 目录

- [static 的四种用法](#static-的四种用法)
- [ABI vs API](#abi-vs-api)
- [有符号/无符号混用与 size_t](#有符号无符号混用与-size_t)
- [匿名命名空间 vs 具名命名空间](#匿名命名空间-vs-具名命名空间)
- [默认实参 + 调用链透传](#默认实参--调用链透传)

> 说明：本文件目前收录 W14 答疑的 `static`/`ABI`/`size_t`，及 W16 源码精读的命名空间、默认实参；
> RAII、移动语义、`std::span`、Concepts、`std::expected`、`std::format` 等按方案甲后续从各周笔记批量毕业进来。

---

## static 的四种用法

**是什么**：C++ 的 `static` 是「一词多义」，共 4 种用法（注意：C++ **没有**「静态类」，那是 C#）：

| # | 用法 | 位置 | `static` 含义 |
|---|---|---|---|
| ① | 静态成员变量 | 类内 | 全类共享一份，不属于对象；`Foo::count_` 访问 |
| ② | 静态成员函数 | 类内 | 不绑对象、无 `this`，`类名::函数()` 调；只能访问静态成员 |
| ③ | 函数内静态局部变量 | 函数内 | 生命周期=整个程序、只初始化一次、C++11+ 线程安全 |
| ④ | 文件作用域 static | 文件顶层 | 内部链接，只本文件可见（现代 C++ 多用匿名 namespace 替代） |

**为什么 / 何时用**：记忆主线——**类内（①②）表「属于类、不属于对象」；类外（③④）表「生命周期更长（③）」或「可见性更窄（④）」**。函数内 static（③）常用于实现单例：第一次执行到才构造、之后返回同一个、线程安全。

**坑**：
- ② 静态成员函数没有 `this`，不能访问普通成员变量（不知道是哪个对象的）。
- ④ 想要「一组只含静态成员、不该实例化的工具集合」，C++ 用 `namespace` 装自由函数或 `=delete` 构造函数，**别套 C# 的「static class」**。
- 经典叠用：「② 静态成员函数提供不依赖对象的全局访问入口 + ③ 函数内静态变量保证唯一/懒加载/线程安全」= 单例。

> 实战出处：`02_Inference_Analysis/w14_ort_basics/inference_engine.cpp`（`GlobalEnv()` 同时用 ②+③）
> 关联：[Ort::Env 全局唯一](inference.md#ortenv-全局唯一函数内-static-单例)

---

## ABI vs API

**是什么**：
- **API**（Application **Programming** Interface）：**源码层面**的接口——有哪些函数、参数怎么写、怎么调用。你写代码时看的。
- **ABI**（Application **Binary** Interface）：**二进制层面**的约定——参数怎么在寄存器/栈上传递、数据类型在内存里怎么排布、符号怎么命名。编译成机器码后，库与库之间对接看的。

**为什么 / 何时用**：调用**预编译的 C 库**（如 ORT 的 C API，`.so` 已编译好）时，函数签名在二进制层面写死了参数类型。C++ 代码要跨这条二进制边界对接，就受 ABI 约束。说「C ABI」是地道说法——C 的 ABI 是跨语言事实标准。

**坑**：`CreateTensor` 参数二进制层面声明为 `float*`（非 const），而我们手里是 `const float*`，所以 `const_cast` 去掉 const 来匹配——这个动作是为了**过二进制边界**，所以用 ABI 而非 API 才精确。别把 ABI 当成 API 的笔误。

> 实战出处：`02_Inference_Analysis/w14_ort_basics/notes.md`（设计决策 2 零拷贝，`const_cast` 注）
> 关联：[零拷贝张量输入](inference.md#零拷贝张量输入--buffer-存活契约)

---

## 有符号/无符号混用与 size_t

**是什么**：把「数量 / 尺寸 / 下标」这类**非负**的量统一用无符号的 `size_t` 表达，而不是有符号的 `int` / `int64_t`。混用两者（如 `size_t n; int64_t d; n *= d;`）会触发隐式的有符号↔无符号转换。

**为什么 / 何时用**：① **类型语义对齐**——标准库里所有「大小」接口（`vector(n)`、`.size()`、`sizeof`）收的都是 `size_t`，累加器从一开始就用它，全程待在无符号尺寸域，不用回头再转。② **消隐式转换告警**——`-Wsign-conversion` + 严格 clang-tidy 下隐式 signed→unsigned 会报警告（CI 零容忍）；显式 `static_cast<size_t>(d)` 表明「故意为之」，告警消失、意图自证。③ **跨平台一致**——32 位平台 `size_t`(32) 与 `int64_t`(64) 秩不同，隐式提升规则微妙；显式 cast 把每步钉在 `size_t` 域。

**坑**：显式转换**不消除负数风险**——若 `d` 真为负（如 ONNX 动态维 `-1`），转成 `size_t` 会变成一个巨大的无符号值，连乘后彻底失真。所以转换的**安全性靠调用顺序兜底**：必须在上游先把负值消掉（如 W14 `ResolveDynamicShape` 把 `<= 0` 的维替换成 1）才轮到连乘。这是个**隐式契约**，重构时若打乱顺序、让负值漏到 `static_cast` 这一步，不会报错但结果错——典型 silent bug。

> 实战出处：`02_Inference_Analysis/w14_ort_basics/ort_basics_demo.cpp`（`ElementCount` 连乘，`static_cast<size_t>(d)`）
> 关联：[shape / tensor / Ort::Value 三者关系](inference.md#shape--tensor--ortvalue-三者关系)（动态维 -1 的来历）

---

## 匿名命名空间 vs 具名命名空间

**是什么**：两种命名空间用途相反。**具名**（`namespace w16 { ... }`）给名字**起一个对外可引用的前缀**，别人写 `w16::YOLODetector` 来用；同一个具名空间可在 `.hpp` 声明、`.cpp` 定义**分处书写**，逻辑上是同一个（不是两个）。**匿名**（`namespace { ... }`）反过来——**故意不给名字**，把里面的符号限制在**本翻译单元（本 .cpp）内部**（internal linkage，等价于早年文件级 `static`）。

**为什么 / 何时用**：判据是「这东西要不要被别的文件引用」。**库代码 → 具名**：`yolo_detector.hpp/.cpp` 提供 `w16::` 下的类型给 demo/test/benchmark `#include` 使用，名字就是 API 契约；项目约定每个模块独立命名空间（`w1`…`w16`，全小写）避免跨模块同名（多个模块都有 `NMS`/`Detection` 不冲突）。**可执行入口 → 匿名**：`yolo_demo.cpp` 是 `main` 所在的叶子文件，没人 include 它，其辅助函数（如 `LoadLabels`）只自己用 → 放匿名空间，不暴露成外部符号，**避免和其他 .o 里同名函数链接冲突**，也不误导别人以为它是公共 API。

**坑**：① 给只在本文件用的辅助函数起具名命名空间，是**反向误导**（暗示它是对外 API）。② `.hpp` 和 `.cpp` 里两个 `namespace w16` 不是「两个命名空间」，是同一个分两处写——初学常误读成不同的。③ 匿名命名空间替代了「文件级 `static` 函数」，现代 C++ 优先用前者（还能放类型/模板，`static` 不能）。

> 实战出处：`02_Inference_Analysis/w16_yolo_detector/yolo_detector.hpp/.cpp`（具名 `w16`）vs `yolo_demo.cpp:14`（匿名，藏 `LoadLabels`）

---

## 默认实参 + 调用链透传

**是什么**：函数声明里给末尾参数写默认值（`cv::Scalar pad_color = cv::Scalar(114,114,114)`），调用方省略该参数时编译器**自动填入默认值**。多层函数把「带默认值的参数」一层层原样传给下层，就是「透传」——上层省略 → 取默认 → 透传到底层真正生效处。

**为什么 / 何时用**：让常用路径调用简洁（不必每次写 `114` 灰），又保留少数场景自定义的口子。W16 detector 调 `LetterboxToTensor(rgb, 640, 640, info)` 只传 4 个参数，第 5 个 `pad_color` 走默认 `(114,114,114)`；该值经 `LetterboxToTensor → Letterbox → cv::copyMakeBorder` 三层透传，最终在图像四周刷成灰边。**「demo 没传 pad_color 哪来的灰边」的答案就是默认实参**。

**坑**：① 默认实参只能写在**声明**（`.hpp`）里一次，定义（`.cpp`）里不能重复写，否则重定义报错。② 默认值在**调用点**按声明可见的那个版本绑定，是编译期行为。③ 默认参数必须放在参数列表**末尾**（后面不能再有无默认值的参数）。④ 透传时注意语境变化：W16 在 RGB 图上填充，灰 `(114,114,114)` 三分量相等故 BGR/RGB 无歧义；若改非灰色填充，要按当前通道序填，别填反（见 image-ops.md「BGR↔RGB」）。

> 实战出处：`01_Linux_CPP_Foundations/w10_resize/custom_resize.hpp:79`（`pad_color` 默认值）→ `02_Inference_Analysis/w16_yolo_detector/yolo_detector.cpp:35`（省略调用）
