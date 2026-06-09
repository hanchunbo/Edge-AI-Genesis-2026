# C++20/23 vs 旧写法 对照速查表

> **定位**：Q1（W1-W11）涉及的全部 C++20/23 特性与旧写法的系统性对照。
> "为什么用 XX 替代 YY？"系列问题的标准答案来源。
>
> **最后更新**：2026-03-29（W12 知识闭环）

---

## 目录

1. [并发与线程管理](#1-并发与线程管理)
2. [内存管理与零拷贝视图](#2-内存管理与零拷贝视图)
3. [模板与类型约束](#3-模板与类型约束)
4. [错误处理](#4-错误处理)
5. [格式化与输出](#5-格式化与输出)
6. [多维数组抽象](#6-多维数组抽象)
7. [工程特性综合对照](#7-工程特性综合对照)

---

## 1. 并发与线程管理

### 1.1 `std::jthread` vs `std::thread`

| 维度 | `std::thread`（C++11） | `std::jthread`（C++20） |
|------|----------------------|-----------------------|
| RAII 自动 join | ❌ 析构前必须手动 join/detach，否则 `std::terminate` | ✅ 析构时自动 join |
| 停止机制 | 需手写 `std::atomic<bool>` 标志位 | ✅ 内置 `std::stop_token`，标准化协作中断 |
| 异常安全 | 异常路径容易忘 join → 崩溃 | ✅ 栈展开时自动 join，无泄漏 |
| 与 cv 配合停止 | `atomic<bool>` 无法唤醒阻塞中的 cv | ✅ `condition_variable_any` + stop_token 原子唤醒 |

**项目出处**：W5 `thread_pool.hpp`、W11 `fixed_lab.cpp`

```cpp
// C++11（旧）
std::thread t(work);
// ... 容易忘记 join
t.join();

// C++20（新）
std::jthread jt(work);  // 离开作用域自动 join，异常安全
```

---

### 1.2 `std::stop_token` vs `std::atomic<bool>`

| 维度 | `atomic<bool>`（旧） | `std::stop_token`（C++20） |
|------|---------------------|--------------------------|
| 唤醒休眠线程 | ❌ 修改标志无法唤醒 `cv.wait()` | ✅ 配合 `condition_variable_any` 原子唤醒 |
| 标准化语义 | ❌ 自定义命名，接口不统一 | ✅ 标准协议，与 jthread 深度集成 |
| 传递方式 | 需手动用 `shared_ptr` 或引用传递 | ✅ `jthread` 自动注入给工作函数第一个参数 |

**项目出处**：W5 `thread_pool.hpp` `SubmitWithToken` 接口

---

### 1.3 `std::scoped_lock` vs `std::lock_guard`（死锁预防）

| 维度 | `lock_guard`（C++11） | `scoped_lock`（C++17/C++20） |
|------|----------------------|------------------------------|
| 管理锁数量 | 只能管理 1 个 mutex | ✅ 可同时管理多个，内部使用死锁避免算法 |
| 死锁风险 | 手动控制加锁顺序，逆序即死锁 | ✅ 无论传入顺序如何，内部统一排序获取 |

**项目出处**：W11 `fixed_lab.cpp` Fix 2

```cpp
// 旧（顺序错误即死锁）
std::lock_guard<std::mutex> la(mutex_a);
std::lock_guard<std::mutex> lb(mutex_b);

// C++17/20（顺序无关，安全）
std::scoped_lock lock(mutex_a, mutex_b);
```

---

### 1.4 `alignas(64)` 缓存行对齐（False Sharing 预防）

| 维度 | 无对齐（旧） | `alignas(64)`（C++11，项目 W5 强制规范） |
|------|------------|----------------------------------------|
| False Sharing | 两个独立变量可能共享同一缓存行，一个写入使另一个失效 | ✅ 变量独占缓存行，消除伪共享 |
| 性能影响 | 高并发下性能下降 5-10× | ✅ 吞吐量提升约 30%（W5 实测） |

**项目出处**：W5 `thread_pool.hpp` 热点计数器

---

## 2. 内存管理与零拷贝视图

### 2.1 `std::span` vs 指针 + 长度

| 维度 | 裸指针 + 长度（旧） | `std::span`（C++20） |
|------|-------------------|--------------------|
| 长度绑定 | ❌ 指针和长度分离，传递时容易不同步 | ✅ 长度与指针封装在一起 |
| 边界检查 | ❌ 越界访问 UB，调试困难 | ✅ Debug 模式有边界断言 |
| 与 ranges 集成 | ❌ 需要手动封装 | ✅ 直接用于 `std::ranges` 算法 |
| 零开销 | — | ✅ 编译器完全内联，无运行时代价 |

**项目出处**：W6 `MmapLoader<T>::Span()`，W9/W10 预处理接口

```cpp
// 旧（容易出错）
void Process(const float* data, size_t n);

// C++20（类型安全，长度自带）
void Process(std::span<const float> data);
```

---

### 2.2 `std::make_unique_for_overwrite` vs `std::vector(n)`

| 维度 | `std::vector<T>(n)`（旧） | `make_unique_for_overwrite<T[]>(n)`（C++20） |
|------|--------------------------|---------------------------------------------|
| 初始化 | ❌ 强制值初始化（全域清零），浪费内存带宽 | ✅ 分配未初始化内存，无清零开销 |
| RAII | ✅ 自动释放 | ✅ unique_ptr 自动释放 |
| 适用场景 | 需要初始化保证安全性的场景 | 紧跟全量覆盖写入的大缓冲区（张量预分配） |

**风险**：必须确保分配后 100% 覆写，否则模型收到脏内存。

**项目出处**：W11 `fixed_lab.cpp` Fix 1，tech-debt.md 记录

```cpp
// 旧（多余的清零开销，1MB 浪费 ~0.1ms 带宽）
std::vector<uint8_t> buf(1024 * 1024);

// C++20（零开销分配，适合紧跟 memset/cv::resize 的场景）
auto buf = std::make_unique_for_overwrite<uint8_t[]>(1024 * 1024);
```

---

### 2.3 `mmap` + RAII vs `fstream::read`

| 维度 | `fstream::read`（旧） | `mmap` + RAII（POSIX + C++20） |
|------|----------------------|-------------------------------|
| 拷贝次数 | 内核 Cache → 用户缓冲区（1次） | ✅ 零拷贝（MMU 直接映射） |
| 内存峰值 | ≈ 2× 文件大小 | ✅ 按需 Page Fault，极低峰值 |
| 大文件启动 | 必须全量读入 | ✅ `mmap` 调用本身极快，惰性加载 |
| 资源管理 | 手动 `close()` | ✅ 析构自动 `munmap` |

**项目出处**：W6 `MmapLoader<T>`

---

## 3. 模板与类型约束

### 3.1 `Concepts` vs `SFINAE / enable_if`

| 维度 | `std::enable_if`（C++11） | `concept + requires`（C++20） |
|------|--------------------------|------------------------------|
| 错误信息 | ❌ 触发时输出数百行模板展开，极难读 | ✅ 直接报"constraints not satisfied" |
| 可读性 | ❌ 约束隐藏在模板参数列表中 | ✅ 约束直接写在函数签名处 |
| 可组合 | ❌ 嵌套 `enable_if` 极其繁琐 | ✅ `&&` / `||` 自由组合 |
| 函数重载 | 需要两套模板 + `void_t` 技巧 | ✅ 直接用 concept 约束重载候选 |

**项目出处**：W1 `SafeTensorBuffer`，W5 `ThreadPool::Submit`，W6 `TriviallyMappable`

```cpp
// 旧（SFINAE，晦涩难读）
template<typename F,
         typename = std::enable_if_t<std::is_invocable_v<F>>>
auto Submit(F&& task);

// C++20（清晰直白）
template<std::invocable F>
auto Submit(F&& task);
```

---

## 4. 错误处理

### 4.1 `std::expected` vs 异常（C++23）

| 维度 | `try-catch` 异常（旧） | `std::expected<T, E>`（C++23） |
|------|----------------------|-------------------------------|
| 运行时开销 | ❌ 栈展开有运行时代价 | ✅ 纯值语义，零开销 |
| 错误忽略 | ❌ 调用方可以不写 catch | ✅ 必须检查 expected，否则拿不到值 |
| 嵌入式兼容 | ❌ 很多平台禁用异常（`-fno-exceptions`） | ✅ 无异常依赖 |
| 跨 DLL 传播 | ❌ 异常跨 DLL 边界是 UB | ✅ 值类型，跨边界安全 |

**项目出处**：W3 `ModelScanner`，W6 `MmapLoader<T>::Load()`

```cpp
// C++23（零开销，强制处理错误）
[[nodiscard]] static auto Load(const std::filesystem::path& path)
    -> std::expected<MmapLoader, MmapError>;

// 调用方必须检查
auto result = MmapLoader<float>::Load("model.bin");
if (!result) { /* 处理错误 */ }
auto& loader = *result;  // 安全访问
```

---

## 5. 格式化与输出

### 5.1 `std::format` / `std::print` vs `printf` / `std::cout`

| 维度 | `printf`/`cout`（旧） | `std::format`（C++20）/ `std::print`（C++23） |
|------|----------------------|----------------------------------------------|
| 类型安全 | ❌ `printf` 格式符与参数类型不匹配是 UB | ✅ 编译期检查，类型安全 |
| 性能 | `cout` 链式拼接产生中间字符串 | ✅ 一次性格式化，无中间对象 |
| 可读性 | `cout << a << " " << b` 碎片化 | ✅ `std::format("{} {}", a, b)` 类似 Python |
| 国际化 | 需要特殊处理 | ✅ 格式字符串可运行时替换 |

**项目规范**：严禁使用 `printf` 或 `cout` 拼接，统一使用 `std::format`

```cpp
// 旧（类型不安全）
printf("处理 %d 帧，耗时 %.2f ms\n", count, ms);

// C++20（类型安全，编译期检查）
std::cout << std::format("处理 {} 帧，耗时 {:.2f} ms\n", count, ms);

// C++23（更简洁）
std::print("处理 {} 帧，耗时 {:.2f} ms\n", count, ms);
```

---

## 6. 多维数组抽象

### 6.1 `std::mdspan` vs 手算偏移（C++23）

| 维度 | `ptr[y * step + x * ch + c]`（旧） | `std::mdspan`（C++23） |
|------|-----------------------------------|----------------------|
| 正确性 | ❌ 行列顺序、stride 极易写错 | ✅ 编译器保证索引计算正确 |
| HWC/CHW 转换 | ❌ 需要手写三层嵌套循环 | ✅ `layout_stride` 透明表达不同 layout |
| 运行时开销 | — | ✅ 零开销，编译器完全内联，利于 SIMD 向量化 |
| 可读性 | ❌ 魔法数字满屏 | ✅ `img[y, x, c]` 直观索引 |

**项目出处**：W9 `bgr2gray.cpp`，W10 `custom_resize.cpp`

```cpp
// 旧（容易出错）
uint8_t pixel = src_ptr[y * src.step[0] + x * 3 + c];

// C++23（安全直观）
namespace stdex = std::experimental;
auto img = stdex::mdspan(src.data, src.rows, src.cols, 3);
uint8_t pixel = img[y, x, c];
```

---

## 7. 工程特性综合对照

### 一览总结表

| 旧写法 | C++20/23 替代 | Q1 出处 | 核心收益 |
|--------|--------------|---------|---------|
| `std::thread` | `std::jthread` | W4→W5→W11 | RAII 自动 join，异常安全 |
| `atomic<bool>` 停止标志 | `std::stop_token` | W5 | 标准化，可唤醒休眠线程 |
| `lock_guard` × 2（逆序） | `std::scoped_lock` | W11 | 死锁避免算法，顺序无关 |
| 裸指针 + 长度 | `std::span<T>` | W6/W9/W10 | 类型安全，长度随行 |
| `vector<T>(n)` 大缓冲 | `make_unique_for_overwrite` | W11 | 消除冗余清零，节省带宽 |
| `fstream::read` | `mmap` + RAII | W6 | 零拷贝，惰性加载 |
| `enable_if` / SFINAE | `concept` + `requires` | W1/W5/W6 | 清晰错误信息，可读约束 |
| `try-catch` 异常 | `std::expected<T,E>` | W3/W6 | 零开销，强制处理 |
| `printf` / `cout` 拼接 | `std::format` / `std::print` | 全程 | 类型安全，可读性 |
| `ptr[y*step+x*3+c]` | `std::mdspan` | W9/W10 | 零开销多维抽象，防偏移 Bug |
| 无对齐热点变量 | `alignas(64)` | W5 | 消除 False Sharing |

---

## 高频追问速查

**Q: 为什么用 `jthread` 不用 `thread`？**
> RAII 自动 join，异常路径不会意外崩溃；内置 stop_token 标准化中断机制，配合 condition_variable_any 可唤醒休眠线程。

**Q: `std::span` 有运行时开销吗？**
> 没有。它是一个"指针 + 长度"的零开销视图，编译器会完全内联，等价于传裸指针 + 长度但具备类型安全。

**Q: `std::expected` 和 `std::optional` 有什么区别？**
> `optional<T>` 只表达"有值或无值"，无值时不携带任何错误信息；`expected<T, E>` 表达"成功值 T 或错误 E"，错误路径有明确类型。

**Q: `make_unique_for_overwrite` 什么时候用，什么时候不应该用？**
> 当分配后紧跟全量覆写（如 `memset`、`cv::resize` 填充）时用，可省去清零带宽；当缓冲区可能部分使用时不应该用，未初始化内存读入模型会产生随机脏值影响推理结果。

**Q: `std::mdspan` 和 `cv::Mat` 有什么关系？**
> `cv::Mat` 是 OpenCV 的图像类，带引用计数和图像元数据；`mdspan` 是零开销的多维视图，不持有数据，直接指向 `cv::Mat.data`，类似 `std::span` 的二维版本，目的是以类型安全的方式抽象多维内存布局（HWC/CHW）。
