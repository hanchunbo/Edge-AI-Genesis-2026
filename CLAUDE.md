# CLAUDE.md

> 本文件是给 Claude Code AI 助手的项目说明书。Claude 每次进入项目时都会读取此文件，
> 按照这里的规范来写代码、命名、注释和提交。

## Project Overview（项目概述）

### 项目背景与目标

**Edge-AI-Genesis-2026** 是一个面向 AI 部署工程师转型的实战学习计划，核心目标是：
> 用现代 C++20/23 构建**可演示、可测量、有技术深度**的端侧 AI 推理工程能力，
> 支撑从软件外包（18K）向 AI 部署/应用专家（30K+）的职业跳转。

技术路线聚焦**不可替代性**：AI 推理的终局在端侧（手机/车机/IoT），
需要同时懂模型 + 懂 C++ 工程 + 懂资源约束，纯算法工程师和纯业务 C++ 工程师都做不到这一点。

### 当前进度

**当前处于 Q1 W7（CMake 工程化）阶段，W1-W6 已全部完成。**
每开始新的 week，必须更新此处的进度描述。

### 周次速查表（Q1）

| 阶段 | 周次 | 主题 | 关键技术 | 状态 |
|------|------|------|----------|------|
| 一 | W1 | 内存安全与 RAII | `unique_ptr`, `shared_ptr`, Concepts | ✅ |
| 一 | W2 | 移动语义与零拷贝 | `std::span`, 右值引用 | ✅ |
| 一 | W3 | C++20 特性实战 | `std::format`, `string_view` | ✅ |
| 一 | W4 | 多线程与任务同步 | `counting_semaphore` | ✅ |
| 一 | W5 | 通用线程池架构 | `jthread`, `stop_token`, `alignas(64)` | ✅ |
| 二 | W6 | 高性能 I/O (mmap) | `mmap`, `std::span`, `std::expected` | ✅ |
| 二 | W7 | CMake 工程化 | C++20 模块支持 | 🚧 进行中 |
| 二 | W8 | CMake 进阶 | 待定 | ⬜ |
| 三 | W9-W11 | OpenCV 底层实战 | `std::mdspan` (C++23), SIMD | ⬜ |
| 三 | W12-W13 | 阶段性项目 | 全栈整合 | ⬜ |

## Build Commands（构建命令）

### 首次配置

```bash
# 必须用 g++-13，清理 build/ 后也需要重新执行
cmake -B build -S . -DCMAKE_CXX_COMPILER=g++-13
```

### 当前周次模块的编译与验证（最常用）

每个周次的**目标名**就是该模块 `CMakeLists.txt` 里 `add_executable(xxx ...)` 的 `xxx`；
**CTest 名**就是 `add_test(NAME Yyy ...)` 的 `Yyy`。

```bash
# 只编译当前周的目标（以 W6 为例，有多个目标时空格分隔）
cmake --build build --target w6_mmap_loader_test w6_mmap_benchmark -j$(nproc)

# 只跑当前周的测试（-R 后面是正则，W6_ 可匹配该周所有 CTest 条目）
ctest --test-dir build -R "W6_" --output-on-failure

# 快速验证功能正确性（只跑 GTest，不跑耗时的 benchmark）
ctest --test-dir build -R W6_MmapLoaderTest --output-on-failure
```

> **如何查目标名**：`cmake --build build --target help 2>/dev/null | grep "w[0-9]"`
> 或直接看该周 `CMakeLists.txt` 里的 `add_executable` / `add_test`。

### 整体项目的编译与验证

```bash
# 编译所有目标
cmake --build build -j$(nproc)

# 跑所有测试
ctest --test-dir build --output-on-failure
```

### 格式检查（CI 强制，提交前必须通过）

```bash
# 检查（失败则 PR 无法合入）
find . -maxdepth 3 -regex '.*0[1-4]_.*' \( -name "*.cpp" -o -name "*.hpp" \) | xargs clang-format --dry-run --Werror

# 自动修复（检查失败后先跑这条，再重新检查）
find . -maxdepth 3 -regex '.*0[1-4]_.*' \( -name "*.cpp" -o -name "*.hpp" \) | xargs clang-format -i
```

### 调试专用（排查 bug 时才需要）

```bash
# 内存越界 / use-after-free 检测
cmake -B build -S . -DCMAKE_CXX_COMPILER=g++-13 -DENABLE_ASAN=ON
cmake --build build --target <目标名> -j$(nproc)
ctest --test-dir build -R "<周次正则>" --output-on-failure

# 线程竞争检测
cmake -B build -S . -DCMAKE_CXX_COMPILER=g++-13 -DENABLE_TSAN=ON
cmake --build build --target <目标名> -j$(nproc)
ctest --test-dir build -R "<周次正则>" --output-on-failure
```

## Architecture（目录结构）

**季度目录**对应不同学习阶段：
- `01_Linux_CPP_Foundations/` — C++20/23 系统编程（当前进行中，W1-W6 已完成，W7 进行中）
- `02_Inference_Analysis/` — ONNX Runtime 推理与模型优化（规划中）
- `03_Hardware_Acceleration/` — TensorRT、CUDA 加速（规划中）
- `04_System_Integration/` — 完整部署流水线（规划中）

**周次模块**（如 `w1_memory_safety/`、`w2_move_semantics/`）各自有独立的 `CMakeLists.txt`，
从根目录的 CMakeLists.txt 通过 `add_subdirectory` 引入。每个模块使用独立命名空间（`w1`、`w2`、`w5` 等）。

**分支命名规则**：每个周次模块用 `dev-W{N}-{Feature}-chunbo`，完成后合入 `main`。

## C++ Standards and Conventions（C++ 规范）

- **默认标准**：C++20。部分目标需要 C++23（如 W1 用到 `std::expected`，需在 CMakeLists 单独指定）。
- **编译器**：只用 GCC 13+，通过 `CMAKE_CXX_COMPILER=g++-13` 指定，不支持 clang 或旧版 GCC。
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

源文件使用以下固定注释格式，解释为什么用新写法代替旧写法（尖括号内容用中文填写）：

```cpp
// [Legacy C++11/17]: <描述旧版实现方式，如：手动 join std::thread>
// [Pain Point]: <说明旧版的痛点，如：忘记 join 导致线程泄漏或程序崩溃>
// [Modern C++20/23]: <说明新特性如何解决，如：std::jthread 析构时自动汇合，RAII 安全>
```

实际示例（来自 W5 ThreadPool）：

```cpp
// [Legacy C++11/17]: std::thread + 手动 join + atomic<bool> 停止标志
// [Pain Point]: 忘记 join 导致程序崩溃；atomic 标志与条件变量配合繁琐，易死锁
// [Modern C++20/23]: std::jthread 析构自动汇合；stop_token 标准化停止，无需额外标志
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

2. **文档进度同步**：检查 `docs/` 目录中的以下文件，确认进度描述与代码实际状态一致，**如有不符必须先更新文档再 commit**：
   - `README.md` — 当前周次的任务状态（✅ / 🚧 / ⬜）
   - `docs/Q1.md` 或 `docs/Q2.md` — 对应季度阶段进度
   - `docs/career_assessment.md` — 若涉及里程碑节点（P0 门槛达成等）
   - `docs/tech-debt.md` — 若本次修复了已记录的技术债，将 `[OPEN]` 改为 `[FIXED]`
   - 对应模块的 `notes.md` — Checklist 项目打勾，新增验证命令等

3. **commit author**：格式为 `Hanchunbo <hanchunbo@users.noreply.github.com>`，并附 `Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>`

## CI（持续集成）

GitHub Actions（`.github/workflows/ci.yml`）在向 `main` 推送或发起 PR 时自动触发，包含两个检查：
1. **build-and-test**：cmake 配置 → 编译 → ctest 跑所有测试
2. **format-check**：对所有 `.cpp`/`.hpp` 文件执行 `clang-format --dry-run --Werror`，格式不对则失败
