---
trigger: always_on
---

# Google C++ Style Guide 规范 (2026 增强版 - C++20/23)

## 1. 命名规范
- **文件命名**：全部小写，单词间用下划线连接，如 `my_useful_class.cc`。
- **类名/结构体**：使用大驼峰（PascalCase），如 `TensorManager`。
- **函数名**：使用大驼峰（PascalCase），注意：Google 允许大驼峰或小驼峰，但本项目统一使用 **大驼峰**，如 `ExecuteInference()`。
- **局部变量**：全部小写，单词间下划线，如 `image_height`。
- **类成员变量**：小写加后缀下划线，如 `buffer_size_`。
- **常量**：以小写 k 开头，使用大驼峰，如 `kMaxBufferSize`。
- **枚举值**：使用大驼峰，如 `kRedValue`。

## 2. 格式与排版
- **缩进**：统一使用 **2 个空格**，严禁使用制表符（Tab）。
- **行宽**：限制在 80 个字符以内。
- **大括号**：左大括号不换行，右大括号独占一行（K&R 风格）。
- **空行**：函数内部尽量少使用空行，逻辑块之间可以加一个空行。

## 3. 现代 C++ 特性 (C++20/23)

### 3.1 核心语言特性
- **标准版本**：项目强制使用 **C++20** 标准作为基准，鼓励在编译器支持的情况下使用 **C++23** 特性。
- **概念 (Concepts)**：模板约束必须使用 C++20 `concept` 和 `requires` 子句，**严禁**继续使用 `std::enable_if` 或 SFINAE 技巧。
- **头文件保护**：使用 `#ifndef PATH_FILENAME_H_` 格式，而非 `#pragma once`（对齐 Google 传统规范）。
- **类型安全**：
  - 空指针必须使用 `nullptr`。
  - 禁止 C 风格强制转换，必须使用 `static_cast` 或 `reinterpret_cast`。
  - 派生类虚函数必须显式标注 `override` 或 `final`。
- **Constexpr 优先**：凡是能在编译期计算的逻辑（如张量维度的计算、静态配置等），必须优先使用 `constexpr` 或 `consteval`，以实现运行时的零开销。

### 3.2 并发编程 (Concurrency)
- **线程管理**：强制使用 `std::jthread` (C++20) 替代 `std::thread`，利用其 RAII 自动汇合特性防止资源泄漏。
- **停止机制**：必须配合 `std::stop_token` 实现线程的优雅停止，禁止使用 `volatile bool` 标志位。

### 3.3 内存与视图 (Memory & Views)
- **零拷贝视图**：
  - 一维数据传递强制使用 `std::span` (C++20) 替代指针+长度。
  - 多维张量数据强制使用 `std::mdspan` (C++23) 处理。
- **内存对齐**：边缘侧高性能代码中的关键数据结构必须使用 `alignas(64)` 确保缓存行对齐。
- **对象拷贝**：禁止非必要的大对象（如图像、张量）拷贝，应优先使用移动语义或视图。

### 3.4 日志与输出
- **格式化**：严禁使用 `printf` 或 `std::cout` 拼接字符串。统一使用 `std::format` (C++20) 或 `std::print` (C++23) 进行类型安全的日志输出。

## 4. 注释规范
- 仅使用 `//` 进行单行注释，不建议使用 `/* */`（Doxygen 文档注释 `///` 属单行注释，允许）。
- 每个文件开头必须包含版权声明和文件功能描述。
- 每个类和非平凡函数前必须有功能描述注释。
- **W16 起**：注释规范以 `CLAUDE.md`「Documentation & Comments」节为准——
  C++ 用 Doxygen `///` 一句话 brief，坑用 `@pre`/`@warning`/`@note` 标准标签，
  Python 用 Google 风格 docstring。演进式三段注释仅限 W1–W15 存档模块。