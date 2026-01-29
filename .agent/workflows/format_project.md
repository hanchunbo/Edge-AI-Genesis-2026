---
description: # 描执行 Google Style 自动化检查与修复
---

# 描述：执行 Google Style 自动化检查与修复
# 触发指令：/google-format

步骤：
1. **自动生成 .clang-format**：如果在根目录没找到，请自动根据 Google 官方标准生成一个 `.clang-format` 配置文件。
2. **全场扫描**：使用 `clang-format` 扫描项目内所有的 `.cc`, `.h`, `.cpp`, `.hpp` 文件。
3. **修复与反馈**：
   - 自动执行 `clang-format -i` 修改文件。
   - 检查命名规范。如果发现变量名不符合 `google_style_guide.md`（如私有变量没加下划线），请通过重构（Rename）方式进行修复。
4. **演进式重构工作流 (Evolutionary Refactoring Workflow)**
   - **触发条件**：任何从旧标准（C++11/14/17）向新标准（C++20/23）的代码转换。
   - **强制注释模板**：在关键代码块上方必须插入：
     ```cpp
     // [Legacy C++11/17]: <描述旧版实现方式>
     // [Pain Point]: <说明旧版的性能瓶颈或维护隐患，例如：手动内存管理风险、虚假唤醒、缺乏编译期约束>
     // [Modern C++20/23]: <说明新特性如何解决问题，例如：jthread 的 RAII 特性、std::span 的零拷贝视图>
     ```
   - **笔记同步**：格式化代码后，自动在对应的 `notes.md` 中生成“技术演进复盘”小节，总结转换的核心逻辑差异。
5. **编译验证**：修改完成后，自动运行 `cmake` 确保重命名操作没有破坏代码逻辑。