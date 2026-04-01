# 开发日志

> 记录每日操作、有效命令、踩坑解决方案和环境变更。
> 日期倒序（最新在最上方）。
> 平时在「草稿区」随手记，定期说"帮我整理一下 devlog 草稿"让 Claude 归类整理。

---

## 草稿区

<!-- 在这里随手记零散内容，不用管格式 -->

---

## 2026-04-02

### 操作摘要
- 更新 `01_Linux_CPP_Foundations/w13_image_pipeline/notes.md`，将全部 ASCII 图替换为 Mermaid（flowchart / sequenceDiagram / stateDiagram-v2）
- 配置 Telegram Claude 插件的持久化启动方案（tmux）
- 重组 `docs/` 目录结构：拍平单文件子目录、归档 tech-debt、删除 claude-usage.md、新增 README.md 导航索引
- 新建 `docs/devlog.md` 开发日志系统

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
```

### 环境变更
- 安装 tmux 3.6a（`sudo apt-get install -y tmux`）
