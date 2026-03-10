# Q1 C++ 基础面试题库 (W1-W5)

> **使用说明**：本文档包含 Q1 前 5 周学习内容相关的高频面试题。每道题包含问题、考察点、参考答案和加分回答。
>
> **更新记录**：2026-02-06 首次创建，12 道题目覆盖 W1-W5。

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

## 使用建议

1. **每周复习**：完成每周学习后，尝试口头回答相关题目。
2. **模拟面试**：与 Partner B 互相提问，练习表达。
3. **扩展延伸**：面试官可能会追问，准备 1-2 个深入的 follow-up 回答。
4. **代码演示**：如果有白板/在线编程环节，确保能手写核心代码片段。
