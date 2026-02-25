# 技术债记录

> 最后更新：2026-02-26

---

## [FIXED] W1-W5 clang-format 不通过

**发现**：2026-02-26（W6 开发期间）
**修复**：2026-02-26，执行 `clang-format -i` 后全部通过，编译和测试均未受影响。

---

## [FIXED] W1-W4 缺少 GTest 单元测试

**修复**：2026-02-26

| 模块 | 测试文件 | 用例数 |
|------|---------|--------|
| W1 | `w1_memory_safety_test.cpp` | 17 |
| W2 | `w2_move_semantics_test.cpp` | 21 |
| W3 | `w3_filesystem_test.cpp`（附带提取 `model_scanner.hpp`） | 14 |
| W4 | `w4_threading_test.cpp`（附带提取 `producer_consumer.hpp`） | 12 |

全部 6 个测试套件 `ctest` 通过（共 84 个用例）。

---

## [OPEN] W1-W5 缺少 `[[nodiscard]]` 标注

**严重度**：🟡 中（规范要求所有非 void 函数必须标注，漏标会导致忽略返回值的 bug 无法被编译器捕获）

共计约 **26 处**遗漏，按模块：

| 模块 | 遗漏函数（举例） | 数量 |
|------|----------------|------|
| W1 | `Create()`、`View()`、`At()`、`MakeTensorBuffer()` | 5 |
| W2 | `Data()`、`Size()`、`Valid()`、`View()`、`RowView()`、`CalculateAveragePixel()` | 8 |
| W3 | `IsValidPath()`、`Scan()`、`GetHumanReadableSize()` | 3 |
| W4 | `Pop()`、`IsStopped()` 及多个 getter | 5 |
| W5 | `Submit()`、`SubmitWithToken()`、`GetThreadCount()` 等 getter | 5 |

---

## [OPEN] W4 使用 `std::thread` 而非 `std::jthread`

**严重度**：🟡 中（违反项目规范，需手动 join，有线程泄漏风险）

`producer_consumer.cpp` 中 `ImageProducer` 和 `ImageConsumer` 的实现：

```cpp
// 当前（C++11 风格）
std::thread thread_;
void Start() { thread_ = std::thread(&ImageProducer::ProducerLoop, this, n); }
void Join()  { if (thread_.joinable()) thread_.join(); }

// 应改为（C++20）
std::jthread thread_;  // RAII 自动 join，析构时无需手动调用
void Start() { thread_ = std::jthread(&ImageProducer::ProducerLoop, this, n); }
```

---

## [OPEN] W3 ModelScanner 未做 hpp/cpp 分离

**严重度**：🟡 中（类定义和 `main()` 混在同一个 `.cpp`，无法被其他模块引用）

`model_scanner.cpp` 中 `ModelScanner` 类及相关类型应拆分为：
- `model_scanner.hpp`：类声明、`ModelFileInfo` 结构体
- `model_scanner.cpp`：实现 + `main()` 演示

---

## [OPEN] W4 `.cpp` 文件含不当的头文件保护

**严重度**：🟢 低（不影响编译，但违反 C++ 惯例）

`producer_consumer.cpp` 第 48-49 行和末尾有 `#ifndef` / `#endif` 保护宏，
`.cpp` 文件不应有头文件保护，应直接删除。

---

## [OPEN] W2-W4 CMakeLists.txt 未显式设置 `CXX_STANDARD`

**严重度**：🟢 低（当前依赖根目录的 C++20 默认值，未来升级时容易漏配）

W2、W3、W4 的 `CMakeLists.txt` 应补充：
```cmake
set_target_properties(<target> PROPERTIES CXX_STANDARD 20)
```
（若将来引入 `std::expected` 则改为 23，与 W1/W6 保持一致）
