# W9 OpenCV 底层实战 — 工程笔记

## 核心知识点速查

### cv::Mat 内存布局

```
Mat(H, W, CV_8UC3) 的内存：
  ├─ data → [B₀G₀R₀][B₁G₁R₁]...[BW₋₁GW₋₁RW₋₁][padding?][B₀G₀R₀]...
  ├─ step[0] = 行字节宽（≥ cols*3，对齐 padding）
  ├─ step[1] = 每元素字节数 = 3（对于 CV_8UC3）
  └─ isContinuous() = (step[0] == cols * elemSize())
```

**为什么用 `.ptr<T>(row)` 而不是 `data + row*cols*3`？**
因为 `step[0]` 不一定等于 `cols*3`（内存对齐或 ROI 子图会导致 padding），
`.ptr<T>(row)` 内部使用 `data + row * step[0]`，是正确做法。

### BGR→Gray 定点权重

| 通道 | 浮点系数 | 定点 (×256) | 误差 |
|------|---------|------------|------|
| B    | 0.114   | 29         | < 1灰度级 |
| G    | 0.587   | 150        | < 1灰度级 |
| R    | 0.299   | 77         | < 1灰度级 |
| 合计 | 1.000   | 256        | ✓ |

右移 8 位代替除以 256，避免浮点运算。

### std::mdspan (C++23)

```cpp
// HWC 布局（OpenCV 默认）：行优先，最后一维是通道
std::mdspan<const uint8_t,
            std::extents<size_t, dyn, dyn, dyn>> view(data, H, W, C);
// 访问 [r][c][ch]：view[r, c, ch]（C++23 多下标语法）

// CHW 布局（PyTorch 默认，AI 推理常用）：
std::mdspan<const uint8_t,
            std::extents<size_t, dyn, dyn, dyn>> chw_view(data, C, H, W);
// 访问 [ch][r][c]：chw_view[ch, r, c]
```

## 构建与测试

```bash
# 前提：sudo apt-get install -y libopencv-dev
cmake -B build -S . -DCMAKE_CXX_COMPILER=g++-15 -G Ninja
cmake --build build --target w9_bgr2gray w9_bgr2gray_test -j$(nproc)

# 运行测试
ctest --test-dir build -R "W9_" --output-on-failure

# 运行 benchmark
./build/01_Linux_CPP_Foundations/w9_opencv_optimized/w9_bgr2gray
```

## mdspan 兼容层

Debian GCC 15.2.0 打包版本缺少 `<mdspan>` 头文件（上游 GCC 13+ 已实现）。
解决方案：`mdspan_compat.hpp` 用 `__has_include` 检测，
自动回退到 `third_party/mdspan/`（kokkos P0009 参考实现，v0.6.0）。
等 Debian 修复打包后，只需删除 `third_party/mdspan/` 即可无缝切换到标准库。

## Benchmark 结果（1080P, Release -O3 -mavx2, 100 rounds）

| 实现 | ms/frame | 说明 |
|------|----------|------|
| V1 ptr | ~1.6ms | 双层循环基准（标量） |
| V2 span | ~0.54ms | -O3 自动 AVX2 向量化，3×256-bit 加载 32 像素/迭代 |
| V3 mdspan | ~0.50ms | 与 V2 相当（kokkos 展开后相同向量化路径） |
| V4 AVX2 | ~0.31ms | vpmaddubsw 手写，接近 cvtColor |
| cvtColor | ~0.28ms | OpenCV 内部 AVX2 + 流水线优化 |

**关键教训**：
- Debug 模式下 V3 mdspan 会退化到 560ms（模板层未内联），必须用 Release 跑 benchmark。
  根因修复：根 CMakeLists.txt 加了 `if(NOT CMAKE_BUILD_TYPE)` 默认 Release 保护。
- V2/V3 在 Release 下已被编译器自动 AVX2 向量化（3×vmovdqu 32 字节，32 像素/迭代）；
  "手写 SIMD 一定更快"的直觉是错的，编译器 shuffle table 与我们的 naive intrinsics 打平。
- V4 改用 `_mm256_maddubs_epi16`（vpmaddubsw，一条指令完成 uint8×int8 乘加）后，
  算术指令从 6×mullo+4×add 压缩为 2×maddubs+1×add，取得 ~1.7× V2 加速，逼近 cvtColor。
- V4 与 cvtColor 剩余 ~12% 差距来自 OpenCV 的预取调度和更大批次流水线。

## 🐛 踩坑实录（本次调试过程）

### 坑1：Debug 模式下性能数据严重失真

**现象**：首次跑 benchmark，结果完全违反直觉：
```
[V2 span  ] 19.7ms   ← 比 V1 慢 4x？
[V3 mdspan] 560ms    ← 极度异常
[V4 AVX2  ]  4.8ms   ← 和 V1 一样慢？
```

**排查过程**：检查 CMake 构建类型，发现没有传 `-DCMAKE_BUILD_TYPE`，默认走了 Debug（`-O0`）。

**根因**：
- V2 `std::span` 的下标运算符在 Debug 下有边界检查，每次访问 `src_flat[i*3+k]` 都走检查逻辑，比裸指针的 V1 还慢
- V3 kokkos mdspan 是多层模板封装，Debug 下完全不内联，每次 `view[r, c, ch]` 都展开成完整调用链，560ms 是真实结果
- V4 AVX2 intrinsics 在 Debug 下退化为普通函数调用，SIMD 优势归零

**修复**：根 `CMakeLists.txt` 加保护：
```cmake
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
endif()
```
**教训**：**跑性能 benchmark 必须用 Release，哪怕只是"看一眼数据"也不能省**。模板重的代码（mdspan、span）Debug 惩罚远超普通代码。

---

### 坑2：手写 AVX2（V4）比编译器自动向量化（V2）还慢

**现象**：切换 Release 后数据合理了，但 V4 仍然输给了 V2：
```
[V2 span ] 0.54ms
[V4 AVX2 ] 0.71ms   ← 手写 SIMD 竟然更慢？
```

**排查过程**：编译到汇编，对比 V2 和 V4 的指令数：
- V2 汇编：423 条向量指令，热循环有 36 条 `vpmovzxbd`
- V4 汇编：190 条向量指令

V2 的编译器生成了 **3×`vmovdqu`（256-bit）每迭代处理 32 像素**的代码，用预计算好的 shuffle table（`.LC4`～`.LC15`）做通道解交织；而我们的 V4 每迭代只处理 16 像素，4 次 128-bit 加载。

**根因**：我们 V4 的算术路径指令太多。每 8 像素需要：
- 6×`vpmullw`（B×29, G×150, R×77 各一对 lo/hi）
- 4×`vpaddw`
- 6×`_mm_cvtepu8_epi16`（通道扩展到 16-bit）
合计 16 条，而编译器对 V2 生成的代码更紧凑。

**修复**：改用 `_mm256_maddubs_epi16`（vpmaddubsw 指令），一条指令完成 `uint8×int8 → int16` 的乘加：

权重拆分方案：
```
[B, G] × [29, 99] → B×29 + G×99    max=255×128=32640 < int16上限 ✓
[G, R] × [51, 77] → G×51 + R×77    max=32640 ✓
vpaddw（wrapping）  → B×29+G×150+R×77  max=65280，wrapping后>>8仍正确 ✓
```

算术指令：6×mullo+4×add → **2×maddubs+1×add**，减少约 50%。

结果 V4 降到 0.31ms，比 V2 快 1.7×。

**教训**：**"手写 SIMD 一定比自动向量化快"是错的**。编译器在 `-O3 -mavx2` 下会生成更大批次的循环展开和更优的 shuffle 序列。想赢过编译器，必须用更高效的指令（如 maddubs），而不是简单地把标量逻辑翻译成 intrinsics。

---

### 坑3：vpmaddubsw 权重溢出问题

**困惑**：想直接用 `maddubs([B,G], [29,150])` 一步算完，但 150 作为 int8 是负数（> 127），结果完全错误。

**根因**：`_mm256_maddubs_epi16` 的 b 操作数是**有符号 int8**，范围 -128～127。权重 150 超出范围，被解释为 -106，计算结果错误。

**解决**：把 G 的权重 150 拆成两部分，保证每个权重 ≤ 127：
```
150 = 99 + 51
对 [B,G] 用权重 [29, 99]，对 [G,R] 用权重 [51, 77]
```

验证两对权重和各自 ≤ 128，最大乘积 255×128=32640 < 32767（int16上限），不饱和截断。

**教训**：使用 `vpmaddubsw` 时，b 向量的权重必须是有符号 int8（-128～127）。G 权重 150 是 W9 BGR→Gray 的典型陷阱，拆分方案 `99+51` 是正确解。

---

## 💡 疑难解惑集锦 (Q&A 总结)

### 1. CMake 魔法依赖与核心用法
* **潜规则变量**：`find_package(OpenCV)` 成功后，CMake 会在后台自动生成 `OpenCV_INCLUDE_DIRS` (去哪找头文件) 和 `OpenCV_LIBS` (去哪找编译好的 `.so` 二进制代码)。
* **日常开发三板斧**：
  1. `find_package(XXX REQUIRED)`: 让 CMake 去系统里找这个库。
  2. `target_include_directories(tgt PUBLIC ${XXX_INCLUDE_DIRS})`: 告诉编译器去哪找头文件。
  3. `target_link_libraries(tgt PUBLIC ${XXX_LIBS})`: 告诉链接器把现成的二进制实现代码绑在一起。

### 2. OpenCV 底层与内存 “坑”
* **`Mat(H, W, CV_8UC3)`**：开辟一块 高H×宽W 的 8位无符号 3通道(BGR) 彩色图像内存。
* **Padding (内存填充)**：每行的字节长度 `step[0]` **不等于** 像素宽×3 (`W * 3`)。为了硬件 CPU 的内存对齐或截取了原图的一部分(ROI)，行尾经常会填充一些废弃字节。因此遍历图片时，**必须加上跨度带来的偏移行高**，使用 `.ptr<uint8_t>(row)` 内部处理 `data + row * step[0]` 才是对的。
* **万金油 `cv::Scalar`**：专门配置各种通道组合色素点的“调色盘”。配合 `img.setTo(cv::Scalar(...))` 能够瞬间给整块内存刷满同一底色。
* **祖师爷 `cv::cvtColor`**：色彩空间转换的核心函数。在这个案例中将 BGR 转换至 Gray，作为 Benchmark 对比跑分的天下第一标杆（底层融合了 SIMD 甚至汇编优化）。

### 3. 高性能测速与交叉验证
* **抽查中心像素**：在动辄成百上千万次的死循环 Benchmark 测速中，用 $O(1)$ 的时间只抽查正中心的像素 `(H/2, W/2)`。既能防逻辑错、防算错，又丝毫不拖慢测试。
* **地毯式验算 (`cv::norm`)**：跑完大循环后，利用无穷范数 `cv::norm(..., cv::NORM_INF)` 验算整图几百万像素的最大差值。只要 $max\_diff \le 1$（肉眼不可见），说明我们发明的优化算法（如定点位移）完全等效且成功！

### 4. BGR 转灰度：定点化位移优化
* 浮点公式：$Gray = R \times 0.299 + G \times 0.587 + B \times 0.114$
* 浮点运算极慢，我们通过**整数等比例放大再除 256** 来替代：$Gray = (R \times 77 + G \times 150 + B \times 29) \div 256$。
* 除以 256 **可以用纯二进制右移 8 位 (`>> 8`) 替代**。这条绝招把本来极慢的除法降维成 1 个时钟周期的位移指令，速度碾压。

### 5. std::mdspan 与 C++23 的类型契约
* **配方和药材剥离**：`std::mdspan<const uint8_t, Extents3D>` 中，`uint8_t` 是药柜抽屉里装的药 (**真正的单个颜色值 0~255**)，而 `Extents3D` 里规定的 `size_t` 仅仅是用来找抽屉时的**长宽高坐标系约束**。
* **编译期极致压榨**：在 `extents` 里把最后一维强行由动态写死为常数 `3` ($C=3$)。编译器会将其对应的 $c \times C$ 动态乘法，强制优化成静态的极致高效指令。
* **魔法后缀 `3UZ`**：C++23 语法，强制让裸数字 `3` 穿上 `size_t` (无符号长整型，Z 代表大小) 的马甲，满足 `mdspan` 严苛的强类型契约。
* **灰度图降维打击**：因为灰度图仅有亮度值（无 RGB 色彩概念），它的 `extents` “配方”只剩下两维（行、列）。因此编译时你再传多一个维度去拿数据，编译器会直接报警拉停。
