---
description: # 描执行 Google Style 自动化检查与修复
---

# 描述：执行 Google Style 自动化检查与修复
# 触发指令：/google-format

步骤：
1. **修复与反馈**：
   - 检查项目目录。若为 Flat 结构，自动创建 include/, src/, tests/, docs/ 文件夹。
   - 将 .hpp 移动至 include/，.cpp 实现移动至 src/，测试代码移动至 tests/，notes.md 移动至 docs/。
   - 更新 CMake：重写 CMakeLists.txt，使用 target_include_directories 包含 include 目录，并确保 add_executable 路径正确。
2. **自动生成 .clang-format**：如果在根目录没找到，请自动根据 Google 官方标准生成一个 `.clang-format` 配置文件。
3. **全场扫描**：使用 `clang-format` 扫描项目内所有的 `.cc`, `.h`, `.cpp`, `.hpp` 文件。
4. **修复与反馈**：
   - 自动执行 `clang-format -i` 修改文件。
   - 检查命名规范。如果发现变量名不符合 `google_style_guide.md`（如私有变量没加下划线），请通过重构（Rename）方式进行修复。
5. **演进式重构工作流 (Evolutionary Refactoring Workflow)**
   - **触发条件**：任何从旧标准（C++11/14/17）向新标准（C++20/23）的代码转换。
   - **强制注释模板**：在关键代码块上方必须插入：
     ```cpp
     // [Legacy C++11/17]: <描述旧版实现方式>
     // [Pain Point]: <说明旧版的性能瓶颈或维护隐患，例如：手动内存管理风险、虚假唤醒、缺乏编译期约束>
     // [Modern C++20/23]: <说明新特性如何解决问题，例如：jthread 的 RAII 特性、std::span 的零拷贝视图>
     ```
   - **笔记同步**：格式化代码后，自动在对应的 `notes.md` 中生成“技术演进复盘”小节，总结转换的核心逻辑差异。
6. **编译验证**：修改完成后，自动运行 `cmake` 确保重命名操作没有破坏代码逻辑。
   - 运行 cmake .. && make。
   - 新增：如果存在 tests/ 目录，必须运行编译后的测试程序，验证逻辑未因重构损坏。
