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

**INTERFACE 宏的真正语义**：
`W7_TENSOR_UTILS_AVAILABLE` 不是"检测库是否编译成功"的手段——它不具备检测能力。
它的语义是：**CMake 通知消费者"我已经把这个库链接给你了，你可以放心调用它"**。
能走到"编译 app.cpp"这一步，库就已经存在了（否则 CMake 早在链接阶段报错）。
典型用途是**可选依赖**：CMakeLists 用 `if` 条件决定是否链接库，app.cpp 用 `#ifdef` 决定走哪条代码路径。

**W7 的核心本质**：
`app/main.cpp` 和 `tests/tensor_utils_test.cpp` 能直接写 `#include "tensor_utils.hpp"`，
而不需要在各自的 CMakeLists.txt 里配置任何路径——这就是 W7 要达到的目标。
PUBLIC 可见性让库自己声明头文件位置，消费者只写 `target_link_libraries`，路径自动传递。

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

**本项目状态**：需要 GCC 15+ 和 CMake 3.28+（`import std;` 需要 GCC 15 稳定支持），
且必须用 Ninja 生成器（`-G Ninja`），Unix Makefiles 不支持 `FILE_SET CXX_MODULES`。
模块演示在 `modules/CMakeLists.txt` 中加了版本检查，低版本会跳过并打印提示。

**本项目采用的生产级方案（GCC 15 + C++23）**：

```cpp
// hello_module.cppm —— 模块接口
export module w7.hello;
export import std;        // 把整个标准库重新导出给所有消费者
export namespace w7 { ... }

// module_consumer.cpp —— 消费端
import w7.hello;          // 唯一的 import，无任何 #include
// std::cout、std::string 等全部通过 export import std; 链传入
```

**为什么用 `export import std;` 而非 `#include`**：
- `import std;` 替代 global module fragment 的 `#include`，规避 GCC 符号 `@模块名` 冲突
- `export import std;` 把 std 重新导出，consumer 零 `#include`，彻底避免混用冲突
- 这是 C++23 模块在生产代码中的推荐写法

**GCC 15 + CMake 4.x consumer 端的已知限制（2026 现状）**：
CMake 不为普通 consumer `.cpp` 文件自动添加 `-fmodules`/`-fmodule-mapper` 标志，需手动指定：

```cmake
target_compile_options(w7_module_consumer PRIVATE
  -fmodules
  -fmodule-mapper=.../hello_module.cppm.o.modmap)
```

**编译顺序问题已由 CMake 4.x + Ninja dyndep 自动解决**：
早期（CMake < 4.x）需要用 stamp 文件 + `OBJECT_DEPENDS` 手动桥接
"模块库链接完成 → `.gcm` 已就绪"这一事件，防止 consumer 提前编译。
CMake 4.x 的 `FILE_SET CXX_MODULES` 会为模块目标生成 dyndep 文件，
Ninja 据此自动排序，stamp workaround 已完全不需要（验证于 CMake 4.2.3）。

---

### 6. INSTALL 规则（生产级安装配置）

> **注意**：install 规则不在 W7 核心任务范围内（见 docs/archive/Q1.md W7 节），是为演示
> `$<INSTALL_INTERFACE:...>` 生成器表达式的完整用法而补充的。理解"是干什么的"即可，
> 不需要深究 find_package / 导出集的细节。

**为什么需要 install()**：
构建树（build tree）中的 `target_include_directories` 只在当前构建有效。
外部项目通过 `find_package()` 使用本库时，必须有 install() 生成的 export 配置。

```cmake
# 1. 安装库文件 + 声明 export 集合名
install(
  TARGETS w7_tensor_utils
  EXPORT W7TensorUtilsTargets
  ARCHIVE DESTINATION lib          # .a 文件 → ${prefix}/lib/
  INCLUDES DESTINATION include)   # 头文件根路径声明（配合 INSTALL_INTERFACE）

# 2. 安装头文件（用子目录避免命名冲突）
install(FILES tensor_utils.hpp DESTINATION include/w7)

# 3. 生成 targets cmake 文件（外部项目 find_package 时读取）
install(
  EXPORT W7TensorUtilsTargets
  FILE W7TensorUtilsTargets.cmake
  NAMESPACE w7::              # 消费者写 w7::w7_tensor_utils
  DESTINATION lib/cmake/W7TensorUtils)
```

**验证安装**：
```bash
cmake --install build --prefix /tmp/w7_install_test
ls /tmp/w7_install_test/lib/                        # libw7_tensor_utils.a
ls /tmp/w7_install_test/include/w7/                 # tensor_utils.hpp
ls /tmp/w7_install_test/lib/cmake/W7TensorUtils/    # W7TensorUtilsTargets.cmake
```

**Checklist**：
- [x] INSTALL 规则：`install(TARGETS/FILES/EXPORT)`，`$<INSTALL_INTERFACE:include>` 生成器表达式
- [x] Consumer 演示：`modules/module_consumer.cpp`，`import w7.hello;` 完整调用链

---

## 构建与测试命令

```bash
# 首次配置（必须用 Ninja，C++20 具名模块不支持 Unix Makefiles）
# 编译器必须 GCC 15+（import std; 需要 GCC 15 稳定支持）
cmake -B build -S . -DCMAKE_CXX_COMPILER=g++-15 -G Ninja

# 编译 W7 全部目标
cmake --build build --target w7_app w7_tensor_utils_test w7_module_consumer -j$(nproc)

# 跑 W7 所有测试
ctest --test-dir build -R "W7_" --output-on-failure

# 只跑功能测试（跳过 App 演示程序）
ctest --test-dir build -R W7_TensorUtilsTest --output-on-failure

# 运行模块演示
./build/01_Linux_CPP_Foundations/w7_cmake_engineering/modules/w7_module_consumer
```

## 验证 PUBLIC 传播的方法

```bash
# 查看 w7_app 的实际编译命令，确认包含 lib/ 路径
cmake --build build --target w7_app -v 2>&1 | grep "tensor_utils"
```

输出中应看到类似 `-I/.../w7_cmake_engineering/lib`，这是 CMake 自动注入的。
