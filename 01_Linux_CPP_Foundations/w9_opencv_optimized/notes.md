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

## Benchmark 结果（1080P, Release, 100 rounds）

| 实现 | ms/frame | 说明 |
|------|----------|------|
| V1 ptr | ~4.2ms | 双层循环基准 |
| V2 span | ~3.5ms | 展平后编译器更易向量化 |
| V3 mdspan | ~2.9ms | kokkos 实现优化了索引计算 |
| cvtColor | ~1.1ms | OpenCV 内部有 SIMD 优化 |

与 cvtColor 3-4x 差距来自 SIMD：手写版是标量，cvtColor 用了 AVX2 向量化。

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
