# 开发日志

> 记录每日操作、有效命令、踩坑解决方案和环境变更。
> 日期倒序（最新在最上方）。
> 平时在「草稿区」随手记，定期说"帮我整理一下 devlog 草稿"让 Claude 归类整理。

---

## 草稿区

<!-- 在这里随手记零散内容，不用管格式 -->

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
