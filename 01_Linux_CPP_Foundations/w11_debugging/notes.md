# W11 性能调优工具链：定位瓶颈与 Bug

> **目标**：掌握 GDB / Valgrind / Perf 三大调试工具，通过复现和修复三种
> 经典 Bug 建立系统性排查思路。

---

## 实验项目结构

| 文件 | 说明 |
|------|------|
| `buggy_lab.cpp` | 包含三种故意漏洞的演示程序（内存泄漏/死锁/冗余循环） |
| `fixed_lab.cpp` | 对应的修复版本 |
| `fixed_lab.hpp` | 修复版公开接口（供 GTest 引用） |
| `debugging_test.cpp` | GTest：5 个用例验证修复后行为 |

---

## Bug 一览与修复

| Bug | 类型 | 根因 | 修复方案 |
|-----|------|------|---------|
| Bug 1 | 内存泄漏 | `new[]` 后丢弃指针 | `std::vector<uint8_t>` RAII 自动释放 |
| Bug 2 | 死锁 | 两线程逆序加锁 | `std::scoped_lock(a, b)` 同时锁，消除顺序依赖 |
| Bug 3 | O(n²) 慢搜索 | 朴素双重循环 | `std::sort` + `std::binary_search`，O(n log n) |

---

## Valgrind 使用手册

### 检测内存泄漏（Bug 1）

```bash
# 编译（需保留调试信息）
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target w11_buggy_lab w11_fixed_lab

# 验证 buggy 版本泄漏
valgrind --leak-check=full --show-leak-kinds=all \
  ./build/01_Linux_CPP_Foundations/w11_debugging/w11_buggy_lab 1
# 输出：definitely lost: 5,242,880 bytes in 5 blocks（5 帧 × 1MB）

# 验证 fixed 版本无泄漏
valgrind --leak-check=full --error-exitcode=1 \
  ./build/01_Linux_CPP_Foundations/w11_debugging/w11_fixed_lab
# 输出：All heap blocks were freed -- no leaks are possible ✅
```

### 关键 Valgrind 输出解读

```
==PID== LEAK SUMMARY:
==PID==    definitely lost: 5,242,880 bytes in 5 blocks  ← 确定泄漏（最严重）
==PID==    indirectly lost: 0 bytes in 0 blocks
==PID==      possibly lost: 0 bytes in 0 blocks          ← 可能泄漏
==PID==    still reachable: 0 bytes in 0 blocks          ← 还有引用但未释放
```

---

## GDB 使用手册

### 复现和定位死锁（Bug 2）

```bash
# 启动 GDB
gdb ./build/01_Linux_CPP_Foundations/w11_debugging/w11_buggy_lab

(gdb) run 2          # 运行 bug 2（会卡住）
# Ctrl+C 中断
(gdb) info threads   # 查看所有线程状态
(gdb) thread 2       # 切换到线程 2
(gdb) bt             # 查看调用栈，能看到 __lll_lock_wait 说明在等锁
```

### 常用 GDB 命令速查

| 命令 | 说明 |
|------|------|
| `break main` | 在 main 处打断点 |
| `run [args]` | 运行程序 |
| `bt` | 打印调用栈 |
| `info threads` | 列出所有线程 |
| `thread N` | 切换到第 N 个线程 |
| `p variable` | 打印变量值 |
| `next` / `step` | 单步（不进入/进入函数） |

---

## Perf 使用手册

### 对比 Bug 3 前后性能

```bash
# 统计 CPU 事件
perf stat ./build/01_Linux_CPP_Foundations/w11_debugging/w11_buggy_lab 3
perf stat ./build/01_Linux_CPP_Foundations/w11_debugging/w11_fixed_lab

# 火焰图生成（需 perf + FlameGraph 工具）
perf record -g ./w11_buggy_lab 3
perf script | /path/to/FlameGraph/stackcollapse-perf.pl \
  | /path/to/FlameGraph/flamegraph.pl > flame.svg
```

---

## 核心知识点

### 死锁的四个必要条件（缺一不可）

1. **互斥条件**：资源同时只能被一个线程持有
2. **持有并等待**：线程持有资源的同时等待其他资源
3. **不可剥夺**：资源只能由持有者主动释放
4. **循环等待**：形成等待环（A 等 B，B 等 A）

`std::scoped_lock` 通过打破"循环等待"来解决——内部使用 `std::lock()` 的
死锁避免算法（按地址顺序统一加锁），无论调用顺序如何都安全。

### RAII 与内存泄漏

```
手动 new/delete 的问题：
  - 异常路径下 delete 被跳过 → 泄漏
  - 多返回路径忘记 delete → 泄漏
  - 函数改动后忘记同步 delete → 泄漏

RAII 方案（std::vector / unique_ptr）：
  - 析构函数由编译器保证调用
  - 异常安全（栈展开时析构自动执行）
  - 零额外运行时代价
```

### 算法复杂度对照

| 实现 | 复杂度 | n=50000 实测 |
|------|--------|-------------|
| O(n²) 双循环 | O(n×m) | ~300ms |
| sort + lower_bound | O((n+m) log n) | <2ms |

---

## Q&A

**Q: Valgrind 会让程序变慢多少倍？**

约 10~30 倍。这是正常现象，Valgrind 在每次内存分配/释放时都插入检查钩子。
生产代码不在 Valgrind 下跑，只用于开发调试阶段。

**Q: `std::scoped_lock` 和 `std::lock_guard` 有何区别？**

`lock_guard` 只能管理一个 mutex；`scoped_lock` 可以同时管理多个，
内部调用 `std::lock()` 使用死锁避免算法，适合需要同时锁多个资源的场景。

**Q: Perf 和 Valgrind 能同时用吗？**

不能同时用，两者都需要在真实 CPU 上运行。通常流程：
1. Valgrind 检测内存问题
2. 修复后，Perf 定位性能热点
