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

固定做这四步。先用本次会话的当前日期(`YYYY-MM-DD`),并读 `docs/devlog.md`
「草稿区」与三份联动文档;草稿区有相关零散内容就并入今天的总结,然后只留占位注释。

1. **提炼今天的复习 / 实操要点** —— 回顾本次对话:今天学了 / 做了什么、暴露哪些短板、有哪些实操证据、留下什么待办。**只写有信息量的结论,不复述工具原始输出。**
2. **追加 dated devlog 条目** —— 按下方模板,把新条目插在「草稿区」分隔线之后、最近一条 `## 日期` 之前(日期倒序,新的在最上)。
3. **联动更新学习资料** —— 按下面的**三级路由**分发,核心铁律是**单一事实源:可复用概念正文只进主题库一份**(见 `CLAUDE.md` 的 Notes 知识库分工)。
   - **先判定有没有「可复用概念」** —— 判据复用 `CLAUDE.md`:「这段话离开本模块代码还成立吗?」成立 → 可复用概念。
   - **可复用概念 → `docs/notes/` 主题库(唯一正文,首次即入库)**:按主题选对文件(`inference.md` / `cpp-core.md` / `image-ops.md` 等,索引见 `docs/notes/README.md`),用三段式「是什么 / 为什么 / 坑 + 实战出处」写正文。**这是该概念的唯一正文,别处不再复制**。主题库已有该概念就补充/订正,不新开。
   - **`docs/interview_faq.md`(答题视角,不是概念正文)**:只在**真做了自测 / 暴露答案缺口**、有面试问答价值时追加题目,Q 编号接现有最大值。参考答案写「**答题角度 + 加分回答 + 指向主题库的链接**」,**不重写「是什么/为什么/坑」的概念正文**(那在主题库)。没有就不写填充题,回报时说明未新增 FAQ。
   - **`docs/q1_self_test.md`**:更新历史成绩单,并在暴露答案缺口时同步答案区 / 深讲指向。
   - FAQ 题数变化时同步 `docs/README.md` 计数;主题库新增概念时确认 `docs/notes/README.md` 索引表已覆盖。
4. **commit 到 `dev`** —— 提交全部入库改动,**不改 git config、不 commit 到 main**。提交 author 与 trailer 服从仓库 `CLAUDE.md` 的 Commit Checklist;需要覆盖 author 时用 inline `git -c`。

commit 后,在回复里贴出写入的 devlog 条目全文 + commit hash。

## devlog 条目模板

`### 操作摘要` 必有,其余小节按当天内容取舍(参考 `docs/devlog.md` 的 2026-05-18 / 05-20 条目):

````markdown
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
````

## Common Mistakes

| ❌ | ✅ |
|----|----|
| 整段工具输出贴进 devlog | 只留结论和关键数据 |
| 新条目追加到文件末尾 | 日期倒序,插在最新条目之前 |
| 忘了 interview_faq / q1_self_test 联动 | 按 Workflow 第 3 步检查 |
| 把概念正文写进 FAQ 长答案 | 概念正文进主题库 `docs/notes/`,FAQ 只留答题角度 + 加分回答 + 链接 |
| 可复用概念只进 FAQ、漏了主题库 | 先判定可复用 → 首次即入库主题库(唯一正文) |
| commit 到 main / 改 git config | 一律 `dev`,用 inline `-c` |
