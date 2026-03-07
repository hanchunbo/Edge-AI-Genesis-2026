---
description: 执行 Google Style 自动化格式检查与修复
---

# 描述：执行 Google Style 自动化检查与修复
# 触发指令：/google-format

> **重要说明**：本项目每个周次模块采用**扁平结构**（`.hpp` / `.cpp` 同在 `wx_xxx/` 目录下），
> 禁止创建或重排 `include/` / `src/` / `tests/` 子目录，否则会破坏头文件相对路径引用，
> 导致编译失败。

## 步骤

### 1. 生成或检查 .clang-format

如果根目录没有 `.clang-format`，根据 Google 官方标准自动生成。
如果已存在，跳过此步。

### 2. 全量格式扫描与修复

扫描所有季度目录下的 `.cpp` / `.hpp` 文件并自动修复：

```bash
find . -maxdepth 3 -regex '.*0[1-4]_.*' \( -name "*.cpp" -o -name "*.hpp" \) \
  | xargs clang-format -i
```

修复后再做一次 dry-run 确认无残留违规：

```bash
find . -maxdepth 3 -regex '.*0[1-4]_.*' \( -name "*.cpp" -o -name "*.hpp" \) \
  | xargs clang-format --dry-run --Werror
```

### 3. 命名规范检查

检查 `.cpp` / `.hpp` 中是否有不符合 `google_style_guide.md` 的命名：

- 私有成员变量必须有尾部下划线（如 `data_`）
- 常量必须以 `k` 开头（如 `kMaxSize`）
- 函数名必须大驼峰（如 `GetCount()`）

如发现问题，通过重构（Rename）方式修复，**不要手动字符串替换**。

### 4. 演进式注释检查

触发条件：任何从旧标准（C++11/14/17）向新标准（C++20/23）转换的代码块。

在关键代码上方必须插入以下三行演进注释（用中文填写尖括号内容）：

```cpp
// [Legacy C++11/17]: <描述旧版实现方式，如：手动 join std::thread>
// [Pain Point]: <说明旧版的痛点，如：忘记 join 导致程序崩溃>
// [Modern C++20/23]: <说明新特性如何解决，如：std::jthread RAII 自动汇合>
```

格式化代码后，在对应模块的 `notes.md` 中同步"技术演进复盘"小节，
总结新旧写法的核心差异。

### 5. 编译验证

修改完成后，必须验证编译和测试全部通过：

```bash
# 项目根目录执行（build 目录若不存在会自动创建）
cmake -B build -S . -DCMAKE_CXX_COMPILER=g++-13
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

如果测试失败，说明重构破坏了逻辑，必须回滚并排查原因，**不得跳过失败的测试**。
