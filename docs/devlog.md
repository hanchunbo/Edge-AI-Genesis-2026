# 开发日志

> 记录每日操作、有效命令、踩坑解决方案和环境变更。
> 日期倒序（最新在最上方）。
> 平时在「草稿区」随手记，定期说"帮我整理一下 devlog 草稿"让 Claude 归类整理。

---

## 草稿区

<!-- 在这里随手记零散内容，不用管格式 -->

---

## 2026-05-21

### 操作摘要
- Session 2 下半场（W11 调试三件套深讲 + 三项实操）—— Session 2 收口
- 新建 `study-log` skill（`.claude/skills/study-log/`）：每日学习总结入库工作流，本条目即其第一次试跑产出
- 沉淀产物：`interview_faq.md` 新增 Q33-Q35，`q1_self_test.md` / `README.md` 同步更新

### 今日深讲内容
- **Valgrind 四种泄漏**：按"退出时还能否 reach"判定 —— definitely（必修）/ indirectly（被连累，修根即消）/ possibly（指针指中间，人工查）/ still reachable（长命服务 RSS 涨时才是真凶）；长服务该用 massif 而非 memcheck
- **GDB attach 抓死锁**：`CPU 0%` = 死锁或 IO；`gdb -p PID -batch -ex "info threads" -ex "thread apply all bt"`；栈顶 `__lll_lock_wait`/`futex_wait` = 等锁，`read`/`recv` = IO
- **perf + 火焰图**：stat 答"快不快"、record 答"慢在哪"；火焰图横轴 = 采样占比（**不是时间**）、纵轴 = 栈深，找又宽又平的方块 = 热点

### 实操记录
- **GDB attach**：`w11_buggy_lab 2` 死锁后 attach，`thread apply all bt` 拍到 ThreadA 等 `<mutex_b>`、ThreadB 等 `<mutex_a>`，循环等待闭环
- **perf stat**：buggy O(n²) vs fixed O(n log n)，task-clock 10.05s vs 0.037s（≈370×）；GCP VM 无硬件 PMU → `cycles <not supported>`
- **火焰图**：`perf record --call-graph dwarf` + FlameGraph，`FindTarget` 铺满 100% 宽度 = 热点；`fp` 收栈在 `-O0` 内层循环走飞，换 `dwarf` 解决
- 踩到三个真实环境坑：`ptrace_scope=1`（attach 提权）、`perf_event_paranoid=4`（采样被拦，临时降到 1）、云主机虚拟掉 PMU

### 命令备忘
```bash
# GDB attach 抓死锁
sudo gdb -p <PID> -batch -ex "info threads" -ex "thread apply all bt"

# perf 采样 + 火焰图（需先 git clone brendangregg/FlameGraph）
sudo sysctl kernel.perf_event_paranoid=1          # 放开采样权限（临时，重启复原）
perf record -e task-clock -F 250 --call-graph dwarf -o perf.data ./prog
perf script -i perf.data | ~/FlameGraph/stackcollapse-perf.pl > out.folded
~/FlameGraph/flamegraph.pl out.folded > flame.svg
```

### 待办
- **Session 3**（W10 Resize 数学 + W9 mdspan）—— 待补
- Session 3 完成后进 W14（ONNX Runtime 集成）

### 关联
- 深讲补充题：interview_faq.md Q33-Q35
- 自测题库：q1_self_test.md A11

---

## 2026-05-20

### 操作摘要
- Session 2 上半场（W5 深讲）+ `std::expected` 概念再深化 —— 接续 2026-05-18 Q1 自测复盘
- 沉淀产物：`docs/interview_faq.md` 新增 Q31（expected / 错误码 / sum type 选型）、Q32（W5 jthread / stop_token / 伪共享）

### 今日深讲内容
- **std::expected 再深化**：
  - 错误码"输出参数"陷阱 —— 返回值被错误码占用，结果挤进 `out` 参数，逼调用方先 `Model m;` 构造一个"空对象"（造两次 / 逼出默认构造函数 / "已构造但无效"危险窗口 / 违反 RAII）
  - expected ≠ "返回多类型工具" —— 返回多值/多类型早有 pair/tuple/variant/optional；expected 的不可缺特征是「互斥 + 有方向 + 错误处理 API」
  - expected 的真正对手是异常与错误码，不是 pair
- **W5（Session 2 上半场）**：
  - jthread = thread + RAII 自动 join + 自带停止机制
  - stop_token vs atomic<bool> 本质区别 —— atomic 是被动数据，唤不醒睡在 `cv.wait` 里的线程；stop_token 是通知框架（`condition_variable_any` 已接入 + `stop_callback`）
  - alignas(64) 伪共享 —— 两个无关变量同处一条 64B cache line，多核写各自变量却引发 cache line 乒乓；`std::hardware_destructive_interference_size`

### 待办
- **Session 2 下半场**（W11 调试三件套：Valgrind 三种 leak + Perf + 火焰图横纵轴）—— 今日未完成，待补
- **Session 3**（W10 Resize 数学 + W9 mdspan）—— 待补
- 完成 W11 + Session 3 后进 W14（ONNX Runtime 集成）

### 关联
- 自测题库：docs/q1_self_test.md（历史成绩表已加 2026-05-20 行）
- 深讲补充题：docs/interview_faq.md Q27-Q32

---

## 2026-05-18

### 操作摘要
- Q1 W1-W13 知识自测复盘（W14 ONNX Runtime 集成启动前的最后体检）
- 完成 13 题自测 + 评分 + 4 红色短板诊断 + Session 1（W2 noexcept move + W3 Concepts/expected）深讲
- 沉淀产物：`docs/q1_self_test.md`（quiz 自测，答案折叠）+ `docs/interview_faq.md` Q27-Q30（深讲补充题）

### 自测结果
- **整体成绩**：GPA ≈ C+（65-70 分）
- **扎实周次**：W1 / W6 / W7 / W8
- **半懂周次**：W4 / W5 / W9
- **红色短板（必补）**：
  - **W2**：误以为 "move 后 x 已销毁"（实际为 valid but unspecified state）
  - **W3**：Concepts + std::expected 概念忘了
  - **W10**：Resize 双线性 4 邻权重 + Letterbox padding 计算不会
  - **W11**：Valgrind 三种 leak + 火焰图横纵轴释义不会

### 待办（Session 2 + 3 排期）
- **Session 2**（~45min，待安排）：W5 stop_token 与 atomic<bool> 本质区别 + W11 调试三件套（Valgrind + Perf + 火焰图）
- **Session 3**（~30min，待安排）：W10 Resize 数学（双线性 + Letterbox）+ W9 mdspan 复活
- 完成全部 3 个 Session 后再进 W14

### 命令备忘
```bash
# 自测复盘文档位置
docs/q1_self_test.md          # quiz 格式自测
docs/interview_faq.md         # Q27-Q30 深讲
```

### 关联
- 副产出：Obsidian inbox/2026-05-18-edge-ai-q1-cpp20-self-test.md（已存档）

---

## 2026-04-03

### 操作摘要
- 排查 VSCode Remote SSH 反复掉线问题，定位出两条独立根因并修复

### 问题与排查

**现象**：白天 VSCode 连 VPS 正常，晚上回来就掉线，已复现多次

**根因 1（OOM Killer，有日志铁证）**：
- tmux 内的 claude 进程运行了 1 天 6 小时，内存峰值膨胀至 1.6G + Swap 2G
- VPS 只有 1.9Gi RAM，触发 OOM Killer，SSH 进程被连带杀掉
- 日志时间线：`08:04:26 OOM killed tmux scope → 08:04:28 SSH Connection closed`

**根因 2（SSH 无 Keepalive，NAT 超时静默断线）**：
- `/etc/ssh/sshd_config` 中 `ClientAliveInterval` 和 `TCPKeepAlive` 均被注释，实际禁用
- 内核 TCP keepalive 为 7200s，远超运营商 NAT 空闲超时（通常 5~30 分钟）
- 连接空闲时 NAT 表项过期，双端无感知，连接悄悄死掉

### 修复方案

**Fix 1（已执行）**：开启 sshd keepalive
```bash
# /etc/ssh/sshd_config 修改项：
# TCPKeepAlive yes
# ClientAliveInterval 60   （每 60s 探测一次）
# ClientAliveCountMax 10   （连续 10 次无响应约 10 分钟才断）
sudo systemctl reload sshd
```

**Fix 2（待执行）**：本地 `~/.ssh/config` 也加 keepalive（客户端双保险）
```
Host <VPS_IP>
    ServerAliveInterval 60
    ServerAliveCountMax 10
```

**Fix 3（建议）**：给 telegram tmux 的 claude 进程加内存上限，防止下次 OOM
```bash
# 重启 telegram 会话时用 systemd-run 包住，限制最大内存
systemd-run --user --scope -p MemoryMax=600M \
  tmux new-session -d -s telegram 'claude --channels plugin:telegram@claude-plugins-official --dangerously-skip-permissions'
```

### 命令备忘
```bash
# 查看 SSH 断线日志
journalctl -u ssh --since "today" --no-pager | grep -E "Disconnect|timeout|killed"

# 查看 OOM 事件
journalctl --since "today" --no-pager | grep -E "OOM|oom-killer"

# 查看各进程内存排名
ps aux --sort=-%mem | head -15

# 验证 sshd keepalive 已生效
sudo grep -E "ClientAlive|TCPKeepAlive" /etc/ssh/sshd_config
```

---

## 2026-04-02

### 操作摘要
- 更新 `01_Linux_CPP_Foundations/w13_image_pipeline/notes.md`，将全部 ASCII 图替换为 Mermaid（flowchart / sequenceDiagram / stateDiagram-v2）
- 配置 Telegram Claude 插件的持久化启动方案（tmux）
- 重组 `docs/` 目录结构：拍平单文件子目录、归档 tech-debt、删除 claude-usage.md、新增 README.md 导航索引
- 新建 `docs/devlog.md` 开发日志系统
- 在 BotFather 新建 bot `@ChunClaudebot`，替换旧 token
- 更新 `~/.claude/channels/telegram/.env` 为新 token（`8636641025:...`）
- 终止遗留的旧 token 进程（PID 16817），避免双进程冲突
- 将 dmPolicy 临时切换为 `pairing`，完成新 Telegram 账号（`8627270441`）配对
- 锁回 `allowlist` 模式，allowlist 现有两个账号：`8200284523`（旧）、`8627270441`（新）

### 问题与排查
- **现象**：更换 bot token 后发消息无回复
- **根因 1**：旧进程仍在运行并持有旧 token，与新进程并存导致 getUpdates 争抢
- **根因 2**：新 Telegram 账号 ID 不在 allowlist，消息被静默丢弃
- **解法**：kill 旧进程 → 切 pairing 模式 → 配对新账号 → 锁回 allowlist

### 环境变更
- 安装 tmux 3.6a（`sudo apt-get install -y tmux`）

### 命令备忘
```bash
# 查看 Telegram 插件进程是否在运行
ps aux | grep telegram | grep -v grep

# tmux 后台启动 Telegram 插件（终端关闭后仍运行）
tmux new-session -d -s telegram 'claude --channels plugin:telegram@claude-plugins-official --dangerously-skip-permissions'

# 查看所有 tmux 会话
tmux ls

# 重启 Telegram 插件
tmux kill-session -t telegram
tmux new-session -d -s telegram 'claude --channels plugin:telegram@claude-plugins-official --dangerously-skip-permissions'

# 进入会话查看日志（Ctrl+B D 退出但不关闭）
tmux attach -t telegram

# 检查并清理重复的 telegram 进程
ps aux | grep telegram | grep -v grep
kill <旧PID>

# 配对新账号
# 1. 切换为 pairing 模式（Claude Code 内执行）：/telegram:access policy pairing
# 2. 新账号 DM bot，获得 6 位码后执行：/telegram:access pair <code>
# 3. 锁回 allowlist：/telegram:access policy allowlist
```
