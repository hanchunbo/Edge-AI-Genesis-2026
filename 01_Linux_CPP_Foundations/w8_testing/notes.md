# W8 覆盖率工程 —— 知识点笔记

## 工具安装

```bash
# 覆盖率工具（GTest 不需要单独安装，已集成为本地 zip）
sudo apt install lcov

# 验证版本（项目使用 lcov 2.x，--ignore-errors mismatch,inconsistent 需要 2.x）
lcov --version
genhtml --version
```

### GTest 集成方式：本地 zip（FetchContent）

`third_party/v1.15.2.zip` 已随仓库提交，CMake 使用 `file://` 协议直接读取，
configure / build / ctest **全程零网络依赖**，内网 / 离线机器开箱即用。

| 场景 | 处理方式 |
|------|---------|
| 内网 / 无网络 | 直接 `cmake -B build -S .`，FetchContent 读本地 zip |
| 完全离线（不含 zip 的机器） | 把 `third_party/v1.15.2.zip` 随代码一起拷入即可 |
| 跳过测试 | `cmake -DBUILD_TESTING=OFF`，不需要 GTest |

---

## 构建命令速查

```bash
# 1. 开启覆盖率插桩重新配置（必须加 -DW8_COVERAGE=ON）
cmake -B build -S . -DCMAKE_CXX_COMPILER=g++-15 -G Ninja -DW8_COVERAGE=ON

# 2. C++20 模块必须先单独编译（并行构建存在依赖竞态）
cmake --build build --target w7_hello_module -j1

# 3. 编译全部目标（含 W1-W7 测试目标）
cmake --build build -j$(nproc)

# 4. 生成覆盖率报告（自动运行测试 → 采集 → 过滤 → HTML）
cmake --build build --target w8_coverage

# 5. 启动临时 HTTP server，在本机浏览器访问 http://VPS_IP:8080
python3 -m http.server 8080 --directory build/w8_coverage_report
```

### 仅跑单周覆盖率（调试时用）

```bash
# 手动采集 W5 单独数据（示例）
lcov --capture --directory build/01_Linux_CPP_Foundations/w5_thread_pool \
     --output-file /tmp/w5.info
genhtml /tmp/w5.info --output-directory /tmp/w5_report
```

---

## 成功指标验证

### 指标一：ctest 全部通过

```bash
ctest --test-dir build -R "W[1-7]_" --output-on-failure
# 期望输出：100% tests passed, 0 tests failed out of 10
```

### 指标二：覆盖率报告 + 核心逻辑无盲区

**生成报告（分步，比 w8_coverage target 更稳定）：**

```bash
# 1. 开启插桩重新配置 + 编译
cmake -B build -S . -DCMAKE_CXX_COMPILER=g++-15 -G Ninja -DW8_COVERAGE=ON
cmake --build build --target w7_hello_module -j1
cmake --build build -j$(nproc)

# 2. 清零历史数据并跑测试
lcov --zerocounters --directory build
ctest --test-dir build -R "W[1-7]_" -Q

# 3. 采集 → 过滤第三方 → 生成 HTML
#    /usr/* 和 */_deps/* 需加 --ignore-errors unused（见下方 lcov 问题 4）
lcov --capture --directory build --output-file build/w8_raw.info \
     --ignore-errors mismatch,inconsistent
lcov --remove build/w8_raw.info "/usr/*" "*/_deps/*" \
     --output-file build/w8_filtered.info \
     --ignore-errors mismatch,inconsistent,unused
genhtml build/w8_filtered.info \
     --output-directory build/w8_coverage_report \
     --title "Edge-AI-Genesis-2026 W1-W7 Coverage"

# 4. 查看文字摘要（无需浏览器）
lcov --list build/w8_filtered.info

# 5. 启动临时 HTTP server 浏览 HTML（可选）
python3 -m http.server 8080 --directory build/w8_coverage_report
```

**判断标准：**
- 函数覆盖率 **100%**：所有函数都被调用，核心逻辑无死代码
- 行覆盖率 **≥ 90%**：未覆盖的行通常是异常/错误分支，属正常现象
- HTML 报告中红色高亮行 = 未覆盖路径，可针对性补测试用例

**当前基线（2026-03-16）：**

| 模块 | 核心头文件 | 行覆盖率 | 函数覆盖率 |
|------|-----------|---------|-----------|
| W1 | safe_tensor_buffer.hpp | 94.9% | 100% |
| W2 | custom_image.hpp | 100% | 100% |
| W3 | model_scanner.hpp | 100% | 100% |
| W4 | producer_consumer.hpp | 97.1% | 100% |
| W5 | thread_pool.hpp | 94.3% | 100% |
| W6 | mmap_loader.hpp | 95.0% | 100% |
| W7 | tensor_utils（lib） | 100% | 100% |
| **合计** | | **98.7%** | **100%** |

---

## 核心概念

| 概念 | 说明 |
|------|------|
| `--coverage` | GCC 编译选项，等价于 `-fprofile-arcs -ftest-coverage`，生成 `.gcno` 插桩文件 |
| `.gcno` | 编译期生成，记录代码分支图 |
| `.gcda` | 运行期生成，记录实际执行次数；每次运行**累加** |
| `lcov --zerocounters` | 清除上次 `.gcda` 数据，避免和历史运行叠加 |
| `lcov --capture` | 读取 `.gcda` + `.gcno`，生成 `.info` 汇总文件 |
| `genhtml` | 把 `.info` 转成 HTML 报告 |

---

## lcov 常见问题

### 问题 1：`geninfo: ERROR: .gcno not found`

原因：测试可执行文件没有 `--coverage` 插桩，或 build 目录被清理后 `.gcno` 丢失。

解决：确认 `W8_COVERAGE=ON`，重新 `cmake --build build`，再跑 `w8_coverage`。

---

### 问题 2：`mismatch` / `inconsistent` 警告导致 lcov 中断

现象：`lcov: ERROR: checksum mismatch` 或 `mismatched end line`，lcov 2.x + GCC 15
对内联函数行号有误判，导致大量 inconsistent warning 最终变 error。

解决：采集时加 `--ignore-errors mismatch,inconsistent`（已写入分步命令）。
若 `w8_coverage` CMake target 退出码非零，改用上方"分步"命令替代。

---

### 问题 3：覆盖率数据包含 _deps/ 下的 gtest 代码

FetchContent 把 GTest 源码解压到 `build/_deps/googletest-src/`，这些路径会出现
在覆盖率数据中。`lcov --remove` 的 `*/_deps/*` 模式负责过滤它们。

---

### 问题 4：`lcov --remove` 报 `unused` error（exit code 25）

原因：cmake custom target 不使用 shell 引号，`/usr/*` 会被 shell 展开为
`/usr/bin`、`/usr/lib` 等具体路径。这些路径在 tracefile 中无匹配，
lcov 2.x 将"未使用的排除模式"视为 error（exit 25）。

解决：`--remove` 步骤加 `--ignore-errors mismatch,inconsistent,unused`（已写入 CMakeLists.txt）。

---

### 问题 5：`-O0` 与 benchmark 冲突

W6 benchmark（`w6_mmap_benchmark`）需要 `-O2` 才能体现真实性能，故覆盖率插桩**只加在测试目标**上，不加在 benchmark 目标上。这也是 W6 `CMakeLists.txt` 覆盖率块只针对 `w6_mmap_loader_test` 的原因。

---

### 问题 6：覆盖率与 AddressSanitizer 不兼容

`--coverage` 与 `-fsanitize=address` 同时开启会导致链接错误。需要分别跑两个 build：一个开 ASAN，一个开覆盖率，不能合并。

---

### 问题 7：C++20 模块并行构建竞态

W7 的 `w7_module_consumer.cpp` 依赖 `w7_hello_module`（`.gcm` 文件），
`-j$(nproc)` 并行构建时可能在模块编译完成前就尝试编译 consumer，导致 `w7.hello: No such file` 错误。

解决：先单独构建模块再并行编译全部目标：
```bash
cmake --build build --target w7_hello_module -j1
cmake --build build -j$(nproc)
```
