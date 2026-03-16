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
# 前提：已安装 OpenCV（见 third_party/opencv-debs/install.sh）
cmake -B build -S . -DCMAKE_CXX_COMPILER=g++-15 -G Ninja
cmake --build build --target w9_bgr2gray w9_bgr2gray_test -j$(nproc)

# 运行测试
ctest --test-dir build -R "W9_" --output-on-failure

# 运行 benchmark
./build/01_Linux_CPP_Foundations/w9_opencv_optimized/w9_bgr2gray
```

## OpenCV 离线依赖

存储于 `third_party/opencv-debs/`（32 个 .deb），版本 4.10.0+dfsg-7。
换机器时执行：`sudo bash third_party/opencv-debs/install.sh`

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
