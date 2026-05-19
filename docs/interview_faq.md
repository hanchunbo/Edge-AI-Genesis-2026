# Q1 C++ 基础面试题库 (W1-W11)

> **使用说明**：本文档包含 Q1 全程学习内容相关的高频面试题。每道题包含问题、考察点、参考答案和加分回答。
>
> **更新记录**：
> - 2026-02-06 首次创建，12 道题目覆盖 W1-W5
> - 2026-03-17 补充 W9-W10 图像预处理题目（Q13-Q14）
> - 2026-03-17 补充 W11 调试工具题目（Q15-Q16）
> - 2026-03-29 补充 W6/W7/W8 题目（Q17-Q26），FAQ 库覆盖 W1-W11 全程

---

## W1：内存安全与 RAII

### Q1: 什么是 RAII？为什么它对 AI 推理程序特别重要？

**考察点**：理解资源管理基础，能关联到实际场景。

**参考答案**：
RAII (Resource Acquisition Is Initialization) 是 C++ 的核心资源管理模式，即"资源获取即初始化"。其核心思想是将资源的生命周期与对象的生命周期绑定：
- 构造函数中获取资源（如内存、文件句柄、GPU 显存）
- 析构函数中释放资源

对于 AI 推理程序特别重要的原因：
1. AI 推理服务通常是**常驻进程**（7x24 运行），任何内存泄漏都会累积导致 OOM。
2. GPU 显存是稀缺资源，泄漏后无法被其他任务使用。
3. 推理过程中可能发生异常（如输入格式错误），RAII 确保异常时资源自动释放。

**加分回答**：
> "在我们的线程池实现中，我使用 `std::jthread` 替代 `std::thread`，就是利用了 RAII 的自动 join 特性，避免了线程泄漏。"

---

### Q2: `shared_ptr` 的引用计数是线程安全的吗？它对高频推理有什么性能影响？

**考察点**：区分"控制块线程安全"和"数据访问线程安全"。

**参考答案**：
**引用计数的增减**是线程安全的，因为它使用了原子操作 (`std::atomic`)。但**通过 `shared_ptr` 访问的数据本身**不是线程安全的，需要额外加锁。

对高频推理的性能影响：
1. 每次拷贝 `shared_ptr` 都需要原子递增计数器，存在**缓存一致性开销**。
2. 在 AI 推理的热路径上（如每帧处理），频繁拷贝 `shared_ptr` 会成为性能瓶颈。
3. 最佳实践：热路径上使用 `const shared_ptr&` 避免拷贝，或者评估是否可以使用 `unique_ptr`。

**加分回答**：
> "如果是单一所有权场景，我会优先使用 `unique_ptr`，因为它没有引用计数开销，性能更好。"

---

### Q3: `weak_ptr` 的作用是什么？在 AI 应用中有哪些使用场景？

**考察点**：理解循环引用问题和观察者模式。

**参考答案**：
`weak_ptr` 是一种不增加引用计数的智能指针，用于：
1. **打破循环引用**：当两个对象互相持有 `shared_ptr` 时，会导致内存永远无法释放。
2. **实现观察者模式**：观察者持有 `weak_ptr`，不影响被观察对象的生命周期。

AI 应用场景：
- **模型缓存管理**：缓存系统持有模型的 `weak_ptr`，不阻止模型被卸载，但可以在需要时检查模型是否仍在内存中。
- **回调函数注册**：推理回调持有 Engine 的 `weak_ptr`，避免回调阻止 Engine 析构。

---

## W2：移动语义与零拷贝

### Q4: 请解释 `std::move` 的作用，它实际"移动"了什么？

**考察点**：区分"移动"语义和实际操作。

**参考答案**：
`std::move` **本身不移动任何东西**，它只是一个类型转换函数，将左值强制转换为右值引用 (`T&&`)。

实际的"移动"发生在：
1. 调用对象的**移动构造函数**或**移动赋值运算符**时。
2. 移动构造函数会"窃取"源对象的资源（如指针、缓冲区），而不是深拷贝。
3. 源对象被置于"有效但未定义状态"（通常是空指针或空容器）。

```cpp
std::vector<float> a(1000000);
std::vector<float> b = std::move(a);  // a 的内部指针被转移给 b，a 变为空
```

**加分回答**：
> "在处理 4K 图像帧时，我通过移动语义将图像从预处理模块转移到推理模块，避免了 8MB+ 的内存拷贝。"

---

### Q5: 什么是"完美转发"？`std::forward` 和 `std::move` 有什么区别？

**考察点**：模板编程和万能引用。

**参考答案**：
**完美转发** (Perfect Forwarding) 是指在模板函数中保持参数的原始值类别（左值/右值）传递给另一个函数。

区别：
- `std::move`：**无条件**将参数转换为右值引用，用于明确表示"我不再需要这个对象"。
- `std::forward`：**有条件**转发，只有当参数本身是右值时才转换为右值引用，保持原始类型。

```cpp
template<typename T>
void wrapper(T&& arg) {
    // std::forward 保持 arg 的原始类型（左值or右值）
    inner_function(std::forward<T>(arg));
}
```

**加分回答**：
> "在线程池的 `Submit` 函数模板中，我使用 `std::forward` 来完美转发用户提交的 Lambda，确保 Lambda 捕获的资源不会被意外拷贝。"

---

### Q6: 如何在编译期验证一个类型是可移动的？

**考察点**：类型特性 (Type Traits) 和 `static_assert`。

**参考答案**：
使用 `<type_traits>` 头文件中的类型特性：

```cpp
#include <type_traits>

static_assert(std::is_move_constructible_v<MyClass>, 
              "MyClass must be move constructible");
static_assert(std::is_nothrow_move_constructible_v<MyClass>,
              "MyClass move constructor must be noexcept");
```

`noexcept` 移动构造函数很重要，因为 `std::vector` 在扩容时只有在移动构造函数是 `noexcept` 时才会使用移动而非拷贝。

---

## W3：C++20 Concepts 与错误处理

### Q7: C++20 Concepts 相比 SFINAE 有什么优势？请举例说明。

**考察点**：现代 C++ 模板约束。

**参考答案**：
主要优势：
1. **更清晰的编译错误信息**：SFINAE 失败时错误信息晦涩难懂，Concepts 直接告诉你"类型不满足 XX 约束"。
2. **更好的可读性**：约束条件直接写在函数签名中，而非隐藏在模板参数列表。
3. **可组合**：多个 Concepts 可以用 `&&` 和 `||` 组合。

```cpp
// SFINAE 方式 (旧)
template<typename T, 
         typename = std::enable_if_t<std::is_integral_v<T>>>
void process(T value);

// Concepts 方式 (新)
template<std::integral T>
void process(T value);

// 或者更直观的 requires 语法
void process(std::integral auto value);
```

**加分回答**：
> "在我们的 ThreadPool 中，我用 `std::invocable` Concept 约束提交的任务类型，如果用户传入非可调用对象，编译器会直接报错'不满足 invocable 约束'而非一大堆模板展开错误。"

---

### Q8: `std::expected` 相比异常有什么优势？为什么 AI 部署场景更推荐它？

**考察点**：错误处理策略选择。

**参考答案**：
`std::expected<T, E>` 是 C++23 引入的类型，表示"要么是成功值 T，要么是错误 E"。

相比异常的优势：
1. **零开销**：异常处理需要运行时栈展开，`expected` 是纯值语义，无运行时开销。
2. **显式错误处理**：调用者必须处理错误情况，不会被忽略。
3. **嵌入式友好**：许多嵌入式平台禁用异常，`expected` 是唯一选择。

AI 部署场景推荐原因：
- 推理引擎可能被编译为动态库，异常跨 DLL 边界传播是未定义行为。
- 高频推理场景下，异常的栈展开开销不可接受。
- 边缘设备可能禁用异常以减少二进制体积。

---

## W4：多线程与任务同步

### Q9: `std::counting_semaphore` 和 `std::condition_variable` 有什么区别？什么场景用哪个？

**考察点**：同步原语选择。

**参考答案**：

| 特性 | `counting_semaphore` | `condition_variable` |
|------|----------------------|----------------------|
| 计数 | 内置计数器 | 需要手动维护 |
| 虚假唤醒 | 无 | 有，需要 while 循环检查 |
| 使用复杂度 | 简单 | 需要配合 mutex 和谓词 |
| C++ 版本 | C++20 | C++11 |

使用场景：
- **Semaphore**：简单的生产者-消费者、限流（如限制同时处理的请求数）。
- **Condition Variable**：复杂的条件等待（如等待队列非空且某标志位为真）。

```cpp
// Semaphore 实现简单限流
std::counting_semaphore<10> limiter(10);  // 最多 10 个并发
void process() {
    limiter.acquire();
    // 处理...
    limiter.release();
}
```

---

### Q10: 什么是数据竞争 (Data Race)？如何检测和避免？

**考察点**：并发安全基础。

**参考答案**：
**数据竞争**是指两个或多个线程同时访问同一内存位置，且至少有一个是写操作，且没有同步机制。这是**未定义行为**。

检测方法：
- **ThreadSanitizer (TSan)**：`g++ -fsanitize=thread` 编译后运行，能精确定位数据竞争。

避免方法：
1. **互斥锁**：`std::mutex` + `std::lock_guard`
2. **原子操作**：`std::atomic<T>` 用于简单计数器
3. **无锁设计**：使用无锁队列等数据结构
4. **线程局部存储**：`thread_local` 变量避免共享

**加分回答**：
> "在我们的项目中，我使用 `std::atomic` 实现无锁的任务计数器，避免了每次更新计数都要加锁的开销。"

---

## W5：线程池与 C++20 并发特性

### Q11: `std::jthread` 相比 `std::thread` 有什么优势？

**考察点**：C++20 新特性理解。

**参考答案**：
`std::jthread` (C++20) 的优势：

1. **RAII 自动 join**：析构时自动调用 `join()`，避免忘记 join 导致程序崩溃。
2. **内置 stop_token**：支持协作式中断，可以优雅地请求线程停止。
3. **异常安全**：不会因为忘记 join 而在异常时调用 `std::terminate`。

```cpp
// std::thread 需要手动管理
std::thread t(work);
try {
    // ... 可能抛异常
    t.join();
} catch (...) {
    t.join();  // 容易忘记
    throw;
}

// std::jthread 自动管理
std::jthread jt(work);  // 离开作用域自动 join
```

---

### Q12: 什么是 False Sharing？如何在线程池中避免它？

**考察点**：性能优化深度。

**参考答案**：
**False Sharing（伪共享）** 是指多个线程修改不同的变量，但这些变量恰好在同一个 CPU 缓存行（通常 64 字节）内，导致缓存行频繁失效，性能大幅下降。

避免方法：
```cpp
// 问题代码：两个计数器可能在同一缓存行
struct BadStats {
    std::atomic<int> counter1;
    std::atomic<int> counter2;  // 可能与 counter1 共享缓存行
};

// 解决方案：使用 alignas 强制对齐
struct GoodStats {
    alignas(64) std::atomic<int> counter1;
    alignas(64) std::atomic<int> counter2;  // 独占一个缓存行
};
```

在线程池中的应用：
- 每个工作线程的私有数据（如任务计数器）应该 `alignas(64)` 对齐。
- 共享队列的头尾指针应该分开对齐，避免生产者和消费者互相干扰。

**加分回答**：
> "在我们的 ThreadPool 实现中，我使用 `alignas(64)` 加上 padding 确保热点变量独占缓存行，在 4 核 CPU 上测试，吞吐量提升了约 30%。"

---

## W9-W10：图像预处理与 OpenCV 底层

### Q13: `std::mdspan` 相比于传统的 `y * step + x` 手算偏移有什么优势？

**考察点**：C++23 新特性，多维内存视图抽象。

**参考答案**：
1. **消除 Bug**：手算偏移极易写错行列顺序或步长，`mdspan` 完全由编译器保证索引计算正确。
2. **多维抽象**：`mdspan` 通过 `layout_stride` 可以无缝表达图像中的 CHW 和 HWC 布局转换，而底层指针完全不变。
3. **零开销**：它是纯头文件实现的轻量级视图，绝大部分情况下编译器能将其完全内联优化，甚至比手写的指针偏移更容易向量化（SIMD）。

**加分回答**：
> "在我的自定义 Resize 算子中，我不仅用 `mdspan` 抽象了由于 padding 带来的不连续内存映射，还利用它方便地处理了双线性插值中的越界映射逻辑。"

---

### Q14: 模型部署中 Resize 的 Asymmetric 和 HalfPixel 坐标模式有什么区别？

**考察点**：工程实践细节，AI 预处理踩坑经验。

**参考答案**：
- **Asymmetric**（非对称）：将像素视为左上角对齐的点。坐标映射公式为 `src_x = dst_x * scale`。ONNX Runtime 默认使用此模式。
- **HalfPixel**（半像素）：将像素视为有面积的格子，以中心点对齐。映射公式为 `src_x = (dst_x + 0.5) * scale - 0.5`。TensorRT 和 PyTorch 默认使用。

如果在部署时 C++ 预处理用的 cv::resize（接近 HalfPixel）而模型内用的是 Asymmetric，会导致图像边缘像素系统性偏移，肉眼难辨却会引起小目标漏检。

---

## W11：性能调优与 Debug

### Q15: Valgrind 检测内存泄漏的原理是什么？为什么它会让程序变慢？

**考察点**：底层调试工具原理。

**参考答案**：
Valgrind 是一个动态二进制插桩（DBI）框架，其中最常用的 Memcheck 工具：
1. 它在程序运行时接管并重写指令流。
2. 它在系统所有 `malloc` / `new` 和 `free` / `delete` 处插入钩子（Hooks），记录每一块内存的首地址和所属堆栈。
3. 程序结束时，它扫描所有寄存器和栈帧，寻找是否还有指针指向之前分配的堆块，如果没有，则报告 "definitely lost"。

变慢 10-30 倍是因为：程序实际上是运行在 Valgrind 提供的一个虚拟 CPU 环境上层，每条涉及内存访问的机器指令都被加入了一个合法性检查。

---

### Q16: 死锁产生的必要条件是什么？如何从架构上避免死锁？

**考察点**：操作系统基础与并发规避。

**参考答案**：
四个必要条件（缺一不可）：
1. 互斥条件（资源独占）
2. 持有并等待（不释放已有资源去等新资源）
3. 不可剥夺（无法强行抢占资源）
4. 循环等待（形成 A 等 B，B 等 A 的环）

避免死锁的最佳实践（打破循环等待）：
1. **锁排序**：所有线程强制按照同一个固定的全局顺序获取锁。
2. **同时加锁**：直接使用 C++17 的 `std::scoped_lock(m1, m2, ...)`，它内部使用死锁避免算法保证安全地获取所有相关的锁，无论传入顺序如何。

**加分回答**：
> "在我们的 W11 实验室中，我制造的两个线程互相逆序拿锁的死锁，最后仅仅是用 `std::scoped_lock` 一行代码就漂亮且隐式地解决了。"

---

## W6：Linux 高性能 I/O（mmap + std::span）

### Q17: `mmap` 和 `read`/`fstream` 读取大文件有什么本质区别？

**考察点**：操作系统 I/O 原理，AI 模型加载场景优化。

**参考答案**：

| 维度 | `fstream::read` | `mmap` |
|------|-----------------|--------|
| 拷贝次数 | 内核 Page Cache → 用户缓冲区（1次拷贝） | 零拷贝（MMU 直接映射） |
| 内存峰值 | ≈ 2× 文件大小 | 按需 Page Fault，物理内存占用极低 |
| 启动延迟 | 必须全量读入才能使用 | `mmap` 调用本身极快，首次访问才触发惰性加载 |
| 适用场景 | 小文件、顺序读取 | GB 级模型权重、反复随机访问 |

核心原理：`mmap` 通过 MMU 建立"虚拟页 → 磁盘块"的映射，用户态直接通过指针读取，内核仅在首次访问时触发 Page Fault 按需换页，省去了"内核缓冲区 → 用户缓冲区"的额外一次 `memcpy`。

**加分回答**：
> "在我们的 W6 实现中，`mmap` 成功后立即关闭了 `fd`——POSIX 保证映射建立后不依赖 `fd` 存在，及时关闭可以防止边缘设备（`fd` 上限通常 1024）出现 `Too many open files` 错误。"

---

### Q18: `mmap` 建立后为什么要立即调用 `madvise`？有哪些模式？

**考察点**：高性能 I/O 调优经验。

**参考答案**：
`mmap` 默认按需换页，内核不知道你的访问模式，可能做出次优的预读决策。`madvise` 让你把访问意图告诉内核：

| 标志 | 语义 | AI 部署场景 |
|------|------|------------|
| `MADV_SEQUENTIAL` | 从头到尾顺序扫描 | 模型权重逐层加载 |
| `MADV_WILLNEED` | 尽快异步预取到物理内存 | 推理前热身（Warmup） |
| `MADV_RANDOM` | 随机访问，禁用预读 | 稀疏权重、KV Cache |

`madvise` 是 best-effort，即使失败也不影响正确性，因此实现中通常静默忽略失败。

---

### Q19: 在 W6 的 `MmapLoader<T>` 中，`TriviallyMappable` Concept 约束了什么？为什么必须这样约束？

**考察点**：Concepts 实际工程应用，内存安全。

**参考答案**：
`TriviallyMappable` 要求类型 T 同时满足三个条件：

```cpp
template <typename T>
concept TriviallyMappable = std::is_trivially_copyable_v<T> &&
                            std::is_standard_layout_v<T>   &&
                            !std::is_empty_v<T>;
```

- **`trivially_copyable`**：保证 `reinterpret_cast<T*>(mmap_ptr)` 的内存解释是 well-defined，有虚函数或用户定义构造的类不满足。
- **`standard_layout`**：保证内存布局与磁盘二进制格式一一对应，混合访问控制的类不满足。
- **`!empty`**：`sizeof(T) > 0`，避免 `byte_size / sizeof(T)` 除零。

若不加约束，用 `std::string` 或含虚函数的类实例化 `MmapLoader` 会产生未定义行为，Concept 在编译期就拦截这类错误。

---

### Q20: `std::span<const T>` 作为 `mmap` 数据的视图有什么优势？直接用裸指针有什么问题？

**考察点**：C++20 零拷贝视图，类型安全。

**参考答案**：
`mmap` 返回 `void*`，直接使用裸指针有两个问题：
1. **长度信息丢失**：调用方必须额外维护一个长度变量，容易出现"指针和长度不一致"的 bug。
2. **类型不安全**：`void*` 需要手动 `reinterpret_cast`，编译器无法帮你检查类型。

`std::span<const T>` 将"首元素指针 + 元素数量"打包成一个类型安全的视图：
- 可直接用于 `std::ranges` 算法、范围 for、`subspan` 切片
- `const T` 传达"只读"语义，防止意外修改映射内存（写操作会触发 `SIGSEGV`）
- 零运行时开销，编译器完全内联

---

## W7：现代 CMake 工程化

### Q21: CMake 的 `PRIVATE` / `INTERFACE` / `PUBLIC` 三种可见性有什么区别？

**考察点**：现代 CMake Target-based 模型，工程化基础。

**参考答案**：

```
              本库编译时可见？  链接它的消费者自动继承？
PRIVATE            ✅               ❌
INTERFACE          ❌               ✅
PUBLIC             ✅               ✅
```

**记忆方法**：`PRIVATE` = 只给自己；`INTERFACE` = 只给别人；`PUBLIC` = 给自己和别人。

**项目实例**（W7 `lib/CMakeLists.txt`）：
```cmake
# 头文件路径 PUBLIC：lib 源码和 app/tests 都能直接 #include
target_include_directories(w7_tensor_utils PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)

# 编译选项 PRIVATE：只影响 lib 自身，不传给 app/tests
target_compile_options(w7_tensor_utils PRIVATE -O2)
```

**工程意义**：`app/main.cpp` 和测试文件可以直接写 `#include "tensor_utils.hpp"`，无需在各自的 `CMakeLists.txt` 里配任何路径——路径跟随库目标自动传播。

**加分回答**：
> "旧式的 `include_directories()` 是全局配置，会污染整个构建树中的所有目标；Target-based 的 `target_include_directories` 精确控制每个目标的属性，这是现代 CMake 最核心的设计转变。"

---

### Q22: 什么是 CMake 生成器表达式？为什么 `target_include_directories` 要区分 `BUILD_INTERFACE` 和 `INSTALL_INTERFACE`？

**考察点**：CMake 进阶，库的可分发性设计。

**参考答案**：
生成器表达式语法为 `$<condition:value>`，在 CMake **生成构建文件时**才求值（configure 阶段不求值），用于表达"构建时"和"安装时"的不同配置。

以头文件路径为例：
```cmake
target_include_directories(mylib PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
```

- `BUILD_INTERFACE`：在当前机器上构建时，指向源码树的绝对路径（如 `/home/dev/project/include`）。
- `INSTALL_INTERFACE`：通过 `cmake --install` 安装到其他位置后，指向 `${prefix}/include` 相对路径。

如果不区分，导出的 cmake 配置文件会包含硬编码的绝对路径，在其他机器上 `find_package` 会找不到头文件。

---

### Q23: C++20 具名模块（Named Modules）相比传统 `.hpp` 头文件有什么优势？当前工程化的主要挑战是什么？

**考察点**：C++20 新特性，现代工具链现状。

**参考答案**：

| 对比项 | 传统 `.hpp` | C++20 具名模块 |
|--------|------------|----------------|
| 编译速度 | 每个翻译单元重复解析头文件 | 预编译为 BMI，一次生成，多处复用 |
| 宏隔离 | 宏会泄漏给所有包含者 | 模块内宏不泄漏到模块边界外 |
| 顺序依赖 | `#include` 顺序敏感 | `import` 顺序无关 |

**工程化挑战（2026 现状）**：
- 编译器支持：需要 GCC 15+ / Clang 17+，`import std;` 要 GCC 15 稳定支持
- 构建系统：必须用 Ninja（`-G Ninja`），Unix Makefiles 不支持 `FILE_SET CXX_MODULES`
- CMake：需要 CMake 3.28+，`import std;` 用 `export import std;` 语法避免符号冲突

**加分回答**：
> "在我们的 W7 项目里，C++20 模块演示在 CMakeLists.txt 里加了版本检查，低于 CMake 3.28 时自动跳过并打印提示，确保普通用户不会因为工具链不够新而构建失败。"

---

## W8：自动化测试与代码覆盖率

### Q24: 如何在 CMake 工程中以零网络依赖的方式集成 GTest？

**考察点**：工程化能力，内网/离线环境的实际工程经验。

**参考答案**：
使用 `FetchContent` + 本地 zip 方案：
```cmake
FetchContent_Declare(
  googletest
  URL file://${CMAKE_SOURCE_DIR}/third_party/v1.15.2.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(googletest)
```

`file://` 协议让 CMake 读取本地文件而非发起网络请求，整个 configure/build/ctest 流程全程无需网络访问。将 zip 文件提交到仓库的 `third_party/`，在内网机器、VPS、CI 环境上均可开箱即用。

**注意事项**：
- `FetchContent_MakeAvailable` 必须在所有用到 `GTest::gtest_main` 的 `add_subdirectory` 之前执行
- `set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)` 防止 GTest 的 `install()` 规则污染父项目的安装树

---

### Q25: gcov/lcov 代码覆盖率的工作原理是什么？`.gcno` 和 `.gcda` 分别是什么文件？

**考察点**：测试工程化原理，CI 质量门禁设计。

**参考答案**：

| 文件 | 生成时机 | 内容 |
|------|---------|------|
| `.gcno` | 编译期（`-fprofile-arcs -ftest-coverage`） | 代码的分支控制流图 |
| `.gcda` | 运行期 | 实际执行次数（每次运行**累加**，需用 `--zerocounters` 清零） |

**完整流程**：
```bash
# 1. 用 --coverage 编译
cmake -B build -DW8_COVERAGE=ON
cmake --build build

# 2. 跑测试（生成 .gcda）
ctest --test-dir build

# 3. lcov 读取 .gcda + .gcno → .info
lcov --capture --directory build --output-file raw.info

# 4. 过滤第三方代码
lcov --remove raw.info "/usr/*" "*/_deps/*" --output-file filtered.info

# 5. 生成 HTML 报告
genhtml filtered.info --output-directory coverage_report
```

**加分回答**：
> "我们项目中遇到了 lcov 2.x + GCC 15 的 `mismatch/inconsistent` 问题——内联函数行号误判导致大量 warning 变 error。解决方案是在 `lcov --capture` 和 `--remove` 步骤加 `--ignore-errors mismatch,inconsistent,unused` 参数，已写入 `CMakeLists.txt` 的 `w8_coverage` target 中。"

---

### Q26: 代码覆盖率达到 100% 就意味着没有 Bug 吗？覆盖率应该怎么用？

**考察点**：测试工程观，避免"为数字而测试"的反模式。

**参考答案**：
**不是**。覆盖率只能证明"代码被执行过"，无法证明"行为正确"。

覆盖率的正确用法：
1. **发现测试盲区**：行覆盖率报告中的红色高亮行 = 完全未被测试的代码路径，应优先补充用例。
2. **质量底线门禁**：在 CI 中设置覆盖率阈值（如行 ≥ 90%），防止新增代码缺少测试就合入主干。
3. **不是目标而是指标**：不要为了提高覆盖率数字而写无意义的测试；应关注**分支覆盖率**（每个 if/else 分支均被测到）比**行**覆盖率更能暴露边界 bug。

**我们项目的基线**（W1-W7 合计，GCC 15）：行覆盖率 **98.7%**，函数覆盖率 **100%**，未覆盖行主要集中在错误分支（如 `mmap` 失败的 `SIGBUS` 路径）。

---

## Q1 自测复盘补充题（Q27-Q30）

> **来源**：2026-05-18 Q1 自测会话（详见 [q1_self_test.md](q1_self_test.md) 与 [devlog.md](devlog.md)），暴露 4 个红色短板，这 4 道题专门做深讲，作为对 Q1-Q26 的补充。

---

### Q27: std::move(x) 后 x 是什么状态？什么时候 vector 会把你的 move ctor 退回成 copy？

**考察点**：移动语义本质 + 标准库强异常安全保证 + 标准库默认 noexcept 知识

**参考答案**：

**Part 1 · moved-from 状态**：`std::move(x)` 后 x 处于 **"valid but unspecified state"**——
- ✅ **还活着、还会被析构**（不是"被销毁"）
- ✅ 可以重新赋值、可以析构、可以调用"无前置条件"的成员函数（如 `clear()` / `size()`）
- ❌ 不能依赖它的"原值"做业务判断（标准只保证"valid"，不保证具体值）

经典 bug：自定义类型写 move ctor 时若忘了把源对象的 raw pointer 置 nullptr，会导致 moved-from 对象析构时**双重释放**。

**Part 2 · vector 何时退回 copy**：

`std::vector` 在 push_back / resize 触发扩容时，需要**强异常安全保证**（操作失败可完全回滚）。
- 用 **move** 搬：搬到一半若 move ctor 抛异常 → 旧元素已被破坏无法回滚
- 用 **copy** 搬：搬到一半若 copy ctor 抛异常 → 旧元素完好可丢弃新内存回滚 ✅

底层判断（简化）：
```cpp
if constexpr (std::is_nothrow_move_constructible_v<T>)
    new (dst) T(std::move(src));    // 用 move（快）
else
    new (dst) T(src);                // 退回 copy（慢但安全）
```

**Part 3 · 标准库 move ctor noexcept 速查表**：

| 类型 | move ctor noexcept |
|---|---|
| `vector<T>` / `string` / `unique_ptr<T>` / `shared_ptr<T>` | ✅ 是 |
| **`std::function<R(Args...)>`** | ❌ **否**（坑） |
| **`std::list` / `std::map` / `std::set`** | ❌ **否**（坑） |

持有这些"❌ 否"成员的类，**隐式 move ctor 不是 noexcept**——会被 vector 默默退回 copy 路径，性能塌方。

**修复方案**：
- 用 `std::unique_ptr<std::function<...>>` 间接持有（指针 move 永远 noexcept）
- C++23：改用 `std::move_only_function`
- 加 `static_assert(std::is_nothrow_move_constructible_v<MyClass>);` 防止退化

**加分回答**：
> "我在 W13 的 ThreadPool 实现里，给 Task 类显式写了 `noexcept` 的 move ctor 并加了 static_assert——这样在 std::vector 任务队列扩容时一定走 move 路径，而不是因为某个成员（比如 std::function 回调）默认非 noexcept 退化到 copy。这个细节让队列吞吐量提升了一个量级。"

---

### Q28: Concepts 跟 SFINAE / enable_if 的本质区别是什么？为什么 concept 不能做运行期值约束？

**考察点**：C++20 Concepts 设计意图 + 编译期 / 运行期约束的边界

**参考答案**：

**Part 1 · Concepts vs SFINAE/enable_if**：

| 维度 | SFINAE / enable_if (C++17 之前) | Concepts (C++20) |
|---|---|---|
| 报错信息 | 长 3 屏，从模板内部报 | 短 1-2 行，在调用现场报 |
| 自文档化 | ❌ 约束藏在 enable_if 表达式里难读 | ✅ `concept Scannable = ...` 一目了然 |
| IDE 补全 | ❌ 不知道 T 长啥样 | ✅ IDE 能基于 concept 智能提示 |
| 写法 | `template<typename T, std::enable_if_t<is_xxx<T>::value>* = nullptr>` | `template<Scannable T>` 或 `void f(Scannable auto x)` |

**Part 2 · 标准写法**：

```cpp
template<typename T>
concept Scannable = requires(T t) {
    { t.name() } -> std::convertible_to<std::string_view>;
};

template<Scannable T>
void Process(T value) { value.name(); }
```

5 种常见约束类型：
1. 表达式合法（`t.size();`）
2. 表达式合法 + 返回类型约束（`{ t.empty() } -> std::convertible_to<bool>;`）
3. 嵌套类型约束（`typename T::value_type;`）
4. noexcept 约束（`{ t.clear() } noexcept;`）
5. 带参数约束（`t.at(i);`）

**Part 3 · 为什么不能做运行期值约束**（关键认知陷阱）：

```cpp
template<typename T>
concept Loadable = requires(T t) {
    t.load();
    t.size() > 0;   // ❌ 陷阱！这不检查 size 的运行期值
};
```

**真相**：`t.size() > 0;` 的真正含义是"**这个表达式编译时是否合法可写**"——即 `t.size()` 能否调用 + 返回类型能否与 0 用 `>` 比较。**不会去判断 size 究竟是不是大于 0**。

原因：**concept 在模板实例化阶段（编译期）就要算完**，而 size 的运行期值要等程序跑起来才知道。

正确做法：
- 想要"返回 bool"约束：`{ t.size() > 0 } -> std::convertible_to<bool>;`
- 想要"返回某类型"约束：`{ t.size() } -> std::convertible_to<size_t>;`
- 想要运行期值检查：那是 `assert` / `if constexpr` / `std::expected` 的领域，不是 concept

**加分回答**：
> "我在 model_scanner.cpp 里用 `ByteBufferLike` concept 约束模型加载函数——只要类型提供 data() 和 size() 就能传进来。这让 std::vector、自己实现的 MmapRegion、甚至 std::array 全都能复用同一份 LoadModel 代码，且 IDE 报错精准。如果用 SFINAE 写会丑很多。"

---

### Q29: std::expected<T, E> 的"零开销"机制是什么？什么时候你仍然应该选异常？

**考察点**：C++23 expected 设计 + 异常成本 + 选型判断

**参考答案**：

**Part 1 · 零开销机制**：

`std::expected<T, E>` 内部是个 union + bool tag：

```cpp
// 概念上简化
template<typename T, typename E>
class expected {
    union { T value_; E error_; };
    bool has_value_;
};
```

零开销三层含义：
1. **不分配堆**：T 和 E 在栈上同地址 union，size 是 `max(sizeof(T), sizeof(E)) + 1 byte tag + 对齐 padding`
2. **不生成异常 unwind 表**：异常需要编译器在每个可能 throw 的栈帧生成 unwind metadata，二进制 +5-30%；expected 不需要
3. **失败路径就是 if/else**：调用方 `if (!result)` 检查，编译器可像普通函数一样内联优化

**Part 2 · 与异常的对比**：

| 开销 | 异常 | std::expected |
|---|---|---|
| 编译期 unwind 表 | ✅ 生成 | ❌ 不生成 |
| 编译期 RTTI | ✅ 异常类型需要 | ❌ 不需要 |
| 运行期成功路径 | ✅ 零成本 | ✅ 零成本 |
| **运行期失败路径** | 🔴 **栈展开 μs 级（比成功慢 1000x）** | ✅ **if/else ns 级** |
| 编译器内联 | 🔴 难（跨函数边界） | ✅ 易 |

**Part 3 · C++23 链式风格**：

```cpp
auto result = LoadModel("foo.onnx")
    .and_then([](Model m) { return m.Inference(input); })
    .and_then([](Tensor t) { return PostProcess(t); });
// 任意一步失败，整条链短路，result 携带最早错误
```

**Part 4 · 何时仍坚持用异常**：

1. **构造函数失败**：构造函数没有"返回值"概念，无法返 expected，只能 throw 或 std::abort
2. **operator 重载失败**：`operator[]` / `operator+=` 等如果不能返回额外错误信息，throw 是唯一选择
3. **跨边界传递严重错误**：assert 失败、不可恢复的逻辑错误，throw + 顶层 catch 更符合"快速失败"语义
4. **第三方库已用异常**：如 std::stoi、Boost 系列——硬包成 expected 反而增加 boilerplate

**反过来**——这些场景必用 expected：
- 嵌入式 / 实时系统（禁用异常）
- 业务可恢复的错误（文件找不到、网络超时）
- 性能敏感的失败路径（推理失败要 fallback 重试）
- 接口设计上希望调用方"必须处理错误"（异常容易漏 catch）

**加分回答**：
> "我在 W14 的 InferenceEngine::Create 里把 ORT C++ API 的异常包成 expected<InferenceEngine, std::string> 对外暴露——内部保留 ORT 的强壮性（让它抛），对外给一个嵌入式友好、调用方必须处理的接口。这种'内部 throw + 边界转 expected'的 adapter 模式我觉得是 C++23 库设计的最佳实践。"

---

### Q30: 双线性插值的 4 邻点权重公式？Letterbox 为什么必须保比例？1920×1080 → 640×640 的 padding 怎么算？

**考察点**：W10 Resize 算子的数学基础 + 模型推理对图像几何的敏感性

**参考答案**：

**Part 1 · 双线性插值 4 邻权重**：

目标输出像素 (x_out, y_out) 反推回输入图位置 (x_in + dx, y_in + dy)，dx/dy 是小数部分（0 ≤ dx, dy < 1）。4 个邻居在 (x_in, y_in)、(x_in+1, y_in)、(x_in, y_in+1)、(x_in+1, y_in+1)：

| 邻居 | 权重 |
|------|------|
| 左上 (x, y) | (1-dx)(1-dy) |
| 右上 (x+1, y) | dx(1-dy) |
| 左下 (x, y+1) | (1-dx)dy |
| 右下 (x+1, y+1) | dx·dy |

**所有权重和恒为 1**（即 dx(1-dy) + (1-dx)(1-dy) + dx·dy + (1-dx)dy = 1，可代数验证）。

输出值 = Σ(邻居像素值 × 权重)。

**Part 2 · Letterbox 保比例原因**：

模型训练时图像是按固定比例输入的，**强行拉伸会改变物体的纵横比**：
- 一辆方车被拉成长方形 → CNN 提取的特征跟训练分布不一致 → 识别率下降
- 人脸被压扁 → 关键点检测错位
- 文字被拉变形 → OCR 识别失败

所以 Resize 必须**保比例缩放 + 用纯色填充剩余区域**（Letterbox），让物体形状不变。

**Part 3 · 1920×1080 → 640×640 padding 计算**：

```
步骤 1：按长边定 scale
  scale = min(640/1920, 640/1080) = min(0.333, 0.593) = 0.333
  （取小的那个 = 以长边为基准，保证缩完两边都不超 640）

步骤 2：等比缩放
  new_w = 1920 × 0.333 = 640
  new_h = 1080 × 0.333 = 360

步骤 3：算 padding
  缩后是 640 × 360，目标是 640 × 640
  需要补的总像素：上下各补 (640-360)/2 = 140 px
  左右各补 (640-640)/2 = 0 px

步骤 4：填充
  在 360 高度的图上下各补 140 行黑边（或灰色 RGB 114,114,114，YOLO 默认）
```

最终：原图居中放置，**上下各 140px 黑边、左右无 padding**。

**Part 4 · 推理后的反映射**：

预测出的目标框坐标是相对 640×640 的，要还原回原图坐标：
- 减去上方 padding：`y_orig = (y_pred - 140) / 0.333`
- x 直接除 scale：`x_orig = x_pred / 0.333`

**加分回答**：
> "我在 W10 实现 Resize + Letterbox 时用 std::mdspan 表达输入输出图像，4 邻插值的代码长这样：
> ```cpp
> auto w_tl = (1-dx)*(1-dy);  auto w_tr = dx*(1-dy);
> auto w_bl = (1-dx)*dy;       auto w_br = dx*dy;
> dst(y, x, c) = img(y0, x0, c)*w_tl + img(y0, x1, c)*w_tr 
>              + img(y1, x0, c)*w_bl + img(y1, x1, c)*w_br;
> ```
> 比裸指针 + stride 计算清晰太多。这种数学公式直接落代码的写法是 mdspan 的最大价值。"

---

## 使用建议

1. **每周复习**：完成每周学习后，尝试口头回答相关题目。
2. **模拟面试**：与 Partner B 互相提问，练习表达。
3. **扩展延伸**：面试官可能会追问，准备 1-2 个深入的 follow-up 回答。
4. **代码演示**：如果有白板/在线编程环节，确保能手写核心代码片段。
