# CLAUDE.md

> 本文件是给 Claude Code AI 助手的项目说明书。Claude 每次进入项目时都会读取此文件，
> 按照这里的规范来写代码、命名、注释和提交。

## 当前进度

**Q2 W15（分类推理端到端闭环）已完成；W1–W14 + W14.5（CUDA EP）全部完成。各周细节见对应模块 `notes.md`，可复用概念见 `docs/notes/`；遗留 ResNet18 对比（降级可选）、VPS CPU EP 环境（待定）。**
每开始新的 week，必须更新此处的进度描述（一句话：当前周 + 状态 + 遗留，细节进模块 notes）。

## Build（构建速查）

编译 / 测试 / 格式检查命令见 `README.md` Quick Start 节（含离线/ASAN/TSAN 场景）；
工具链约束见 C++ Standards 节（必须 Ninja）与 Commit Checklist 节（必须 clang-format-21）。

> 查目标名：`cmake --build build --target help 2>/dev/null | grep "w[0-9]"`

## Architecture（目录结构）

- `01_Linux_CPP_Foundations/` — C++20/23 系统编程（当前活跃，W1-W8 已完成，W9 进行中）
- `02_Inference_Analysis/`、`03_Hardware_Acceleration/`、`04_System_Integration/` — 规划中

**周次模块**（如 `w9_opencv_optimized/`）各自有独立 `CMakeLists.txt`，
从根目录通过 `add_subdirectory` 引入，使用独立命名空间（`w1`、`w2`… `w9`）。

**开发分支**：日常直接在 `dev` 上开发（不再开 feature 分支）。合入 `main` 的流程与铁律见下方 「Branch & Merge Policy」 节。

### Notes 知识库分工（周笔记 vs 主题库）

- **周笔记**（`wXX/notes.md`）：模块专属——概述、Mermaid 图、本模块设计决策、踩坑（带 commit）、测试/基准、编译运行命令。
- **主题库**（`docs/notes/*.md`）：可复用概念——按主题组织，三段式（是什么 / 为什么 / 坑 + 实战出处），格式与索引见 `docs/notes/README.md`。
- **判定**：这段话离开本模块代码还成立吗？成立 → 主题库；只对本模块有意义 → 周笔记。
- **首次即入库**：可复用概念**首次出现就直接写进主题库**（不在周笔记暂存、不批量迁移）；周笔记只留「本模块怎么用」+ 向上链接。
- **单一事实源**：同一概念正文只在主题库一份；双向链接（周笔记 → 主题库看概念，主题库 → 周笔记看 Mermaid / 语境）。

## C++ Standards and Conventions（C++ 规范）

- **默认标准**：C++20。部分目标需要 C++23（如 W1 用到 `std::expected`，需在 CMakeLists 单独指定）。
- **编译器**：首选 GCC 15+（`CMAKE_CXX_COMPILER=g++-15`），CI/生产构建必须使用。本地开发若装不上 g++-15，根 CMakeLists 会自动回退到 g++-14（仅作为开发兜底，C++23 的 `std::expected` / `import std` / `std::mdspan` 等特性受限）。不支持 clang 或更旧版 GCC。
- **构建工具**：必须用 Ninja（`-G Ninja`），C++20 具名模块不支持 Unix Makefiles 生成器。
- **风格**：Google C++ Style Guide（2026 增强版），由 `.clang-format` 和 `.clang-tidy` 自动检查。

### Naming（由 .clang-tidy 强制，下列为易写错的非常规约定）

类名/函数名大驼峰（`GetThreadCount()`）、普通变量 snake_case、成员变量末尾 `_`（`data_`）、常量 `k` 前缀大驼峰（`kMaxBufferSize`）、命名空间全小写（`w5`）。格式（缩进/列宽/括号/指针位/include 序）由 `.clang-format` 自动处理，无需记忆。

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

每个 `.cpp` / `.hpp` 文件必须以此开头（SPDX 主流写法，许可证文本统一在根 `LICENSE`）：

```cpp
// SPDX-License-Identifier: MIT
//
// 文件功能：[用一句话描述这个文件做什么]
```

## Testing（测试规范）

使用 Google Test v1.15.2，通过 CMake FetchContent 自动下载。
测试目标链接 `GTest::gtest_main` 和 `Threads::Threads`，
用 `add_test()` 注册到 CTest，命名格式为 `W5_ThreadPoolTest`（周次 + 模块名）。

## Commit Checklist（提交前必检）

每次 commit 前，按顺序执行以下检查：

1. **clang-format**：确保格式检查通过（CI 强制，**必须用 clang-format-21**，与 CI 版本严格对齐——v18/v19/v20 在中文列宽、include 排序等细节上输出会与 v21 不一致，本地通过不代表 CI 通过）
   ```bash
   find . -maxdepth 3 -regex '.*0[1-4]_.*' \( -name "*.cpp" -o -name "*.hpp" \) | xargs clang-format-21 --dry-run --Werror
   ```

2. **文档进度同步**：确认 `README.md`、`docs/Q2.md`、`docs/tech-debt.md`、模块 `notes.md` 进度与代码一致，**不符则先更新再 commit**。

3. **环境依赖同步**：若本次开发安装了新工具（编译器、调试器、覆盖率工具等），必须同步更新 `README.md` 的「前提条件」安装命令。目的：VPS 上积累的隐式环境依赖若不记录，换机器（如 WSL、CI）时会批量复现已解决的问题。

4. **commit author**：格式为 `hanchunbo <hanchunbo@users.noreply.github.com>`

## Branch & Merge Policy（分支与合入流程）

**铁律：Claude 不能未经用户明确许可就把任何改动合入 `main`。**

日常**直接在 `dev` 上开发**，分两步：

1. **在 `dev` 上 commit / push** — 无需询问
   - 先 `git fetch origin`，看远端是否有用户直推的提交，避免漏改基线
   - commit 后 `git push origin dev`

2. **合入 `main`** — **必须先停下来问用户**
   - 询问形式：「这批改动已在 dev（commit `xxxxx`），要现在合入 main 吗？」
   - 用户明确说"合"/"可以"/"go" 才执行；说"先不合"/"留在 dev" → 停在 dev，**绝不自作主张**

### 为什么 & 注意

- `main` 是对外展示的产出快照，合入意味着"这版可以对外讲"，需用户自己评估
- 远端 `main` 有 "Changes must be made through a pull request" 保护规则，直推会留 `Bypassed rule violations` 警告
- 不绕过钩子：不加 `--no-verify` / `--no-gpg-sign`；规则触发时 stop & fix
- 历史已合入 `main` 的部分不回滚——仅约束未来行为

## CI（持续集成）

GitHub Actions（`.github/workflows/ci.yml`）在向 `main` 推送或发起 PR 时自动触发，包含两个检查：
1. **build-and-test**：cmake 配置 → 编译 → ctest 跑所有测试
2. **format-check**：对所有 `.cpp`/`.hpp` 文件执行 `clang-format-21 --dry-run --Werror`（版本与本地必须严格一致），格式不对则失败
