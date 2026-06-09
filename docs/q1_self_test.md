# Q1 自测题库（W1-W13）

> **用途**：W14（ONNX Runtime 集成）启动前的最后知识体检；季度复盘时的"快速自检"工具。
> **格式**：13 道开放题。**先盖住答案自答**，再翻到下方对比补正。
> **配套深讲**：见 [interview_faq.md](interview_faq.md) Q27-Q35（针对自测暴露的红色短板 + Session 2 W5/expected、W11 调试三件套深化的扩展题）。
> **历史成绩单**：见 [devlog.md](devlog.md) 2026-05-18 条目。

---

## 题目区（先自答，别看答案）

### W1 · RAII
**Q1**：`std::shared_ptr` 的引用计数为什么是原子操作？这在高频推理热路径上有什么代价？什么场景应改用 `unique_ptr`？

### W2 · 移动语义
**Q2**：`std::move(x)` 之后，x 处于什么状态？还能用吗？`std::vector<T>::push_back` 一个右值时，T 没写 `noexcept` 移动构造会发生什么？

### W3 · Concepts + std::expected
**Q3**：写一个 `Scannable<T>` concept 约束 T 必须有 `name()` 方法且返回类型能转 `std::string_view`。`std::expected<T, E>` 相比异常的"零开销"零在哪里？

### W4 · 并发基础
**Q4**：`std::condition_variable::wait` 为什么要传 predicate？传一个 lambda `[&]{ return !queue.empty(); }` 而不是直接 `wait()` 解决了什么问题（spurious wakeup）？

### W5 · jthread + stop_token
**Q5**：`std::jthread` 比 `std::thread` 安全在哪？`std::stop_token` 的协同中断和 `std::atomic<bool>` 标志比，差在哪？`alignas(64)` 解决的"伪共享"具体是什么物理现象？

### W6 · mmap + span
**Q6**：`mmap` 比 `std::fstream::read` 快在哪一层（内存拷贝次数 / 页错误 / 预读）？`std::span<T>` 相对裸指针 + size 多了什么保障？什么时候 span 比 vector 更合适？

### W7 · CMake
**Q7**：`target_include_directories(foo PRIVATE …)` 和 `PUBLIC` 的区别？generator expression `$<CONFIG:Debug>` 在哪一阶段展开？

### W8 · GTest + 覆盖率
**Q8**：为什么用 `FetchContent` 配本地 zip 而不是远程 URL？行覆盖率 98.7% 但漏掉的 1.3% 通常是什么类型的代码？

### W9 · mdspan + BGR2Gray
**Q9**：`cv::Mat::isContinuous()` 为什么不是恒为 true？`std::mdspan<uint8_t, std::extents<size_t, dynamic_extent, dynamic_extent, 3>>` 与裸指针 + stride 比，在 W10 Resize 实现中的可读性收益体现在哪？

### W10 · Resize
**Q10**：双线性插值的 4 个邻点权重怎么算？Letterbox 为什么必须保持比例不变？输出 640×640 但原图 1920×1080，padding 应该填多少、填在哪边？

### W11 · 调试三件套
**Q11**：Valgrind 报告中的 `definitely lost`、`indirectly lost`、`possibly lost`、`still reachable` 四种泄漏的区别？火焰图的横轴和纵轴分别代表什么？

### W12 / W13 · 综合
**Q12**：Q1 你写过最得意的一段代码是哪个？最坑你的一个 bug 是什么？

**Q13**：被追问 W13 项目时，你能用 1 分钟讲清楚"线程池架构 → 图像流水线 → 性能数据"吗？尝试讲一遍录音回放。

---

## 答案 / 补正区（请先自答再看）

<details>
<summary>📖 点开展开标准答案（自测请先盖住）</summary>

### A1（W1 RAII）
- **原子计数原因**：多线程下避免计数竞争导致内存被错误释放或泄漏
- **热路径代价**：每次 copy/dtor 触发原子操作 + 内存屏障 + 缓存行 ping-pong；推理每秒百万次时 CPU 大量花在 cache coherence 上
- **改用 unique_ptr 场景**：独占所有权、性能敏感热路径、不需要跨线程共享时

### A2（W2 移动语义）
- **moved-from 状态**：**"valid but unspecified state"** —— 对象还活着、还会被析构、可重新赋值，但**不能依赖它的具体值**
- **没 noexcept 会怎样**：`std::vector` 在扩容时要求**强异常安全保证**（操作失败需可回滚），若 move ctor 可能抛异常会破坏旧 vector 无法回滚，所以 vector **退回 copy 路径** —— `std::move_if_noexcept` 是关键判断
- 详见 interview_faq.md Q27

### A3（W3 Concepts + expected）
- **Scannable concept**：
  ```cpp
  template<typename T>
  concept Scannable = requires(T t) {
      { t.name() } -> std::convertible_to<std::string_view>;
  };
  ```
- **expected 零开销三点**：① 栈上 union 存 T 或 E，不分配堆 ② 不生成异常 unwind 表（编译产物小 5-30%）③ 失败路径就是 if/else 分支跳转，编译器可内联
- 详见 interview_faq.md Q28 + Q29

### A4（W4 并发基础）
- **predicate 作用**：解决 **spurious wakeup（虚假唤醒）**—— OS 调度 / 信号中断可能让 wait() 无 notify 也返回；predicate 封装了"醒来后再查条件"的 while 循环

### A5（W5 jthread + stop_token + alignas）
- **jthread vs thread**：自动 join（RAII），析构时不需要手动 join，防异常路径线程泄漏
- **stop_token vs atomic<bool>**：stop_token 标准化、能被 `condition_variable_any::wait` 感知；atomic<bool> 只是标志位，正在 wait 的线程无法被它唤醒
- **伪共享（False Sharing）**：两个无关变量恰好落在同一 64 字节 cache line，线程 A/B 各写自己变量时**物理上整条 cache line 在两核间反复无效化传递**；alignas(64) 是隔到不同 line，**消除** 伪共享

### A6（W6 mmap + span）
- **mmap 优势**：省了**内核→用户态那次 copy**（fstream::read 必须 kernel buffer → user buffer），mmap 直接映射 page 到用户地址空间；额外好处：内核 readahead 自动 + 按需 page fault 加载
- **span 保障**：携带 size、可越界检查、抽象 array/vector/mmap region 为统一只读 view
- **何时用 span**：函数参数（不想 copy、不想模板）、切片不拷贝、统一抽象底层存储

### A7（W7 CMake）
- **PRIVATE vs PUBLIC**：PRIVATE 只有 foo 自己需要该 include；PUBLIC = foo + 所有 link foo 的人都需要；INTERFACE 只消费者需要，foo 自己不需要
- **generator expression 展开时机**：**不是编译阶段**。在 CMake configure → generate 阶段末尾（写 ninja/make 文件那一刻）按当前 config 展开；多 config 生成器（VS、Xcode）为每个 config 各写一份

### A8（W8 GTest + 覆盖率）
- **本地 zip 原因**：离线 / 内网 / CI 环境零网络依赖，避免远程 URL 拉取失败
- **漏掉 1.3% 常见类型**：① 异常 / 错误分支（正常路径触发不到） ② assert 防御代码（断言成功跳过的 abort 分支） ③ 模板未实例化的分支 ④ switch default（编译器要求加但业务永不进入）

### A9（W9 mdspan）
- **isContinuous() 不恒 true 原因**：行间 padding 对齐（让每行 stride 是 SIMD 友好的 16/32/64 字节倍数）+ 子矩阵 / ROI 视图也不连续
- **mdspan 可读性收益**：写 `img(y, x, c)` 代替 `*(data + y*stride + x*3 + c)`；W10 双线性插值访问 4 邻居，没 mdspan 是一堆指针算式，有 mdspan 像数学公式
- 详见 interview_faq.md Q38（cv::Mat 内存模型 + ROI 不连续根因）+ Q39（mdspan 零开销 + Debug 退化 1000× 真坑）

### A10（W10 Resize）
- **双线性 4 邻权重**：目标点 (x+dx, y+dy)，dx/dy 是小数部分
  - 左上 (x,y)：(1-dx)(1-dy)
  - 右上 (x+1,y)：dx(1-dy)
  - 左下 (x,y+1)：(1-dx)dy
  - 右下 (x+1,y+1)：dx·dy
  - **所有权重和恒为 1**
- **Letterbox 保比例原因**：模型推理对图像比例敏感，**变形会扭曲特征**（方车被拉成长方形识别失败）
- **1920×1080 → 640×640 padding**：scale = 640/1920 = 0.333（按长边定），缩完 640×360；竖直补 280px = 上 140 + 下 140
- **bbox 反推口诀**：先减"单边" pad、后除 scale。99% 翻车在符号反 或 用了总 pad 而非单边
- 详见 interview_faq.md Q30 + Q36（bbox 反推）+ Q37（Asymmetric vs HalfPixel）

### A11（W11 调试三件套）
- **Valgrind 四种泄漏**（按"退出时还能否 reach 到"判定）：
  - **definitely lost** = 真泄漏，完全无指针指向分配的内存
  - **indirectly lost** = 被 definitely 连累的泄漏（如泄漏链表头结点后续的所有结点），修好根即跟着消失
  - **possibly lost** = 内部指针还在（指 malloc 块的中间位置），可能真泄漏也可能误判
  - **still reachable** = 程序退出时还有指针指着，没 free；短命程序无所谓，长命服务 RSS 持续涨时它才是真凶
- **火焰图**：
  - **X 轴 = 字典序排列的采样栈**（**不是时间！** 常见误解）
  - **Y 轴 = 调用栈深度**（越上越深，被调用者在调用者上方）
  - **方块宽度 = 该函数占总采样的百分比 = CPU 时间占比**
  - 看图诀窍：**找最宽方块 = 找性能热点**

> 深讲实操详见 interview_faq.md Q33-Q35（GDB attach 抓死锁 / perf / 火焰图实测）。

### A12 / A13
开放题，无标准答案。建议自己录音回放，**能在 60 秒内讲清"三件事 + 数据 + 难点"**即合格。

</details>

---

## 历史成绩

| 日期 | 评分 | 红色短板 | 后续行动 |
|------|------|----------|----------|
| 2026-05-18 | C+ (~65-70 分) | W2 noexcept move、W3 Concepts/expected、W10 Resize 数学、W11 调试三件套 | Session 1 完成 W2 + W3 深讲；Session 2 完成 W5（W11 待补）；Session 3 (W10 + W9) 待补 |
| 2026-05-20 | —（深讲会话） | —— | Session 2 上半场：W5 深讲 + expected 概念再深化 → 沉淀 interview_faq.md Q31-Q32 |
| 2026-05-21 | —（深讲+实操会话） | —— | Session 2 下半场：W11 调试三件套深讲 + GDB/perf/火焰图实操 → 沉淀 interview_faq.md Q33-Q35；新建 study-log skill |
| 2026-05-25 | —（深讲+自测会话） | bbox 反推符号 / mdspan shift 边界处理 | Session 3：W10 双线性 + Letterbox + Asymmetric vs HalfPixel + W9 cv::Mat 内存模型 + mdspan → 沉淀 interview_faq.md Q36-Q39；Q1 review 收尾，下一步进 W14 |

---

## 关联文档

- 学习计划：[Q1.md](Q1.md)
- 深度参考答案与加分回答：[interview_faq.md](interview_faq.md) Q27-Q39
- 技术决策 trade-offs：[Q1_decisions.md](Q1_decisions.md)
- C++20/23 新旧写法：[cpp20_23_cheatsheet.md](cpp20_23_cheatsheet.md)
- 历史会话记录：[devlog.md](devlog.md)
