# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Edge-AI-Genesis-2026 is a structured multi-quarter career acceleration program for AI systems engineering using modern C++20/23. The codebase is organized by quarter (Q1-Q4) and by weekly modules within each quarter. Currently in Q1 W6 (mmap loader).

## Build Commands

```bash
# Configure (GCC 13 required)
cmake -B build -DCMAKE_CXX_COMPILER=g++-13

# Build all targets
cmake --build build -j$(nproc)

# Run all tests
ctest --test-dir build --output-on-failure

# Run a single test by name
ctest --test-dir build -R W5_ThreadPoolTest --output-on-failure

# Build with sanitizers
cmake -B build -DCMAKE_CXX_COMPILER=g++-13 -DENABLE_TSAN=ON   # ThreadSanitizer
cmake -B build -DCMAKE_CXX_COMPILER=g++-13 -DENABLE_ASAN=ON   # AddressSanitizer

# Format check (must pass CI) — scans all quarter directories
find . -maxdepth 3 -regex '.*0[1-4]_.*' \( -name "*.cpp" -o -name "*.hpp" \) | xargs clang-format --dry-run --Werror

# Auto-format
find . -maxdepth 3 -regex '.*0[1-4]_.*' \( -name "*.cpp" -o -name "*.hpp" \) | xargs clang-format -i
```

## Architecture

**Quarter directories** map to learning phases:
- `01_Linux_CPP_Foundations/` — C++20/23 systems programming (active, W1-W6+)
- `02_Inference_Analysis/` — ONNX Runtime, model optimization (planned)
- `03_Hardware_Acceleration/` — TensorRT, CUDA (planned)
- `04_System_Integration/` — Full deployment pipelines (planned)

**Weekly modules** (`w1_memory_safety/`, `w2_move_semantics/`, etc.) each have their own `CMakeLists.txt` added as subdirectories from the root CMakeLists.txt. Each module lives in its own namespace (`w1`, `w2`, `w5`, etc.).

**Branch convention**: `dev-W{N}-{Feature}-chunbo` per weekly module, merged to `main` when complete.

## C++ Standards and Conventions

- **Default standard**: C++20. Some targets require C++23 (e.g., W1 uses `std::expected`).
- **Compiler**: GCC 13+ only. Set via `CMAKE_CXX_COMPILER=g++-13`.
- **Style**: Google C++ Style Guide (2026 enhanced). Enforced by `.clang-format` and `.clang-tidy`.

### Naming (from .clang-tidy)

| Element       | Convention                | Example              |
|---------------|---------------------------|----------------------|
| Classes       | CamelCase                 | `SafeTensorBuffer`   |
| Functions     | CamelCase                 | `GetThreadCount()`   |
| Variables     | lower_case                | `thread_count`       |
| Members       | lower_case + trailing `_` | `data_`              |
| Constants     | k + CamelCase             | `kMaxBufferSize`     |
| Namespaces    | lower_case                | `w5`                 |

### Formatting (from .clang-format)

- 2-space indent, no tabs, 80-column limit
- K&R brace style (attached opening braces)
- Left pointer alignment (`int* x`)
- Sorted includes: project headers first (`"..."`), then stdlib (`<...>`)

### Resource Constraints (Edge AI Target)

- **Memory**: Prioritize stack allocation and `std::span`. Avoid `std::shared_ptr` unless required for shared lifetime management; prefer `std::unique_ptr` for ownership.
- **Performance**: Use `[[nodiscard]]` on all non-void functions. Favor `consteval`/`constexpr` for metadata and lookup tables.

### Mandatory Modern C++ Features

Prefer these over legacy equivalents — this is enforced by the style guide in `.agent/rules/google_style_guide.md`:

| Use                      | Instead of                           |
|--------------------------|--------------------------------------|
| `std::jthread`           | `std::thread` + manual join          |
| `std::stop_token`        | `std::atomic<bool>` flags            |
| `std::span<T>`           | raw pointer + size                   |
| `std::format`            | `printf` / iostream formatting       |
| Concepts                 | SFINAE / `std::enable_if`            |
| `std::expected` (C++23)  | exceptions or error codes            |
| `alignas(64)`            | manual padding for cache lines       |

### Documentation & Comments (注释与文档规范)

- **Language**: All comments must be written in **Chinese (Simplified)**. (所有注释必须使用简体中文).
- **Detail Level**: Avoid trivial comments. Focus on the **"Why"** and **"How"**, not just the "What". (注释需详细，重点解释逻辑缘由和实现细节，而非简单的代码转述).
- **Function Headers**: Use Doxygen-style for complex functions, including `@param`, `@return`, and `@throws`.
- **Logic Blocks**: For complex algorithms (like mmap loader or ThreadPool), provide step-by-step explanations within the code. (针对复杂逻辑，需提供分步骤的中文解释).

### Evolutionary Comment Pattern (演进式注释)

Source files use a distinctive comment pattern explaining the C++ evolution rationale:
```cpp
// [Legacy C++11/17]: <old approach>
// [Pain Point]: <why it was problematic>
// [Modern C++20/23]: <how the new feature solves it>
```

### File Header

```cpp
// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：[Brief description]
// ============================================================================
```

## Testing

Google Test v1.15.2 via FetchContent. Test targets link against `GTest::gtest_main` and `Threads::Threads`. Tests are registered with `add_test()` and use CTest naming like `W5_ThreadPoolTest`.

## CI

GitHub Actions (`.github/workflows/ci.yml`) runs on push/PR to `main`:
1. **build-and-test**: cmake configure, build, ctest
2. **format-check**: `clang-format --dry-run --Werror` on all `.cpp`/`.hpp` files
