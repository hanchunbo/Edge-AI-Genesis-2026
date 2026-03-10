# W7 现代 CMake 工程化 —— 知识点笔记

## 核心知识点速查

### 1. Target-based CMake vs 旧式全局配置

| 旧式（反模式）| 现代 Target-based |
|---|---|
| `include_directories(path)` | `target_include_directories(target PRIVATE path)` |
| `add_compile_options(-Wall)` | `target_compile_options(target PRIVATE -Wall)` |
| `link_libraries(foo)` | `target_link_libraries(target PRIVATE foo)` |

旧式全局配置会污染整个构建树，Target-based 精确控制每个目标的属性。

---

### 2. INTERFACE / PRIVATE / PUBLIC 三种权限

```
              本库编译时可见？  消费者自动继承？
PRIVATE            ✅               ❌
INTERFACE          ❌               ✅
PUBLIC             ✅               ✅
```

**记忆口诀**：
- `PRIVATE`  = 只给自己
- `INTERFACE`= 只给别人（自己不用，纯头文件库的标准写法）
- `PUBLIC`   = 给自己和别人

**本项目演示**（见 `lib/CMakeLists.txt`）：
```cmake
# PUBLIC include：lib/ 源码和 app/tests 都能找到 tensor_utils.hpp
target_include_directories(w7_tensor_utils PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)

# PRIVATE 编译选项：只影响 tensor_utils.cpp，不传染给 app/tests
target_compile_options(w7_tensor_utils PRIVATE -O2)

# INTERFACE 宏：本库不定义，但 app/tests 会自动获得
target_compile_definitions(w7_tensor_utils INTERFACE W7_TENSOR_UTILS_AVAILABLE=1)
```

---

### 3. Generator Expressions（生成器表达式）

语法：`$<condition:value>`，在 CMake 生成构建文件时求值（configure 阶段不求值）。

**常用场景**：

```cmake
# 按编译器类型条件加编译选项
$<$<CXX_COMPILER_ID:GNU,Clang>:-Wall>

# 区分构建路径和安装路径（库的标准写法）
target_include_directories(mylib PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>   # install 后的相对路径
)

# 按构建类型条件
$<$<CONFIG:Debug>:-g3>
$<$<CONFIG:Release>:-O3>
```

**为什么需要 `$<BUILD_INTERFACE:...>`？**
如果直接写绝对路径（如 `/home/dev/project/include`），则导出的 cmake 配置文件会包含这个硬编码路径，在其他机器上 install 后无法使用。Generator Expression 让路径在构建期和安装期分别指向正确位置。

---

### 4. 多模块子目录管理

```
根目录 CMakeLists.txt
└── add_subdirectory(01_Linux_CPP_Foundations/w7_cmake_engineering)
    └── w7_cmake_engineering/CMakeLists.txt
        ├── add_subdirectory(lib)    → 生成 w7_tensor_utils STATIC 库
        ├── add_subdirectory(app)    → 生成 w7_app 可执行文件
        ├── add_subdirectory(tests)  → 生成 w7_tensor_utils_test
        └── add_subdirectory(modules)→ 生成 w7_hello_module（CMake 3.28+ 才启用）
```

**变量作用域**：子目录 CMakeLists.txt 继承父目录的变量，但子目录中的 `set()` 默认不反向传播。用 `set(VAR value PARENT_SCOPE)` 才能向上传播。

**GTest FetchContent 在根目录统一管理**：
- 只在根 CMakeLists.txt 调用 `FetchContent_MakeAvailable(googletest)` 一次
- 各子目录直接用 `GTest::gtest_main`，不重复下载
- 关键：`FetchContent_MakeAvailable` 必须在所有用到 GTest 的 `add_subdirectory` 之前执行

---

### 5. C++20 具名模块（Named Modules）

**传统头文件 vs 具名模块**：

| 对比项 | 传统 .hpp | C++20 具名模块 |
|--------|----------|----------------|
| 编译速度 | 每个 TU 重复解析 | 预编译 BMI，一次生成 |
| 宏泄漏 | 容易 | 模块内宏不泄漏 |
| 顺序依赖 | include 顺序敏感 | import 顺序无关 |
| 编译器支持 | 全面 | GCC 14+ / Clang 17+ 较完整 |

**CMake 3.28+ 配置**：
```cmake
add_library(mymodule STATIC)
target_sources(mymodule PUBLIC
  FILE_SET CXX_MODULES FILES
    my_module.cppm
)
```

**文件扩展名约定**（各编译器接受的模块接口单元扩展名）：
- `.cppm`：Clang/GCC 常用
- `.ixx`：MSVC 常用
- `.mpp`：部分社区使用

**本项目状态**：GCC 13 对具名模块支持有限（实验性），需要 CMake 3.28+。
模块演示在 `modules/CMakeLists.txt` 中加了版本检查，低版本会跳过并打印提示。

---

## 构建与测试命令

```bash
# 编译 W7 全部目标
cmake --build build --target w7_app w7_tensor_utils_test -j$(nproc)

# 跑 W7 所有测试
ctest --test-dir build -R "W7_" --output-on-failure

# 只跑功能测试（跳过 App 演示程序）
ctest --test-dir build -R W7_TensorUtilsTest --output-on-failure
```

## 验证 PUBLIC 传播的方法

```bash
# 查看 w7_app 的实际编译命令，确认包含 lib/ 路径
cmake --build build --target w7_app -v 2>&1 | grep "tensor_utils"
```

输出中应看到类似 `-I/.../w7_cmake_engineering/lib`，这是 CMake 自动注入的。
