# CLAUDE.md

> 本文件是给 Claude Code AI 助手的项目说明书。Claude 每次进入项目时都会读取此文件，
> 按照这里的规范来写代码、命名、注释和提交。

## 当前进度

**当前处于 Q1 W9（OpenCV 底层实战）阶段，W1-W8 已全部完成。**
每开始新的 week，必须更新此处的进度描述。

## Build（构建速查）

详细命令见 `README.md` Quick Start 节（含离线/ASAN/TSAN 场景）。常用命令：

```bash
cmake -B build -S . -DCMAKE_CXX_COMPILER=g++-15 -G Ninja  # 首次配置（Ninja 必须，C++20 模块不支持 Unix Makefiles）
cmake --build build --target <目标名> -j$(nproc)            # 编译当前周目标
ctest --test-dir build -R "W9_" --output-on-failure        # 当前周测试
find . -maxdepth 3 -regex '.*0[1-4]_.*' \( -name "*.cpp" -o -name "*.hpp" \) | xargs clang-format --dry-run --Werror  # 格式检查（CI 强制）
```

> 查目标名：`cmake --build build --target help 2>/dev/null | grep "w[0-9]"`

## Architecture（目录结构）

- `01_Linux_CPP_Foundations/` — C++20/23 系统编程（当前活跃，W1-W8 已完成，W9 进行中）
- `02_Inference_Analysis/`、`03_Hardware_Acceleration/`、`04_System_Integration/` — 规划中

**周次模块**（如 `w9_opencv_optimized/`）各自有独立 `CMakeLists.txt`，
从根目录通过 `add_subdirectory` 引入，使用独立命名空间（`w1`、`w2`… `w9`）。

**分支命名**：`dev-W{N}-{Feature}-chunbo`，完成后合入 `main`。

## C++ Standards and Conventions（C++ 规范）

- **默认标准**：C++20。部分目标需要 C++23（如 W1 用到 `std::expected`，需在 CMakeLists 单独指定）。
- **编译器**：只用 GCC 15+，通过 `CMAKE_CXX_COMPILER=g++-15` 指定，不支持 clang 或旧版 GCC。
- **构建工具**：必须用 Ninja（`-G Ninja`），C++20 具名模块不支持 Unix Makefiles 生成器。
- **风格**：Google C++ Style Guide（2026 增强版），由 `.clang-format` 和 `.clang-tidy` 自动检查。

### Naming（命名规范，来自 .clang-tidy）

| 元素 | 规范 | 示例 |
|---------------|---------------------------|----------------------|
| 类名 | 大驼峰 | `SafeTensorBuffer` |
| 函数名 | 大驼峰 | `GetThreadCount()` |
| 普通变量 | 下划线小写 | `thread_count` |
| 成员变量 | 下划线小写 + 末尾 `_` | `data_` |
| 常量 | `k` 前缀 + 大驼峰 | `kMaxBufferSize` |
| 命名空间 | 全小写 | `w5` |

### Formatting（格式规范，来自 .clang-format）

- 缩进 2 空格，禁止 Tab，每行最多 80 字符
- K&R 风格大括号（开括号不换行）
- 指针靠左：`int* x`，不写 `int *x`
- `#include` 顺序：项目头文件（`"..."`）在前，标准库（`<...>`）在后

### Resource Constraints（边缘 AI 资源约束）

- **内存**：优先用栈分配和 `std::span`。非必要不用 `std::shared_ptr`（引用计数有开销）；独占所有权用 `std::unique_ptr`。
- **性能**：所有有返回值的函数加 `[[nodiscard]]`（防止调用方丢弃返回值导致 bug）。元数据和查找表优先用 `consteval`/`constexpr` 在编译期计算。

### Mandatory Modern C++ Features（强制使用的现代 C++ 特性）

以下是必须用新写法替代旧写法的地方，`.agent/rules/google_style_guide.md` 中有强制要求：

| 用这个（新）             | 替代这个（旧）                       | 原因 |
|--------------------------|--------------------------------------|------|
| `std::jthread`           | `std::thread` + 手动 join            | 自动 join，RAII 安全 |
| `std::stop_token`        | `std::atomic<bool>` 标志位           | 标准化的线程停止机制 |
| `std::span<T>`           | 裸指针 + size 参数                   | 零开销的安全视图 |
| `std::format`            | `printf` / iostream 拼接             | 类型安全、可读性强 |
| Concepts                 | SFINAE / `std::enable_if`            | 约束更清晰，报错更友好 |
| `std::expected` (C++23)  | 异常或错误码                         | 无异常的显式错误传播 |
| `alignas(64)`            | 手动填充 padding 字节                | 避免 CPU 缓存行伪共享 |

### Documentation & Comments（注释与文档规范）

- **语言**：所有注释必须用**简体中文**，禁止英文注释（英文标识符除外）。
- **深度**：避免"翻译代码"式的废话注释。重点解释**为什么这样做**、**有什么隐患**，而不是"这行代码做了什么"。
- **函数头**：复杂函数使用 Doxygen 风格，包含 `@param`、`@return`、`@throws`（如有异常路径）。
- **逻辑块**：复杂算法（如 mmap 加载器、ThreadPool）需在关键步骤旁加中文分步说明，让读者无需查文档即可理解执行流程。

### Evolutionary Comment Pattern（演进式注释）

源文件使用以下固定注释格式，解释为什么用新写法代替旧写法：

```cpp
// [Legacy C++11/17]: <描述旧版实现方式>
// [Pain Point]: <说明旧版的痛点>
// [Modern C++20/23]: <说明新特性如何解决>
```

### File Header（文件头模板）

每个 `.cpp` / `.hpp` 文件必须以此开头：

```cpp
// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：[用一句话描述这个文件做什么]
// ============================================================================
```

## Testing（测试规范）

使用 Google Test v1.15.2，通过 CMake FetchContent 自动下载。
测试目标链接 `GTest::gtest_main` 和 `Threads::Threads`，
用 `add_test()` 注册到 CTest，命名格式为 `W5_ThreadPoolTest`（周次 + 模块名）。

## Commit Checklist（提交前必检）

每次 commit 前，按顺序执行以下检查：

1. **clang-format**：确保格式检查通过（CI 强制）
   ```bash
   find . -maxdepth 3 -regex '.*0[1-4]_.*' \( -name "*.cpp" -o -name "*.hpp" \) | xargs clang-format --dry-run --Werror
   ```

2. **文档进度同步**：确认 `README.md`、`docs/Q1.md`、`docs/tech-debt.md`、模块 `notes.md` 进度与代码一致，**不符则先更新再 commit**。

3. **环境依赖同步**：若本次开发安装了新工具（编译器、调试器、覆盖率工具等），必须同步更新 `README.md` 的「前提条件」安装命令。目的：VPS 上积累的隐式环境依赖若不记录，换机器（如 WSL、CI）时会批量复现已解决的问题。

4. **commit author**：格式为 `Hanchunbo <hanchunbo@users.noreply.github.com>`，并附 `Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>`

## CI（持续集成）

GitHub Actions（`.github/workflows/ci.yml`）在向 `main` 推送或发起 PR 时自动触发，包含两个检查：
1. **build-and-test**：cmake 配置 → 编译 → ctest 跑所有测试
2. **format-check**：对所有 `.cpp`/`.hpp` 文件执行 `clang-format --dry-run --Werror`，格式不对则失败
