# 注释规范统一设计 — W16+ 前向代码（演进式注释存档到 W15）

> 状态：已与用户逐项确认（2026-07-16 brainstorming 会话）
> 范围：注释标准正文、4 个标准文件的落地改动、W16/quant/trt 存量回填

## 1. 背景与问题

- quant 模块（已收口交付物）注释几乎为零：`quant_benchmark.cpp` 307 行仅 3 行注释、
  `quantize_yolov8_static.py` 247 行约 11 行——决策最密集的模块反而最难读。
- 现行标准分散在 4 处（CLAUDE.md、AGENTS.md、google_style_guide.md §4、
  format_project.md），且主力格式「演进式注释」`[Legacy]/[Pain Point]/[Modern]`
  是为 W1–W15 学习存档设计的（新旧 C++ 写法对比叙事），对 W16+ 的领域逻辑代码
  （量化决策、benchmark、TRT API）天然套不上——旗舰格式不适用、轻量要求未强制，
  结果就是没人写。
- 现状注释密度：W1–W15 演进式风格（冻结）；W16 约 10%；quant 约 1%；trt 约 10%。
  W16/quant/trt **零演进式注释**（`grep -rn "Legacy C++\|Pain Point\|Modern C++"` 无命中）
  ——印证 W16 是新风格的天然起点，演进式无需清理。

## 2. 关键决策（已确认）

| 决策点 | 结论 | 理由 |
|---|---|---|
| 演进式注释边界 | 只存档到 **W15**，冻结不改、不再新增 | W16 已基本不用演进式，是新风格天然起点 |
| 首要读者 | **未来的自己** | 几个月后回来不重读实现、不重新推导即可重建上下文；排除展示性/教学性注释 |
| 注释 vs notes.md | **位置绑定原则** | 注释只写「离开这行代码就没意义」的行级事实；模块级叙事只在 notes.md 一份，指针连接——延续项目「单一事实源」 |
| 函数头强制线 | **全覆盖**（类 + 非平凡函数，含 cpp 内部） | 扫一眼函数头即知其职责；quant 事故证明"只靠自觉"不行 |
| 语法层 | **行业标准，不自造格式**：C++ 用 Doxygen `///`，Python 用 Google 风格 docstring | 工具兼容（clangd 悬停渲染）、零学习成本；TS/JS 仓库中不存在，按 YAGNI 不进标准 |
| 坑标记 | 函数头用 Doxygen 标准标签 `@pre`/`@warning`/`@note`；函数体内用 `// 注意：` 前缀 | 优先工具链标准；体内标签不可用故以统一前缀兜底 |
| 存量处理 | W16 + quant + trt **全部回填** | quant 决策记忆还热，拖延则回填质量逐月下降 |

## 3. 标准正文（替换 CLAUDE.md 对应两节）

```markdown
### Documentation & Comments（注释与文档规范，W16 起）

- **语言**：注释一律简体中文（英文标识符除外）；读者是几个月后的自己——
  不重读实现就能重建上下文。
- **函数头（语法用行业标准，不自造格式）**：
  - C++：Doxygen `///`。一句话 brief 必写（类 + 非平凡函数，含 cpp 内部）；
    `@param`/`@return` 仅语义不显然时补；坑用标准标签——`@pre`（前置条件）、
    `@warning`（陷阱/失效条件）、`@note`（其他注意）。
    平凡函数豁免（getter / 单行转发 / 命名即全部语义）。
  - Python：Google 风格 docstring，同样一句话起步。
- **函数体内注释只写两种**：意图（为什么这样做、魔法数字出处）与复杂算法
  关键步骤；体内的坑统一 `// 注意：` 前缀。禁止翻译代码式注释；
  改代码必须同步改注释。
- **位置绑定**：模块级叙事（整体设计、踩坑复盘、基准数据）只在 notes.md 一份，
  注释不重复正文，需要时一行指针（如「基准方法见 notes.md §噪声判定」）。
- **陷阱检索**：`grep -rnE "@(pre|warning|note)|注意：" <模块>/`
- **测试代码**：同标准；TEST 宏体不要求头注释（测试名即描述）。
- 文件头模板（SPDX + 一句话功能）不变，见下节。

### 演进式注释（W1–W15 存档专属）

`[Legacy]/[Pain Point]/[Modern]` 三段式仅存在于 W1–W15 存档模块，冻结不改、
不再新增。W16 起若确需说明新旧取舍，一行「为什么用 X 而非 Y」写进意图注释即可。
```

示例（体现三类信息：brief / 坑标签 / 魔法数字出处）：

```cpp
/// 对单张图跑完整检测链路，返回纯 infer 耗时（ms）。
/// @warning detections 指向 session 内部缓冲，下次 Run() 后失效
[[nodiscard]] double RunOnce(const cv::Mat& img, std::span<Detection>& out);

// coco128 实测 p99 噪声约 2.8%，阈值留到 3.0 避免误报
//（基准方法见 notes.md §噪声判定）
constexpr double kNoisePct = 3.0;
```

## 4. 标准文件落地（4 处改动）

1. **`CLAUDE.md`**：用 §3 两节替换现有「Documentation & Comments」与
   「Evolutionary Comment Pattern」——此处为单一事实源。
2. **`AGENTS.md`**：同步替换对应小节（Codex 等的独立副本，当前已滞后，顺带对齐）。
3. **`.agent/rules/google_style_guide.md` §4**：保留「每个类和非平凡函数必须有
   功能描述」，追加「W16+ 注释规范以 CLAUDE.md 为准（Doxygen 语法 + 标准坑标签）」；
   原有「仅用 `//` 单行注释」与 `///` 不冲突，不动。
4. **`.agent/workflows/format_project.md`**：「必须插入三行演进注释」限定为
   W1–W15 存档维护场景；W16+ 指向新标准。

## 5. 存量回填（三模块，纯注释 commit）

| 模块 | 规模 | 回填要点 |
|---|---|---|
| `02_Inference_Analysis/quantization` | 9 个 C++ 文件 ~900 行 + tools 2 个 py ~300 行 | **重点，从零补**：FP32 保头决策、`kNoisePct` 出处、Entropy 退化坑、ORT 调用顺序约束、IOBinding 结论指针。信息源：模块 notes.md、`docs/benchmarks/quant_*.md`、devlog、git log |
| `02_Inference_Analysis/w16_yolo_detector` | 12 文件 ~1330 行 | 已有底子（hpp 字段级注释较全）：补类/函数头 brief 覆盖、契约事实（所有权/坐标系/`std::span` 生命周期）、decode 与 NMS 的算法关键步骤 |
| `02_Inference_Analysis/tensorrt` | 10 文件 ~810 行 | 补函数头覆盖 + TRT 10 API 生命周期、engine 缓存戳约束的坑标签 |

- 每模块一个独立 commit，**不含任何代码逻辑改动**（diff 好审）。
- 回填顺序：quant（记忆最热）→ W16 → trt。

### 验收标准

1. 类/非平凡函数头注释 100% 覆盖（含 cpp 内部，平凡函数豁免）；
2. 陷阱 grep（`@(pre|warning|note)|注意：`）每模块非空，且覆盖该模块
   notes.md 踩坑节的已知项；
3. 无翻译代码式注释；
4. `clang-format-21 --dry-run --Werror` 通过；
5. 注释不复制 notes.md 叙事，仅一行指针。

## 6. 不做什么（YAGNI）

- 不给 TS/JS 定注释标准（仓库无此类代码，出现时再加一行 TSDoc）；
- 不引入 Doxygen 文档生成（Doxyfile/CI 步骤）——只用其语法层；
- 不回填 W1–W15 存档（冻结）；
- 不改各模块 notes.md 正文（分工不变，注释侧只加指针）。
