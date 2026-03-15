# W8 覆盖率工程 —— 知识点笔记

## 工具安装

```bash
# GTest 系统包（cmake configure 阶段必须已安装，不再使用 FetchContent）
sudo apt install libgtest-dev libgmock-dev

# 覆盖率工具
sudo apt install lcov

# 验证版本（项目使用 lcov 2.x，--ignore-errors mismatch,inconsistent 需要 2.x）
lcov --version
genhtml --version
```

### 内网 / 离线环境说明

系统包模式比 FetchContent **更适合内网**：FetchContent 在 configure 阶段从 GitHub
拉源码，断网直接失败；系统包安装一次后 cmake/ctest 完全本地运行，零网络依赖。

| 场景 | 处理方式 |
|------|---------|
| 内网有 apt 镜像（Nexus/Artifactory） | `sudo apt install libgtest-dev` 照常用 |
| 完全离线机器 | 在联网机上 `apt download libgtest-dev googletest libgmock-dev`，把 `.deb` 拷入后 `sudo dpkg -i *.deb` |
| 包已安装的机器 | cmake configure → build → ctest，全程零网络 |

---

## 构建命令速查

```bash
# 1. 开启覆盖率插桩重新配置（必须加 -DW8_COVERAGE=ON）
cmake -B build -S . -DCMAKE_CXX_COMPILER=g++-15 -G Ninja -DW8_COVERAGE=ON

# 2. 编译全部目标（含 W1-W7 测试目标）
cmake --build build -j$(nproc)

# 3. 生成覆盖率报告（自动运行测试 → 采集 → 过滤 → HTML）
cmake --build build --target w8_coverage

# 4. 在浏览器打开报告
xdg-open build/w8_coverage_report/index.html
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
cmake --build build -j$(nproc)

# 2. 清零历史数据并跑测试
lcov --zerocounters --directory build
ctest --test-dir build -R "W[1-7]_" -Q

# 3. 采集 → 过滤第三方 → 生成 HTML
lcov --capture --directory build --output-file build/w8_raw.info \
     --ignore-errors mismatch,inconsistent
lcov --remove build/w8_raw.info "/usr/*" "*/googletest/*" "*/gtest/*" "*/googlemock/*" \
     --output-file build/w8_filtered.info
genhtml build/w8_filtered.info \
     --output-directory build/w8_coverage_report \
     --title "Edge-AI-Genesis-2026 W1-W7 Coverage"

# 4. 查看文字摘要（无需浏览器）
lcov --list build/w8_filtered.info

# 5. 浏览器打开 HTML（可选）
xdg-open build/w8_coverage_report/index.html
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
| **合计** | | **98.6%** | **100%** |

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

### 问题 3：覆盖率数据包含系统头/gtest 代码

解决：`lcov --remove` 步骤过滤了 `/usr/*`、`*/googletest/*`、`*/gtest/*`、`*/googlemock/*`，只保留项目源文件数据。

---

### 问题 4：`-O0` 与 benchmark 冲突

W6 benchmark（`w6_mmap_benchmark`）需要 `-O2` 才能体现真实性能，故覆盖率插桩**只加在测试目标**上，不加在 benchmark 目标上。这也是 W6 `CMakeLists.txt` 覆盖率块只针对 `w6_mmap_loader_test` 的原因。

---

### 问题 5：覆盖率与 AddressSanitizer 不兼容

`--coverage` 与 `-fsanitize=address` 同时开启会导致链接错误。需要分别跑两个 build：一个开 ASAN，一个开覆盖率，不能合并。
