# quant 收尾修正与性能证据补全计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking. 本计划默认串行执行，不自动派发子代理。

**Goal:** 修正缺产物时的测试控制流，统一可复现的量化入口，补充可归因的 GPU 性能证据，使 quant 的完成声明与实际范围一致。

**Architecture:** 保留现有 W14 engine、W16 detector 与 quant harness。首先小幅修改测试和配置，再给 benchmark 增加默认关闭的 NVTX 标记；完整 IOBinding、内存池和真正的 Entropy 对比继续留在范围外。报告区分历史结果、新实测和未验证推断。

**Tech Stack:** GCC 15、Ninja、CMake、Google Test 1.15.2、Python 标准库、ORT C++ 1.26.0、Nsight Systems、NVTX3。

**状态：** 用户已授权执行。2026-09-10 已完成测试/复现修正、文档同步、NVTX 可选构建与 CI 格式覆盖；后续切换执行权限并安装 Nsight 后已完成真实 GPU 采集与归因；新增一次退出堆损坏问题另列技术债。

---

## 1. 重新评估：哪些值得改

| 上轮发现 | 重新判断 | 本轮建议 |
|---|---|---|
| 缺模型时打印 skip 后仍加载文件 | 确定的测试控制流缺陷，会使裸克隆或新 build 目录误报失败 | 必修；覆盖 quant 和 quant 增补的 W16 测试 |
| build-gpu 没有 INT8 产物 | 本地产物缺失，不是 CUDA 推理实现故障；这些一致性测试实际指定 CPU EP | 配置为复用已有 ONNX，无需重新做一次相同校准 |
| 默认校准命令与 32 图报告不一致 | 确实影响复现；项目已有 Entropy 内存超限记录 | 必修；统一数量限制、数据目录、内存受限运行示例 |
| perf / Nsight 没跑 | 缺性能归因证据，不影响已通过的检测正确性；对性能优化项目有明显价值 | 补一次 Nsight Systems；不要求 perf、Systems、Compute 三套全做 |
| reserve 缺分配次数实测 | 功能等价已测；现在每次 decode 都新建 vector，reserve 不是跨帧复用 | 本轮先改描述，不引入自定义 allocator 或内存池；分配计数保留为未验收项 |
| CI 不覆盖 quant | 工程覆盖不足；上轮没有验证远端 CI 的具体失败状态 | 先扩格式覆盖；带 ORT 的构建/集成作独立后续任务，不阻塞本轮本地修正 |
| 完整 IOBinding 没实现 | 文档已有停止投入决定，不能再当作必须补的遗留功能 | 保留决定；删除“已证明未实现方案无收益”的过强结论 |
| MinMax / Entropy 两产物相同 | 已知且有勘误，不是这轮新发现的量化故障 | 保留既有工具与勘误；真正策略对比继续交给 TRT M2 |

**方案取舍：** 只改文字不能解决测试缺陷与复现入口；逐项补齐原始设想会引入没有瓶颈依据的优化。推荐本计划的“测试与复现修正 + 一次有标记的 profiling + 范围同步”。这会调整原验收范围，不能表述为原 Roadmap 所有要求均已实现。

**优先顺序：** 任务 1 → 任务 2 → 任务 3；任务 4 在工具可用后完成；任务 5 的格式覆盖可独立落地。修复复现入口前不启动高内存校准。

## 2. 已有证据与改动边界

- 上轮本地重新编译后：CPU 构建的 7 个 W16/Quant CTest 目标、32 个 GTest 用例全部通过，无跳过。
- 上轮 `build-gpu` 的 `Quant_Int8ConsistencyTest`：两个用例均在 skip 后抛出模型不存在异常。根因是 `GTEST_SKIP()` 只从辅助函数返回，调用者继续运行。
- 两个现有 CPU INT8 产物均为 6,473,085 字节，SHA256 均为 `3d3a99ae89d76f0b8ef8bb625718fc47f94b6686faaffaf802fe822f50993abb`。
- 本轮复核时 `perf` / `nsys` / `ncu` 不在 PATH；常见安装路径搜索也未找到 nsys。不能把这一点扩大成全机安装状态结论。
- 参考：`docs/Roadmap.md:88`、`docs/benchmarks/quant_yolo_hardening.md:44`、`docs/benchmarks/quant_int8_report.md:96`、`docs/devlog.md:47`。
- 工作区已有 `eval_harness.cpp` 和 `eval_harness.hpp` 的 8 行注释修改。保留它们，不混入本计划的提交。
- W16 仅修 quant 加性扩展测试的控制流，不改检测算法或原始对拍基线。
- 不重做 COCO 全量验证、不改量化数学策略、不补剪枝/蒸馏实现、不启动 TRT M2。

## 3. 文件职责

| 文件 | 计划变化 |
|---|---|
| `02_Inference_Analysis/quantization/int8_consistency_test.cpp` | 传播辅助函数的 skip 状态 |
| `02_Inference_Analysis/quantization/eval_harness_test.cpp` | 同上 |
| `02_Inference_Analysis/quantization/batch_consistency_test.cpp` | 同上 |
| `02_Inference_Analysis/w16_yolo_detector/yolo_detector_test.cpp` | 修正 quant 增补的三个用例 |
| `02_Inference_Analysis/quantization/CMakeLists.txt` | 校准/产物目录配置及默认关闭的 NVTX 选项 |
| `02_Inference_Analysis/quantization/tools/quantize_yolov8_static.py` | 默认校准 32 图，拒绝负数限制 |
| `02_Inference_Analysis/quantization/tools/test_quantize_args.py`（新增） | 不导入 ORT 的参数回归测试 |
| `02_Inference_Analysis/quantization/quant_benchmark.cpp` | 增加采样量及可选测量区间标记 |
| `README.md`、`docs/Roadmap.md`、`AGENTS.md`、`CLAUDE.md`、quant `notes.md`、两份 quant benchmark 报告 | 统一命令、范围与证据描述 |
| `docs/benchmarks/quant_profiling_report.md`（测量后新增） | 环境、原始数据位置、归因与后续方向 |
| `.github/workflows/ci.yml` | 本轮只扩展格式检查覆盖 |

## 任务 1：修正缺产物时的测试控制流

- [x] **先保留现有失败证据。** 执行以下命令；若 build-gpu 仍缺 INT8 产物，应复现 2 个 GTest 失败。若产物已被其他工作恢复，使用下面的临时源码快照复现，不移动或删除工作区模型。

```bash
cmake --build build-gpu --target quant_int8_consistency_test -j2
ctest --test-dir build-gpu -R '^Quant_Int8ConsistencyTest$' --output-on-failure
```

- [x] **做最小控制流修正。** 保留现有缺文件辅助函数。quant 的 eval 两个用例、batch 一个用例、W16 的三个 quant 增补用例，在 `SkipIfAssetsMissing();` 后紧接以下代码：

```cpp
  if (::testing::Test::IsSkipped()) {
    return;
  }
```

INT8 的 `ExpectInt8MatchesFp32()` 中，三次 `SkipIfMissing(...)` 之后、两次 `DetectCpu(...)` 之前加入相同代码。这会把 skip 状态传播到实际加载模型的调用者，不改变模型齐全时的断言。原 W16 `MatchesUltralyticsReference` 已直接在 TEST 内 skip，保留原样。

- [x] **验证两种缺产物场景。** 在仓库根目录执行以下脚本。它复制当前受版本控制的工作区文件到临时目录；既保留当前代码修改，又不带入被忽略的模型，不触碰原模型。

```python
from pathlib import Path
import shutil
import subprocess
import tempfile
import xml.etree.ElementTree as ET

repo = Path.cwd()
ort = repo / 'third_party/onnxruntime/onnxruntime-linux-x64-1.26.0'
targets = {
    '02_Inference_Analysis/w16_yolo_detector/w16_yolo_detector_test': 4,
    '02_Inference_Analysis/quantization/quant_eval_harness_test': 2,
    '02_Inference_Analysis/quantization/quant_batch_consistency_test': 1,
    '02_Inference_Analysis/quantization/quant_int8_consistency_test': 2,
}
with tempfile.TemporaryDirectory(prefix='quant-assets-check-') as tmp:
    src = Path(tmp) / 'src'
    build = Path(tmp) / 'build'
    for name in subprocess.check_output(['git', 'ls-files', '-z']).split(b'\0'):
        if not name:
            continue
        rel = Path(name.decode())
        if (repo / rel).is_file():
            (src / rel).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(repo / rel, src / rel)
    subprocess.run([
        'cmake', '-S', str(src), '-B', str(build), '-G', 'Ninja',
        '-DCMAKE_CXX_COMPILER=g++-15', '-DCMAKE_BUILD_TYPE=Release',
        f'-DONNXRUNTIME_ROOT={ort}',
    ], check=True)
    subprocess.run([
        'cmake', '--build', str(build), '--target',
        *[Path(p).name for p in targets], '-j2',
    ], check=True)
    for binary, expected in targets.items():
        xml = build / (Path(binary).name + '.xml')
        subprocess.run([str(build / binary), f'--gtest_output=xml:{xml}'], check=True)
        cases = ET.parse(xml).getroot().findall('.//testcase')
        assert len(cases) == expected
        assert all(c.find('skipped') is not None for c in cases)
    assets = Path('02_Inference_Analysis/w16_yolo_detector/models')
    for name in ['yolov8n.onnx', 'test_image.jpg']:
        (src / assets).mkdir(parents=True, exist_ok=True)
        shutil.copy2(repo / assets / name, src / assets / name)
    xml = build / 'int8-only-missing.xml'
    binary = build / '02_Inference_Analysis/quantization/quant_int8_consistency_test'
    subprocess.run([str(binary), f'--gtest_output=xml:{xml}'], check=True)
    cases = ET.parse(xml).getroot().findall('.//testcase')
    assert len(cases) == 2
    assert all(c.find('skipped') is not None for c in cases)
print('缺全部模型/图片及仅缺 INT8 两种场景均正确跳过')
```

- [x] **验证模型齐全的路径。** 重新构建上述四个测试目标及 `quant_rolling_stats_test`、`w16_nms_test`、`w16_decode_test`，执行 `ctest --test-dir build -R '^(W16_|Quant_)' --output-on-failure`。检查 GTest 日志，预期原有 32 用例通过，不能只看 CTest 退出码而忽略 skip。

## 任务 2：统一校准入口与模型路径

- [x] **先添加参数回归测试。** 新增 `tools/test_quantize_args.py`：

```python
#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# 文件功能：验证量化校准数量的默认值与参数边界，不加载推理依赖。
import contextlib
import io
import sys
import unittest
from unittest.mock import patch

from quantize_yolov8_static import parse_args


class QuantArgsTest(unittest.TestCase):
    def test_default_limit_matches_report(self):
        with patch.object(sys, 'argv', ['quant', '--model', 'unused.onnx']):
            self.assertEqual(parse_args().calib_limit, 32)

    def test_explicit_limit_is_preserved(self):
        with patch.object(sys, 'argv', ['quant', '--model', 'unused.onnx',
                                        '--calib-limit', '8']):
            self.assertEqual(parse_args().calib_limit, 8)

    def test_explicit_zero_allows_unlimited(self):
        with patch.object(sys, 'argv', ['quant', '--model', 'unused.onnx',
                                        '--calib-limit', '0']):
            self.assertEqual(parse_args().calib_limit, 0)

    def test_negative_limit_is_rejected(self):
        with patch.object(sys, 'argv', ['quant', '--model', 'unused.onnx',
                                        '--calib-limit', '-1']):
            with contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as raised:
                    parse_args()
            self.assertEqual(raised.exception.code, 2)


if __name__ == '__main__':
    unittest.main()
```

Run: `python3 -m unittest discover -s 02_Inference_Analysis/quantization/tools -p test_quantize_args.py -v`。修改前默认值和负数检查两项应失败；不得触发真正校准。

- [x] **修改 Python 参数。** `--calib-limit` 的 `default=0` 改为 `default=32`；help 改为“校准图数量上限，默认 32；显式 0 表示不限。Entropy 校准仍需按机器内存限制运行。”用以下代码替换 `return parser.parse_args()`：

```python
    args = parser.parse_args()
    if args.calib_limit < 0:
        parser.error('--calib-limit 必须 >= 0')
    return args
```

- [x] **修改 CMake 配置。** 用以下配置替代原来写死的模型输出目录与校准目录定义；校准目录存在时用目录、否则回退测试图的分支继续保留。

```cmake
set(QUANT_MODELS_DIR "${CMAKE_CURRENT_BINARY_DIR}/models"
    CACHE PATH "quant INT8 产物目录，可在 CPU/GPU 构建间复用")
set(_QUANT_MODELS_DIR "${QUANT_MODELS_DIR}")
set(QUANT_CALIB_DIR "$ENV{HOME}/code/datasets/coco128/images/train2017"
    CACHE PATH "quant 校准集目录，缺失时回退 W16 测试图")
set(_QUANT_CALIB_DIR "${QUANT_CALIB_DIR}")
set(QUANT_CALIB_LIMIT "32" CACHE STRING "quant 校准图数量，0 表示不限")
if(NOT "${QUANT_CALIB_LIMIT}" MATCHES "^[0-9]+$")
  message(FATAL_ERROR "QUANT_CALIB_LIMIT 必须为非负整数")
endif()
```

在 `quant_yolov8_static` 的 Python 命令参数中增加 `--calib-limit "${QUANT_CALIB_LIMIT}"`。如果 `BUILD_TESTING` 与 `Python3_Interpreter_FOUND` 同时成立，注册参数测试：

```cmake
add_test(NAME Quant_ArgsTest
  COMMAND "${Python3_EXECUTABLE}" -m unittest discover
          -s "${CMAKE_CURRENT_SOURCE_DIR}/tools"
          -p test_quantize_args.py -v)
```

- [x] **验证配置与复用。** 在根目录执行：

```bash
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=g++-15
ninja -C build -n -v quant_yolov8_static
cmake -S . -B build-gpu -G Ninja -DCMAKE_CXX_COMPILER=g++-15 \
  -DQUANT_MODELS_DIR="$PWD/build/02_Inference_Analysis/quantization/models"
cmake --build build-gpu --target quant_int8_consistency_test -j2
ctest --test-dir build-gpu -R '^Quant_Int8ConsistencyTest$' --output-on-failure
```

预期 dry-run 明确包含 `--calib-limit 32`；GPU 构建目录下的两个一致性用例读取现有产物后通过，不能以 skip 代替通过。该测试使用 CPU EP，GPU 本身的验证留给任务 4。

- [x] **更新 README 正式重生成命令。** 以下是本机已有内存约束下的运行方式；32 图限制不是内存上限，不能用它代替内存约束。不在跨平台 CMake 中强制依赖 systemd。

```bash
systemd-run --user --scope -p MemoryMax=5G -p MemorySwapMax=4G \
  cmake --build build --target quant_yolov8_static
```

README 同时写明：默认优先 coco128、缺目录时只做单图工具链验证；单图产物不用于复述历史 coco128 结论。保留手动 Python 命令并显式带 `--calib-limit 32`。本轮可复用已验证产物，不为了补 GPU 目录重复跑高内存校准。

## 任务 3：修正文档范围与过强结论

- [x] **同步状态，保留历史结果。** `README.md`、`AGENTS.md`、`CLAUDE.md`、Roadmap 使用同一事实描述：

> quant 的 INT8 工具链、检测硬化及 coco128 精度评估已完成；测试/复现修正按本计划验收，GPU profiling 证据补充中。完整 IOBinding 与 allocator 分配计数未验收；真正的 Entropy 对比与 GPU INT8 加速继续由 TRT 承接。

任务 4 有真实证据后才能把“profiling 证据补充中”改成“profiling 已补充”。不把本次补测写成历史上已在量化前完成分析。
只替换 quant 的状态片段，保留已有 TRT M1 完成、M1.5/M2 未开始的进度；不改历史周志。

- [x] **逐项替换容易误导的描述。**

| 位置 | 替换为的事实描述 |
|---|---|
| Roadmap 的 reserve“消除逐帧 realloc” | “预留候选容量，减少容量不足时的扩容；当前每帧新建 vector，未实现跨帧复用；未测分配次数” |
| Roadmap 完整双绑要求 | 明确标为原设想、当前不实施，链接后续 TRT 性能分析；不再列为已完成范围 |
| `quant_int8_report.md` 与 quant notes 的双绑结论 | “现有路径未展示稳定收益，因此暂不投入完整方案；完整方案尚未实现，其收益未知” |
| GPU INT8 变慢的“根因” | “日志观察到额外 Memcpy 节点；拷贝、计算及同步各自贡献仍需 profiling 验证” |
| 量化脚本中“头部算力占比小，对延迟影响可忽略” | “检测头保 FP32 用于恢复检测正确性；它对延迟的贡献尚未单独测量” |
| 报告中“牺牲了大部分量化收益” | “保留 FP32 限制了量化覆盖范围；具体延迟代价未单独测量” |
| MinMax/Entropy 表格 | 保留历史数据及字节相同的勘误，说明不是两个独立校准结果的性能比较 |

- [x] **处理旧报告。** 在 `quant_yolo_hardening.md` 保留原 CPU fallback 表并注明历史环境；链接后来已完成的 GPU 量化报告。不能把“GPU 硬化 before/after 尚待补”写成“GPU 从未测过”。
- [x] **保留 reserve 的退出条件。** 本轮不新增 allocator 基础设施。只有后续剖析显示 decode/分配占时值得投入，再开单项对照，比较 reserve=0/512、真实候选数、分配次数与耗时；不得用合并了四项配置的 hardened 表归因于 reserve。

## 任务 4：补一次可归因的 GPU profiling

**必要性：** 此项是性能证据补全，不预设一定能优化多少，也不预设 Memcpy 一定是主要瓶颈。选择 Nsight Systems 看计算、拷贝和同步；先不引入 Nsight Compute 或 perf 的额外工作。

**外部前置条件：** 执行时检查 `nsys --version`、`nsys profile --help`、`nvidia-smi`，确认 WSL 内可采集 CUDA trace。若工具或驱动条件不可用，准确记录该项未完成，继续其他任务，不用空报告替代。安装方式及 NVTX 标记依据 [NVIDIA Nsight Systems 官方指南](https://docs.nvidia.com/nsight-systems/UserGuide/index.html#installing-the-cli-on-your-target)；实际版本及安装命令需写回 README。

- [x] **增加默认关闭的 NVTX 编译选项。** 在 quant 的 `quant_benchmark` 目标创建后增加：

```cmake
option(QUANT_ENABLE_NVTX "给 quant benchmark 增加 Nsight 分段标记" OFF)
if(QUANT_ENABLE_NVTX)
  find_path(QUANT_NVTX_INCLUDE_DIR nvtx3/nvtx3.hpp
            HINTS /usr/local/cuda/include)
  if(NOT QUANT_NVTX_INCLUDE_DIR)
    message(FATAL_ERROR
      "需要 NVTX3 头文件；QUANT_NVTX_INCLUDE_DIR 应指向包含 nvtx3 的 include 目录")
  endif()
  target_include_directories(quant_benchmark PRIVATE "${QUANT_NVTX_INCLUDE_DIR}")
  target_compile_definitions(quant_benchmark PRIVATE QUANT_ENABLE_NVTX=1)
  target_link_libraries(quant_benchmark PRIVATE ${CMAKE_DL_LIBS})
endif()
```

- [x] **标记测量循环，不把初始化算进推理。** `quant_benchmark.cpp` 按条件包含头文件：

```cpp
#ifdef QUANT_ENABLE_NVTX
#include <nvtx3/nvtx3.hpp>
#endif
```

把 `BenchInfer` 最后一个参数扩展为 `bool use_iobinding, [[maybe_unused]] const char* trace_name`。保留其预热循环，在计时循环外增加局部作用域：

```cpp
  std::vector<double> latencies;
  latencies.reserve(kIters);
  {
#ifdef QUANT_ENABLE_NVTX
    const nvtx3::scoped_range trace{trace_name};
#endif
    for (int i = 0; i < kIters; ++i) {
      const auto start = std::chrono::steady_clock::now();
      RunOnce(engine, input, shape, use_iobinding);
      const auto end = std::chrono::steady_clock::now();
      latencies.push_back(
          std::chrono::duration<double, std::milli>(end - start).count());
    }
  }
```

唯一调用点改为：

```cpp
        const std::string trace_name =
            model.name + "_" + EpName(engine.ActiveEp()) + "_" +
            (use_iobinding ? "IOBinding" : "Run");
        const InferStat stat =
            BenchInfer(engine, input, shape, use_iobinding, trace_name.c_str());
```

将 `kWarmup` 改为 20、`kIters` 改为 200；harness 配置的 `.stats_window = 128` 改为 `.stats_window = kIters`，使报告统计包含该轮全部测量帧。这只改变 benchmark 采样，不改变 detector 默认配置。

- [x] **分别构建正常测量版和 profiling 版。** 保持现有 build-gpu 的 NVTX 默认关闭；另建 `build-quant-profile`，使用 GCC 15、Ninja、Release、现有 GPU ORT 包并打开 NVTX。头文件可位于 Nsight 包的 target 目录；用下面的查找与配置脚本，缺依赖时立即报错，不静默产生无标记构建。

```python
from pathlib import Path
import subprocess

repo = Path.cwd()
headers = []
for base in [repo / 'build/quant-tools', Path('/opt/nvidia'), Path('/usr/local'), Path.home() / '.local']:
    if base.exists():
        headers.extend(base.rglob('nvtx3/nvtx3.hpp'))
if not headers:
    raise SystemExit('未找到 NVTX3；先安装 Nsight CLI/NVTX 头文件并记录版本')
include = sorted(headers)[-1].parent.parent
subprocess.run([
    'cmake', '-S', '.', '-B', 'build-quant-profile', '-G', 'Ninja',
    '-DCMAKE_CXX_COMPILER=g++-15', '-DCMAKE_BUILD_TYPE=Release',
    '-DQUANT_ENABLE_NVTX=ON', f'-DQUANT_NVTX_INCLUDE_DIR={include}',
    f'-DONNXRUNTIME_ROOT={repo}/third_party/onnxruntime/onnxruntime-linux-x64-gpu-1.26.0',
], check=True)
subprocess.run(['cmake', '--build', 'build-quant-profile', '--target',
                'quant_benchmark', '-j2'], check=True)
```

- [x] **先运行无 profiler 的基准，再采 trace。** 正常版重编后执行 5 轮，保存每轮原始输出；只使用 FP32 和 MinMax 两个不同模型。

```bash
mkdir -p build/quant-closeout-evidence
cmake --build build-gpu --target quant_benchmark -j2
for quant_round in 1 2 3 4 5; do
  ./build-gpu/02_Inference_Analysis/quantization/quant_benchmark \
    02_Inference_Analysis/w16_yolo_detector/models/test_image.jpg \
    02_Inference_Analysis/w16_yolo_detector/models/yolov8n.onnx \
    build/02_Inference_Analysis/quantization/models/yolov8n.int8.minmax.onnx \
    > "build/quant-closeout-evidence/benchmark-${quant_round}.md" 2>&1
done
nsys profile --trace=cuda,nvtx,osrt --sample=none --cpuctxsw=none \
  --output=build/quant-closeout-evidence/quant \
  ./build-quant-profile/02_Inference_Analysis/quantization/quant_benchmark \
  02_Inference_Analysis/w16_yolo_detector/models/test_image.jpg \
  02_Inference_Analysis/w16_yolo_detector/models/yolov8n.onnx \
  build/02_Inference_Analysis/quantization/models/yolov8n.int8.minmax.onnx
nsys stats --report cuda_gpu_trace --format csv \
  build/quant-closeout-evidence/quant.nsys-rep \
  > build/quant-closeout-evidence/cuda-trace.csv
```

- [x] **验收与报告。** 新报告必须满足：
  1. 记录提交、GPU/驱动/ORT/Nsight 版本、模型哈希、线程设置、预热/样本数、实际 EP；CUDA 回退时不能写 GPU 对比结论。
  2. Nsight 时间线中可分辨 `fp32_CUDA_Run`、`yolov8n.int8.minmax_CUDA_Run`，以及各自 IOBinding 区间；区间只包含预热后的 200 次推理。
  3. 按标记区间分别统计计算、H2D/D2H/D2D 拷贝次数/字节/持续时间及同步等待。不能把整个进程的汇总数字分配给某一个模型。
  4. 区分 host API 时间、设备操作时间和端到端墙钟时间；并行事件不能直接相加当作墙钟占比。缺少算子/EP 分配证据时，不仅凭 Memcpy 个数断言 CPU fallback 或特定算子根因。
  5. 性能表使用无 profiler 的多轮结果；trace 用于归因。补充 GPU default/hardened 的 before/after 表，但仍不把整体收益归给单个 reserve。
  6. 结论允许是“现有证据不足以量化某因素贡献”；提出与证据对应的一项后续方向，不强行为量化正收益找理由。
  7. 把原始 trace、日志与哈希保存在 `build/quant-closeout-evidence/`，随报告交付附件；报告记明路径与复现命令，避免只保留会被清理的 build 目录而没有交付副本。

## 任务 5：CI 的小范围收尾与后续边界

- [x] **本轮扩展格式覆盖。** 只替换现有 `format-check` 的文件收集命令，继续使用 clang-format-21：

```bash
git ls-files -z -- \
  '01_Linux_CPP_Foundations/*.cpp' '01_Linux_CPP_Foundations/*.hpp' \
  '02_Inference_Analysis/*.cpp' '02_Inference_Analysis/*.hpp' \
  | xargs -0 --no-run-if-empty clang-format-21 --dry-run --Werror
```

验证待检查清单包含 quant 和 W16；如果发现其他模块历史格式问题，不顺带全仓重排，记录为独立项。

- [x] **将完整 CI 集成记为独立工程任务。** 范围明确为：固定兼容的 GCC 15/Ninja/CMake 环境，获取 CPU ORT，显式构建 quant 测试目标并检查注册清单；区分无模型单元/skip 检查与有产物的一致性检查。没有模型的 CI 不能宣称通过 INT8 精度回归。首次 PR 不加入每次都重新跑 Entropy 校准或要求 GPU runner。

本计划不展开该独立任务的环境镜像/依赖下载改造，不将其完成设为 TRT 继续开发的前置条件。当前工作流仍使用 g++-13，且没有准备 ORT；仅扩格式覆盖不会修好这两个问题，状态文档必须保留这一事实。

## 最终检查与交付

- [x] 按任务 1 的缺产物矩阵验证 skip 行为，按任务 2 验证有模型的真实断言运行。
- [x] 运行 `ctest --test-dir build -R '^(W16_|Quant_)' --output-on-failure`；新增参数测试后应为 8 个 CTest 目标，包含原 32 个 GTest 用例及 4 个 Python 参数用例。确认不是新增全 skip 后的假通过。
- [x] clang-format-21 检查修改的 C++ 文件，`git diff --check`；文档与注释同步，不改已有用户注释工作。
- [x] 不在 profiling 尚缺工具/证据时把该项打勾；不把“推迟/移交”写成“实现”。
- [x] 交付修改清单、测试通过/跳过明细、未完成项，以及 profiler 的原始附件（如果已采集）。
- [x] 执行阶段按项目规则在 dev 分批提交测试修正、复现入口、证据/文档；只加入本任务文件和本任务修改，不自动合入 main。

**本轮不做的性能实现：** 完整 IOBinding、buffer 池、自定义 allocator、检测头细粒度量化、ORT Entropy 内部修补。只有新的测量证据或 TRT 里程碑需要它们时再制定实现计划。


## 2026-09-10 执行记录

| 工作 | 实际验证 |
|---|---|
| 缺模型测试修复 | 修改前 build-gpu 的两项 INT8 用例失败；修改后临时源码快照中，全部缺模型/图的 9 项及仅缺 INT8 的 2 项均正确 skip |
| 正常回归 | CPU 构建的 8 个 CTest 目标通过：32 个 GTest 用例及 4 个 Python 参数用例，无 skip |
| 跨构建读取产物 | build-gpu 指向现有 CPU build 的模型目录，2 项 INT8 一致性用例通过；测试使用 CPU EP |
| 校准参数 | 新测试先红后绿；Python/CMake 拒绝负数，默认 coco128/32 图、显式 8 图、缺目录回退单图均验证；没有重跑校准 |
| benchmark | 普通 CPU 包版本运行完成，输出 warmup=20/iters=200，4 行端到端对照均检出 5 框；仅作为功能冒烟检查，不更新历史性能结论 |
| NVTX | 默认关闭版本与 NVTX v3.2.2 开启版本都编译成功；头文件解压在 build/quant-tools，没有改系统 CUDA |
| 格式与 CI | CI 收集范围覆盖 87 个 C++ 文件（quant 9、W16 12），clang-format-21 通过；完整 ORT CI 未实现，已在 notes 单列 |
| 真实 GPU profiling | 未完成：nvidia-smi 退出 255，提示 GPU access blocked by the operating system；cuInit(0) 返回 100。未安装 Nsight，未产生 GPU trace、归因报告或新 GPU before/after 数据 |

代码提交：`f41dfcc`（测试修复、复现入口、NVTX 与 CI 格式范围）；文档同步为后续独立提交。新增测试产生的 Python 字节码缓存也列入忽略范围。

本地原始日志在 `build/quant-closeout-evidence/`：`missing-int8-before.log`、
`missing-assets-after.log`、`ctest-cpu.log`、`ctest-gpu-build-int8.log`、
`default-quant-dry-run.log`、`explicit-limit-fallback-dry-run.log`、
`invalid-cmake-limit.log`、`gpu-access.log`、`benchmark-cpu-smoke.md`、`format-check.log`。
未生成 trace，因此不生成“profiling 已完成”的报告或附件。

恢复采集的必要条件是 GPU 可访问且有可用的 Nsight CLI；满足后执行任务 4 剩余两步，
再更新状态。完整 IOBinding、allocator 计数与完整 CI 集成仍按批准的范围取舍处理。


## 后续执行：2026-09-10 GPU profiling

用户切换执行权限后，`/dev/dxg` 可见、CUDA 初始化成功；并明确授权直接安装所需性能工具。
本轮按任务 4 的范围执行 Nsight Systems 与 ORT 节点分配分析，未安装 perf / Nsight Compute。

- 安装与验收：2024.6.2 首次 trace 无设备事件；升级至 2025.1.3 后默认配置仍缺失，按 NVIDIA WSL 方案设置 `CuptiUseRawGpuTimestamps=false` 后恢复。完整采集的 kernel / memcpy 表均非空，8 个 NVTX 区间可见；四个 CUDA 区间每个覆盖 200 次正式推理。
- 无 profiler：6 次尝试中 5 次正常退出，1 次在全部表格输出后 SIGABRT（`corrupted double-linked list`）；失败轮独立保留并排除，不宣称已修复。
- 结果：CPU Run FP32/INT8 47.67/35.62 ms；CUDA Run 5.73/12.38 ms（五轮 P50 的中位数）。GPU kernel 325→843，新增 473 个 Q/DQ kernel，卷积仍用 float。H2D 多 45 次却只增 17,728 字节，数据量不是主要解释；设备计算与 host 调用都有额外负担。
- 归因：按 NVTX 中发起 API 的 correlation ID 匹配 GPU 事件，核对边界与事件总数；分别记录设备时间、API host 时间及墙钟，不相加求占比。ORT 25 次运行后取最后 5 次核对分配：64 个 CUDA float Conv、45 个 CPU 偏置反量化节点。
- 回归：GPU 构建 9 个 CTest 目标通过；38 个 C++ 用例通过，2 个因 CUDA 可用而不适用的 fallback 用例跳过，4 个 Python 参数测试通过。真实 CUDA 推理用例通过；该短回归不关闭退出异常。
- 交付报告：`docs/benchmarks/quant_profiling_report.md`；静态图在 `docs/benchmarks/assets/quant_profile_summary.png`。原始目录 `build/quant-closeout-evidence/20260910-nsys/`，仓库外备份 `/home/dev/code/quant-profiling-artifacts/20260910-nsys/`。
- 下一步：GPU 优化优先按 TRT M1.5/M2 验证 QDQ 融合及真实 INT8 层；完整 IOBinding、allocator、mAP 重测未实施。退出异常在长期运行/发布前另行定位。

上面的“未完成 GPU profiling”表格是首次受限会话的历史记录，由本节与新报告更新当前状态。
