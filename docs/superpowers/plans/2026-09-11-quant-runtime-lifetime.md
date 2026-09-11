# quant 运行时退出顺序修复计划

> 串行执行；用户已授权定位、修复 quant 退出崩溃并验证。采用 systematic-debugging、test-driven-development 与 verification-before-completion。

**Goal:** 消除 quant benchmark 在进程静态析构阶段触发的 ORT CUDA 注册表释放后访问。

**Architecture:** quant 的 `main` 用局部 `Ort::Env` 作为唯一运行时所有者，经 `SessionConfig` 和 `DetectorConfig` 的可选借用指针传递到所有会话。声明顺序确保全部会话和输出先销毁，Env 随后销毁，最后才执行共享库的静态析构。旧构造调用兼容保留；旧 `GlobalEnv()` 路径仍有相同依赖风险，不能宣称整个仓库已迁移。

**Tech Stack:** GCC 15、Ninja、ORT 1.26.0、GTest、GDB 15.1、Valgrind 3.22、ASAN。

## 已确认的证据

- 原始模型、图片、可执行文件的 SHA256 与 2026-09-10 完全一致。
- 三轮原始 GDB 基准、十轮短 GDB 基准、三轮短 ASAN 基准及一轮完整 ASAN 基准未重现 glibc abort；不能据此宣布修复。
- Valgrind 在只创建一次 CUDA 会话的程序退出时发现：`Ort::Env::~Env()` → CUDA 后端访问已由 `__run_exit_handlers` 释放的 64 字节控制块。
- 进一步去除模型与 Session，只保留 CUDA provider 初始化，仍有同一退出 UAF。局部 Env 对照消除了该处 UAF。
- ORT v1.26.0 `cuda_execution_provider.cc` 的静态 `s_kernel_registry` 在 `DeleteRegistry()` 中再次 reset；反汇编确认故障指令修改控制块引用计数。符合静态销毁顺序问题。WSL 驱动映射读报告另行保留，不伪称 Valgrind 全零。
- 原始证据位于仓库外 `../quant-profiling-artifacts/20260911-heap-debug/`。

## 文件与执行步骤

- [x] 保留原始失败日志、模型哈希和用户的 8 行注释 diff；安装 GDB/Valgrind 与匹配现有 glibc 的调试符号，不升级 glibc/CUDA/ORT。
- [x] 用 GDB、ASAN、最小会话及仅注册表程序进行对照，记录每种工具的边界。
- [x] 在 `w14_ort_basics/inference_engine.hpp` 的 `SessionConfig` 和 `w16_yolo_detector/yolo_detector.hpp` 的 `DetectorConfig` 末尾增加 `Ort::Env* env = nullptr`。注明借用所有权，必须比会话存活更久。此时暂不改构造函数选择逻辑。
- [x] 新增 `quantization/runtime_lifetime_test.cpp`，作为独立进程覆盖显式 Env 下直接引擎、检测器、harness 的推理与退出。`--cuda-registry` 模式只构造/销毁引擎与检测器，再初始化 CUDA 注册表。在 Valgrind 下先确认旧选择逻辑仍触发目标退出 UAF；保存 red XML/日志。
- [x] `InferenceEngine` 构造时使用 `cfg.env ? *cfg.env : GlobalEnv()`；`YOLODetector` 透传 `cfg.env`。不提前初始化旧 static Env。
- [x] `quant_benchmark.cpp` 在 `main` 开头声明局部 Env，纯推理与流水线两路均传 `&env`。用 RAII 覆盖正常返回及栈展开，不用 `_Exit`、泄漏或更换依赖掩盖问题。quant eval 测试的 CPU/CUDA 用例均显式传 Env，避免前序测试初始化旧 static Env。
- [x] 重跑同一个 Valgrind 回归，确认目标控制块 UAF 消失；保存其他驱动报告与边界。再跑实际 GPU 完整基准多轮、ASAN 与 W14/W16/Quant CTest。
- [x] 将本轮证据、复现命令及边界写入 `docs/benchmarks/quant_runtime_lifetime.md`；同步 README、Roadmap、技术债、AGENTS、CLAUDE 和模块笔记。历史性能表不改写为本轮测量结果。
- [x] 检查 clang-format-21、`git diff --check`、文档链接与用户注释未变化。
- 交付方式：仅提交本任务文件，在 dev 提交并推送；提交与远端同步状态以实际 Git 记录为准。

## 修复的边界

借用 Env 是显式生命周期契约；不得让它先于使用它的会话销毁。未迁移的历史入口仍走 static Env，另记遗留，不用 quant 单个入口的修复覆盖它们。此次不扩展完整 IOBinding、CI 或 TRT M2。

## 验证记录

- 有效 Valgrind red/green：目标 UAF 上下文 2→0；全部上下文 17→15，WSL 驱动映射读报告保留。
- 完整 benchmark 3/3 正常退出，CUDA 无回退，全部流水线均检测 5 框。
- ORT GPU/CPU 包各 10/10 CTest 目标通过；GTest 分别 38 通过/2 跳过、39 通过/1 跳过；各含 4 个通过的 Python 用例和新增独立生命周期测试。
- 修复后 ASAN 默认推理/CUDA 注册表模式均通过，泄漏检查关闭；不能替代 Valgrind 对未插桩后端的检查。
- 独立代码审查无阻塞项，指出的过时注释已修正。88 个 C++ 文件通过 clang-format-21，两幅模块 Mermaid 图通过 mmdc 渲染。
- 用户原有 eval_harness.cpp/hpp 共 8 行注释 diff 保持不变，不纳入本次提交。
