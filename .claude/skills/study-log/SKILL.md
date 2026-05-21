---
name: study-log
description: Use when the user finishes a day's study, review, deep-dive, self-test, or hands-on session in this project and wants the conversation summarized and recorded into the project devlog. Triggers include "总结今天", "学习完了", "记录一下", "入库", "记到 devlog", or invoking /study-log.
---

# study-log

## Overview

把当天的学习 / 复习 / 自测 / 实操对话提炼成一条 devlog 条目并入库。
每天学完调一次,保证总结 **格式统一、不漏项、自动 commit**。

## When to Use

- 用户说"总结今天 / 学习完了 / 记录一下 / 入库"
- 一场复习、深讲、自测或实操对话即将结束
- **不适用**:纯排障 / 运维对话 —— 那种用 devlog 的 ops 模板,本 skill 专做 study 条目

## Workflow

1. **取日期** —— 用本次会话的当前日期,格式 `YYYY-MM-DD`。
2. **收草稿** —— 读 `docs/devlog.md`「草稿区」。有相关零散内容就并入今天的总结,然后清空草稿区(只留占位注释)。
3. **提炼对话** —— 回顾本次对话:今天学了 / 做了什么、暴露哪些短板、有哪些实操记录、留下什么待办。**只写有信息量的结论,不复述工具原始输出。**
4. **写 devlog 条目** —— 按下方模板,把新条目插在「草稿区」分隔线之后、最近一条 `## 日期` 之前(日期倒序,新的在最上)。
5. **联动更新**(按需):
   - 深讲产出新面试题 → 追加到 `docs/interview_faq.md`,Q 编号接现有最大值
   - 自测 / 复盘 → 更新 `docs/q1_self_test.md` 历史成绩表 / 答案区
   - 题数变化 → 同步 `docs/README.md` 计数
6. **提交** —— 全部改动 commit 到 `dev` 分支,**不改 git config、不 commit 到 main**:
   ```
   git -c user.name=Hanchunbo -c user.email=hanchunbo@users.noreply.github.com commit
   ```
   message:`docs: <一句话今日主题>` + 正文分条 + 结尾空行 + `Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>`。
7. **回报** —— 在回复里贴出写入的 devlog 条目全文 + commit hash。

## devlog 条目模板

`### 操作摘要` 必有,其余小节按当天内容取舍(参考 `docs/devlog.md` 的 2026-05-18 / 05-20 条目):

```markdown
## YYYY-MM-DD

### 操作摘要
- 本次对话主线 + 沉淀产物(改了哪些文件)

### 今日深讲内容        ← 复习 / 深讲日用;自测日换成 ### 自测结果
- 知识要点,分条写透

### 实操记录            ← 有动手实验时
- 工具链 + 关键证据 / 数据,不贴原始输出

### 命令备忘            ← 有可复用命令时
```bash
...
```

### 待办
- 下一步排期

### 关联
- 关联文档 + 编号
```

## Common Mistakes

| ❌ | ✅ |
|----|----|
| 整段工具输出贴进 devlog | 只留结论和关键数据 |
| 新条目追加到文件末尾 | 日期倒序,插在最新条目之前 |
| 忘了 interview_faq / q1_self_test 联动 | 按 Workflow 第 5 步检查 |
| commit 到 main / 改 git config | 一律 `dev`,用 inline `-c` |
