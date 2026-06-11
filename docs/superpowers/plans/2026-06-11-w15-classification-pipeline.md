# W15 分类推理端到端闭环 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 `Classifier` 把一张真实图片跑出真实的 ImageNet Top-5 分类结果（真图片 → 预处理 → 复用 W14 推理 → Top-5 标签）。

**Architecture:** 新建独立模块 `02_Inference_Analysis/w15_classify_pipeline/`（命名空间 `w15`），由三个聚焦文件组成：`preprocess`（cv::Mat→CHW float 张量）、`postprocess`（logits→Top-K 标签）、`classifier`（编排类，持有并复用 `w14::InferenceEngine`）。预处理走分类标准流程（resize 短边 256→center-crop 224→RGB→ImageNet 归一化），不依赖 W13 的检测 letterbox。

**Tech Stack:** C++20、OpenCV（core/imgproc/imgcodecs，解码+resize+crop）、ONNX Runtime（经 W14 复用）、GoogleTest、CMake + Ninja、g++-15。

**设计文档:** `docs/superpowers/specs/2026-06-11-w15-classification-pipeline-design.md`

---

## File Structure

| 文件 | 责任 |
|------|------|
| `02_Inference_Analysis/w15_classify_pipeline/preprocess.hpp/.cpp` | `PreprocConfig` + `Preprocess(cv::Mat,cfg)->vector<float>`：resize/centercrop/RGB/CHW/归一化 |
| `02_Inference_Analysis/w15_classify_pipeline/postprocess.hpp/.cpp` | `TopK` 结构 + `Softmax` + `LoadLabels` + `TopKResults` |
| `02_Inference_Analysis/w15_classify_pipeline/classifier.hpp/.cpp` | `Classifier` 编排类，复用 `w14::InferenceEngine` |
| `02_Inference_Analysis/w15_classify_pipeline/classify_demo.cpp` | 命令行 demo：`./w15_classify_demo <img>` |
| `02_Inference_Analysis/w15_classify_pipeline/preprocess_test.cpp` | 预处理单测（纯函数，无需模型） |
| `02_Inference_Analysis/w15_classify_pipeline/postprocess_test.cpp` | 后处理单测（纯函数，无需模型） |
| `02_Inference_Analysis/w15_classify_pipeline/classifier_test.cpp` | 端到端单测（需模型+标签+图，缺失则 `GTEST_SKIP`） |
| `02_Inference_Analysis/w15_classify_pipeline/CMakeLists.txt` | 模块构建：lib + demo + 3 个测试目标 |
| `02_Inference_Analysis/w15_classify_pipeline/models/imagenet_classes.txt` | ImageNet-1000 标签（1000 行） |
| `02_Inference_Analysis/w15_classify_pipeline/models/test_cat.jpg` | 已知类别测试图 |
| `02_Inference_Analysis/w15_classify_pipeline/notes.md` | W15 学习笔记 |
| `CMakeLists.txt`（根，修改） | 末尾加 `add_subdirectory(...w15_classify_pipeline)` |
| `docs/archive/tech-debt.md`（修改） | 归一化未定义项标记 FIXED |
| `README.md` + `CLAUDE.md`（修改） | 进度表更新到 W15 |

**复用说明：** 模型文件不复制，直接引用 W14 的 `02_Inference_Analysis/w14_ort_basics/models/mobilenetv2.onnx`。`Classifier` 仅依赖 OpenCV + `w14_inference_engine`，不依赖 w10/w13。

**通用命令（每个任务用到）：**
- 配置：`cmake -B build -S . -DCMAKE_CXX_COMPILER=g++-15 -G Ninja`
- 编译某目标：`cmake --build build --target <目标> -j$(nproc)`
- 跑 W15 测试：`ctest --test-dir build -R W15_ --output-on-failure`
- 格式检查（CI 强制 v21）：`find . -maxdepth 3 -regex '.*0[1-4]_.*' \( -name "*.cpp" -o -name "*.hpp" \) | xargs clang-format-21 --dry-run --Werror`
- commit 作者：`hanchunbo <hanchunbo@users.noreply.github.com>`，附 `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`

---

## Task 1: 模块骨架 + CMake 接线（空 lib 能配置编译）

**Files:**
- Create: `02_Inference_Analysis/w15_classify_pipeline/preprocess.hpp`
- Create: `02_Inference_Analysis/w15_classify_pipeline/preprocess.cpp`
- Create: `02_Inference_Analysis/w15_classify_pipeline/CMakeLists.txt`
- Modify: `CMakeLists.txt`（根）

- [ ] **Step 1: 建 `preprocess.hpp`（仅声明）**

```cpp
// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：W15 分类预处理 —— cv::Mat(BGR) 转 ImageNet 归一化的 CHW float 张量。
// ============================================================================

#ifndef EDGE_AI_GENESIS_2026_W15_PREPROCESS_HPP_
#define EDGE_AI_GENESIS_2026_W15_PREPROCESS_HPP_

#include <array>
#include <opencv2/core.hpp>
#include <vector>

namespace w15 {

// 分类预处理配置。归一化参数显式定义（消 tech-debt「归一化参数未定义」）。
struct PreprocConfig {
  int resize_short = 256;  // 短边缩放到该值，保持长宽比
  int crop = 224;          // 中心裁剪到 crop×crop
  std::array<float, 3> mean{0.485f, 0.456f, 0.406f};  // ImageNet RGB 均值
  std::array<float, 3> std{0.229f, 0.224f, 0.225f};   // ImageNet RGB 标准差
  bool to_rgb = true;  // OpenCV 读入为 BGR，分类模型按 RGB 训练，需转换
};

// 把 BGR 图像预处理成连续 CHW float32 张量（大小 3*crop*crop）。
// 步骤：resize 短边 -> center-crop -> (可选 BGR2RGB) -> /255 -> 归一化 -> HWC2CHW。
[[nodiscard]] std::vector<float> Preprocess(const cv::Mat& bgr,
                                            const PreprocConfig& cfg);

}  // namespace w15

#endif  // EDGE_AI_GENESIS_2026_W15_PREPROCESS_HPP_
```

- [ ] **Step 2: 建 `preprocess.cpp`（占位实现，返回正确尺寸的全 0，使骨架可编译）**

```cpp
// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：W15 分类预处理实现 —— 详见 preprocess.hpp。
// ============================================================================

#include "preprocess.hpp"

namespace w15 {

std::vector<float> Preprocess(const cv::Mat& /*bgr*/, const PreprocConfig& cfg) {
  // 骨架占位：Task 2 用 TDD 替换为真实实现。
  return std::vector<float>(
      static_cast<size_t>(3) * cfg.crop * cfg.crop, 0.0f);
}

}  // namespace w15
```

- [ ] **Step 3: 建模块 `CMakeLists.txt`**

```cmake
# Copyright 2026 Edge-AI-Genesis
#
# =============================================================================
# W15 分类推理端到端闭环
# =============================================================================
# 复用 W14 InferenceEngine（推理）+ OpenCV（解码/resize/crop）。
# 模型文件复用 W14 的 mobilenetv2.onnx，不在本模块重复存放。
# =============================================================================

# W14 库未构建（ORT 包缺失会让 W14 early-return）则本模块整体跳过。
if(NOT TARGET w14_inference_engine)
  message(WARNING "[W15] 未找到 w14_inference_engine 目标（ORT 包可能缺失），跳过 W15 构建。")
  return()
endif()

find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)
find_package(Threads REQUIRED)

# -----------------------------------------------------------------------------
# 库目标：w15_classify_lib
# -----------------------------------------------------------------------------
add_library(w15_classify_lib STATIC
  preprocess.cpp
  postprocess.cpp
  classifier.cpp
)
target_include_directories(w15_classify_lib PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${OpenCV_INCLUDE_DIRS}
)
target_link_libraries(w15_classify_lib PUBLIC
  w14_inference_engine        # 传递性带来 ORT include + lib
  ${OpenCV_LIBS}
  Threads::Threads
)
target_compile_features(w15_classify_lib PUBLIC cxx_std_20)

# -----------------------------------------------------------------------------
# Demo 可执行：w15_classify_demo
# -----------------------------------------------------------------------------
add_executable(w15_classify_demo classify_demo.cpp)
target_link_libraries(w15_classify_demo PRIVATE w15_classify_lib)
target_compile_definitions(w15_classify_demo PRIVATE
  W15_MODEL_PATH="${CMAKE_SOURCE_DIR}/02_Inference_Analysis/w14_ort_basics/models/mobilenetv2.onnx"
  W15_LABELS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/models/imagenet_classes.txt"
)
set_target_properties(w15_classify_demo PROPERTIES
  BUILD_RPATH "${ONNXRUNTIME_ROOT}/lib"
)

# -----------------------------------------------------------------------------
# 单元测试
# -----------------------------------------------------------------------------
if(BUILD_TESTING)
  add_executable(w15_preprocess_test preprocess_test.cpp)
  target_link_libraries(w15_preprocess_test
    PRIVATE w15_classify_lib GTest::gtest_main Threads::Threads)
  add_test(NAME W15_PreprocessTest COMMAND w15_preprocess_test)

  add_executable(w15_postprocess_test postprocess_test.cpp)
  target_link_libraries(w15_postprocess_test
    PRIVATE w15_classify_lib GTest::gtest_main Threads::Threads)
  add_test(NAME W15_PostprocessTest COMMAND w15_postprocess_test)

  add_executable(w15_classifier_test classifier_test.cpp)
  target_link_libraries(w15_classifier_test
    PRIVATE w15_classify_lib GTest::gtest_main Threads::Threads)
  target_compile_definitions(w15_classifier_test PRIVATE
    W15_MODEL_PATH="${CMAKE_SOURCE_DIR}/02_Inference_Analysis/w14_ort_basics/models/mobilenetv2.onnx"
    W15_LABELS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/models/imagenet_classes.txt"
    W15_IMAGE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/models/test_cat.jpg"
  )
  set_target_properties(w15_classifier_test PROPERTIES
    BUILD_RPATH "${ONNXRUNTIME_ROOT}/lib")
  add_test(NAME W15_ClassifierTest COMMAND w15_classifier_test)
endif()
```

> 注：本任务先建 lib + demo + 测试目标列表，但 `postprocess.cpp` / `classifier.cpp` / 三个 `*_test.cpp` / `classify_demo.cpp` 在后续任务创建。为让 Task 1 能单独配置成功，**本步先注释掉尚不存在的源文件引用**：把 `postprocess.cpp`、`classifier.cpp` 行、demo 段、test 段暂时注释，仅保留 `preprocess.cpp` 的 lib。后续任务逐步取消注释。

实操：Task 1 的 CMakeLists 先只放：

```cmake
if(NOT TARGET w14_inference_engine)
  message(WARNING "[W15] 未找到 w14_inference_engine 目标，跳过 W15 构建。")
  return()
endif()
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)
find_package(Threads REQUIRED)
add_library(w15_classify_lib STATIC preprocess.cpp)
target_include_directories(w15_classify_lib PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR} ${OpenCV_INCLUDE_DIRS})
target_link_libraries(w15_classify_lib PUBLIC
  w14_inference_engine ${OpenCV_LIBS} Threads::Threads)
target_compile_features(w15_classify_lib PUBLIC cxx_std_20)
```

每个后续任务在创建对应文件后，把上面"完整版"里对应的行加回来。

- [ ] **Step 4: 根 `CMakeLists.txt` 末尾接线**

在 `add_subdirectory(02_Inference_Analysis/w14_ort_basics)` 之后新增一行：

```cmake
add_subdirectory(02_Inference_Analysis/w15_classify_pipeline)
```

- [ ] **Step 5: 配置 + 编译验证**

Run: `cmake -B build -S . -DCMAKE_CXX_COMPILER=g++-15 -G Ninja && cmake --build build --target w15_classify_lib -j$(nproc)`
Expected: 配置无报错（出现 `[W15]` 相关无 WARNING 即 ORT 在位），`w15_classify_lib` 编译成功。

- [ ] **Step 6: Commit**

```bash
git add 02_Inference_Analysis/w15_classify_pipeline/preprocess.hpp \
        02_Inference_Analysis/w15_classify_pipeline/preprocess.cpp \
        02_Inference_Analysis/w15_classify_pipeline/CMakeLists.txt CMakeLists.txt
git commit -m "feat(W15): 模块骨架与 CMake 接线"
```

---

## Task 2: Preprocess（TDD）

**Files:**
- Test: `02_Inference_Analysis/w15_classify_pipeline/preprocess_test.cpp`
- Modify: `02_Inference_Analysis/w15_classify_pipeline/preprocess.cpp`
- Modify: `02_Inference_Analysis/w15_classify_pipeline/CMakeLists.txt`（加回 `w15_preprocess_test` 段）

- [ ] **Step 1: 写失败测试 `preprocess_test.cpp`**

```cpp
// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：W15 预处理单元测试（纯函数，无需模型）。
// ============================================================================

#include "preprocess.hpp"

#include <gtest/gtest.h>

#include <opencv2/core.hpp>

namespace {

// 纯色图经 resize/crop 后颜色不变，便于手算归一化期望值校验。
TEST(W15Preprocess, ShapeAndNormalizationOnSolidImage) {
  // BGR 纯色 (B=50, G=100, R=200)，尺寸大于 crop，确保有效区域全是该色。
  cv::Mat bgr(300, 300, CV_8UC3, cv::Scalar(50, 100, 200));
  w15::PreprocConfig cfg;  // 默认 224 crop + ImageNet 参数 + to_rgb

  std::vector<float> out = w15::Preprocess(bgr, cfg);

  // 尺寸：3 * 224 * 224
  ASSERT_EQ(out.size(), static_cast<size_t>(3) * 224 * 224);

  const size_t hw = static_cast<size_t>(224) * 224;
  // 通道顺序 RGB：c0=R(200), c1=G(100), c2=B(50)
  const float r = (200.0f / 255.0f - 0.485f) / 0.229f;  // ≈ 1.307
  const float g = (100.0f / 255.0f - 0.456f) / 0.224f;  // ≈ -0.285
  const float b = (50.0f / 255.0f - 0.406f) / 0.225f;   // ≈ -0.933

  EXPECT_NEAR(out[0 * hw + 0], r, 1e-3);
  EXPECT_NEAR(out[1 * hw + 0], g, 1e-3);
  EXPECT_NEAR(out[2 * hw + 0], b, 1e-3);
  // 纯色图任意像素都应相同
  EXPECT_NEAR(out[0 * hw + 12345], r, 1e-3);
}

// 非正方形输入也应输出正确尺寸（center-crop 生效）。
TEST(W15Preprocess, NonSquareInputProducesCorrectSize) {
  cv::Mat bgr(480, 640, CV_8UC3, cv::Scalar(0, 0, 0));
  w15::PreprocConfig cfg;
  std::vector<float> out = w15::Preprocess(bgr, cfg);
  EXPECT_EQ(out.size(), static_cast<size_t>(3) * 224 * 224);
}

}  // namespace
```

- [ ] **Step 2: 在 CMakeLists 加回 `w15_preprocess_test` 段**（见 Task 1 "完整版" 的对应 3 行 + add_test），然后配置。

Run: `cmake -B build -S . -DCMAKE_CXX_COMPILER=g++-15 -G Ninja && cmake --build build --target w15_preprocess_test -j$(nproc) && ctest --test-dir build -R W15_PreprocessTest --output-on-failure`
Expected: FAIL（占位实现返回全 0，归一化断言不通过）

- [ ] **Step 3: 写真实实现，替换 `preprocess.cpp`**

```cpp
// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：W15 分类预处理实现 —— 详见 preprocess.hpp。
// ============================================================================

#include "preprocess.hpp"

#include <algorithm>
#include <cmath>
#include <opencv2/imgproc.hpp>

namespace w15 {

std::vector<float> Preprocess(const cv::Mat& bgr, const PreprocConfig& cfg) {
  // 1) 短边 resize 到 cfg.resize_short，保持长宽比（分类标准，避免拉伸失真）
  const int h = bgr.rows;
  const int w = bgr.cols;
  const float scale =
      static_cast<float>(cfg.resize_short) / static_cast<float>(std::min(h, w));
  const int nh = static_cast<int>(std::lround(h * scale));
  const int nw = static_cast<int>(std::lround(w * scale));
  cv::Mat resized;
  cv::resize(bgr, resized, cv::Size(nw, nh), 0, 0, cv::INTER_LINEAR);

  // 2) center-crop 到 cfg.crop×cfg.crop
  const int x = (nw - cfg.crop) / 2;
  const int y = (nh - cfg.crop) / 2;
  const cv::Mat cropped =
      resized(cv::Rect(x, y, cfg.crop, cfg.crop)).clone();

  // 3) BGR2RGB + /255 + 归一化 + HWC2CHW，一次循环写入连续 CHW buffer
  const size_t hw = static_cast<size_t>(cfg.crop) * cfg.crop;
  std::vector<float> out(3 * hw);
  for (int yy = 0; yy < cfg.crop; ++yy) {
    for (int xx = 0; xx < cfg.crop; ++xx) {
      const cv::Vec3b px = cropped.at<cv::Vec3b>(yy, xx);  // BGR
      const size_t pos = static_cast<size_t>(yy) * cfg.crop + xx;
      for (int c = 0; c < 3; ++c) {
        // 输出通道 c 为 RGB 序：c=0->R, 1->G, 2->B。
        // OpenCV 像素是 BGR：R=px[2], G=px[1], B=px[0]，故 to_rgb 时 src=2-c。
        const int src_c = cfg.to_rgb ? (2 - c) : c;
        float v = static_cast<float>(px[src_c]) / 255.0f;
        v = (v - cfg.mean[c]) / cfg.std[c];
        out[c * hw + pos] = v;
      }
    }
  }
  return out;
}

}  // namespace w15
```

- [ ] **Step 4: 跑测试通过**

Run: `cmake --build build --target w15_preprocess_test -j$(nproc) && ctest --test-dir build -R W15_PreprocessTest --output-on-failure`
Expected: PASS（2 个用例全绿）

- [ ] **Step 5: 格式检查 + Commit**

```bash
find . -maxdepth 3 -regex '.*0[1-4]_.*' \( -name "*.cpp" -o -name "*.hpp" \) | xargs clang-format-21 --dry-run --Werror
git add 02_Inference_Analysis/w15_classify_pipeline/preprocess.cpp \
        02_Inference_Analysis/w15_classify_pipeline/preprocess_test.cpp \
        02_Inference_Analysis/w15_classify_pipeline/CMakeLists.txt
git commit -m "feat(W15): 分类预处理 resize/centercrop/归一化/CHW + 单测"
```

---

## Task 3: Postprocess（TDD）

**Files:**
- Create: `02_Inference_Analysis/w15_classify_pipeline/postprocess.hpp`
- Create: `02_Inference_Analysis/w15_classify_pipeline/postprocess.cpp`
- Test: `02_Inference_Analysis/w15_classify_pipeline/postprocess_test.cpp`
- Modify: `CMakeLists.txt`（lib 加 `postprocess.cpp`，加回 `w15_postprocess_test` 段）

- [ ] **Step 1: 写 `postprocess.hpp`**

```cpp
// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：W15 分类后处理 —— softmax、Top-K 选择、ImageNet 标签加载与映射。
// ============================================================================

#ifndef EDGE_AI_GENESIS_2026_W15_POSTPROCESS_HPP_
#define EDGE_AI_GENESIS_2026_W15_POSTPROCESS_HPP_

#include <span>
#include <string>
#include <vector>

namespace w15 {

// 单条 Top-K 结果。
struct TopK {
  int index;          // 类别索引
  float score;        // softmax 后概率（0~1）
  std::string label;  // ImageNet 标签文本
};

// 数值稳定 softmax（减最大值后取指数）。返回与输入等长的概率向量。
[[nodiscard]] std::vector<float> Softmax(std::span<const float> logits);

// 从文本文件按行读取标签（每行一个）。空文件抛 std::runtime_error。
[[nodiscard]] std::vector<std::string> LoadLabels(const std::string& path);

// 对 logits 做 softmax，取概率最高的 k 个，附带标签。
// 要求 logits.size() == labels.size()，否则抛 std::runtime_error。
[[nodiscard]] std::vector<TopK> TopKResults(
    std::span<const float> logits,
    const std::vector<std::string>& labels, int k);

}  // namespace w15

#endif  // EDGE_AI_GENESIS_2026_W15_POSTPROCESS_HPP_
```

- [ ] **Step 2: 写失败测试 `postprocess_test.cpp`**

```cpp
// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：W15 后处理单元测试（纯函数，无需模型）。
// ============================================================================

#include "postprocess.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <numeric>
#include <vector>

namespace {

TEST(W15Postprocess, SoftmaxSumsToOneAndMonotonic) {
  std::vector<float> logits{1.0f, 2.0f, 3.0f};
  std::vector<float> p = w15::Softmax(logits);
  ASSERT_EQ(p.size(), 3u);
  float sum = std::accumulate(p.begin(), p.end(), 0.0f);
  EXPECT_NEAR(sum, 1.0f, 1e-5);
  EXPECT_GT(p[2], p[1]);
  EXPECT_GT(p[1], p[0]);
}

TEST(W15Postprocess, TopKPicksHighestInOrder) {
  std::vector<float> logits{0.1f, 0.9f, 0.2f, 0.7f, 0.05f};
  std::vector<std::string> labels{"a", "b", "c", "d", "e"};
  std::vector<w15::TopK> top = w15::TopKResults(logits, labels, 2);

  ASSERT_EQ(top.size(), 2u);
  EXPECT_EQ(top[0].index, 1);
  EXPECT_EQ(top[0].label, "b");
  EXPECT_EQ(top[1].index, 3);
  EXPECT_EQ(top[1].label, "d");
  EXPECT_GT(top[0].score, top[1].score);   // 降序
  EXPECT_GT(top[0].score, 0.0f);
  EXPECT_LT(top[0].score, 1.0f);
}

TEST(W15Postprocess, LoadLabelsReadsLines) {
  const std::string path = "w15_labels_tmp.txt";
  {
    std::ofstream f(path);
    f << "cat\n" << "dog\n" << "bird\n";
  }
  std::vector<std::string> labels = w15::LoadLabels(path);
  std::remove(path.c_str());

  ASSERT_EQ(labels.size(), 3u);
  EXPECT_EQ(labels[0], "cat");
  EXPECT_EQ(labels[2], "bird");
}

}  // namespace
```

- [ ] **Step 3: 写占位实现 `postprocess.cpp`（让测试先编译并 FAIL）**

```cpp
// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：W15 分类后处理实现 —— 详见 postprocess.hpp。
// ============================================================================

#include "postprocess.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numeric>
#include <stdexcept>

namespace w15 {

std::vector<float> Softmax(std::span<const float> logits) {
  std::vector<float> out(logits.size());
  if (logits.empty()) {
    return out;
  }
  const float max_v = *std::max_element(logits.begin(), logits.end());
  float sum = 0.0f;
  for (size_t i = 0; i < logits.size(); ++i) {
    out[i] = std::exp(logits[i] - max_v);
    sum += out[i];
  }
  for (float& v : out) {
    v /= sum;
  }
  return out;
}

std::vector<std::string> LoadLabels(const std::string& path) {
  std::ifstream f(path);
  if (!f) {
    throw std::runtime_error("[W15] 标签文件打开失败: " + path);
  }
  std::vector<std::string> labels;
  std::string line;
  while (std::getline(f, line)) {
    // 去掉行尾可能的 \r（Windows 换行兼容）
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    labels.push_back(line);
  }
  if (labels.empty()) {
    throw std::runtime_error("[W15] 标签文件为空: " + path);
  }
  return labels;
}

std::vector<TopK> TopKResults(std::span<const float> logits,
                              const std::vector<std::string>& labels, int k) {
  if (logits.size() != labels.size()) {
    throw std::runtime_error(
        "[W15] logits 维度与标签数不一致: " + std::to_string(logits.size()) +
        " vs " + std::to_string(labels.size()));
  }
  const std::vector<float> probs = Softmax(logits);

  std::vector<int> idx(probs.size());
  std::iota(idx.begin(), idx.end(), 0);
  const int kk = std::min<int>(k, static_cast<int>(probs.size()));
  std::partial_sort(
      idx.begin(), idx.begin() + kk, idx.end(),
      [&probs](int a, int b) { return probs[a] > probs[b]; });

  std::vector<TopK> out;
  out.reserve(kk);
  for (int i = 0; i < kk; ++i) {
    const int id = idx[i];
    out.push_back(TopK{id, probs[id], labels[id]});
  }
  return out;
}

}  // namespace w15
```

> 注：上面已是完整实现（非空占位）。TDD 严格做法是先写空壳跑 FAIL；本步直接给完整实现以避免重复贴码。执行者若想严格 red-green，可先把三个函数体替换为 `return {};` 跑一次 FAIL，再贴回上面完整版。

- [ ] **Step 4: CMake 接线 + 跑测试**

把 lib 的源文件列表加上 `postprocess.cpp`，并加回 `w15_postprocess_test` 段。

Run: `cmake -B build -S . -DCMAKE_CXX_COMPILER=g++-15 -G Ninja && cmake --build build --target w15_postprocess_test -j$(nproc) && ctest --test-dir build -R W15_PostprocessTest --output-on-failure`
Expected: PASS（3 个用例全绿）

- [ ] **Step 5: 格式检查 + Commit**

```bash
find . -maxdepth 3 -regex '.*0[1-4]_.*' \( -name "*.cpp" -o -name "*.hpp" \) | xargs clang-format-21 --dry-run --Werror
git add 02_Inference_Analysis/w15_classify_pipeline/postprocess.hpp \
        02_Inference_Analysis/w15_classify_pipeline/postprocess.cpp \
        02_Inference_Analysis/w15_classify_pipeline/postprocess_test.cpp \
        02_Inference_Analysis/w15_classify_pipeline/CMakeLists.txt
git commit -m "feat(W15): 后处理 softmax/TopK/标签加载 + 单测"
```

---

## Task 4: 资产准备（标签文件 + 测试图）

**Files:**
- Create: `02_Inference_Analysis/w15_classify_pipeline/models/imagenet_classes.txt`
- Create: `02_Inference_Analysis/w15_classify_pipeline/models/test_cat.jpg`

- [ ] **Step 1: 获取 ImageNet-1000 标签文件**

Run:
```bash
mkdir -p 02_Inference_Analysis/w15_classify_pipeline/models
curl -fsSL https://raw.githubusercontent.com/pytorch/hub/master/imagenet_classes.txt \
  -o 02_Inference_Analysis/w15_classify_pipeline/models/imagenet_classes.txt
wc -l 02_Inference_Analysis/w15_classify_pipeline/models/imagenet_classes.txt
```
Expected: 行数为 1000（torchvision/ONNX Model Zoo MobileNetV2 使用的 ImageNet-2012 顺序）。
若离线无法下载：用任一可信来源的 1000 行 ImageNet 标签（每行一个，索引 0→999 顺序），确保与模型输出顺序一致。

- [ ] **Step 2: 准备已知类别测试图**

Run（任选一张类别明确的图，例如猫）：
```bash
curl -fsSL https://raw.githubusercontent.com/pytorch/hub/master/images/dog.jpg \
  -o 02_Inference_Analysis/w15_classify_pipeline/models/test_cat.jpg
file 02_Inference_Analysis/w15_classify_pipeline/models/test_cat.jpg
```
Expected: 输出为 JPEG 图像。（文件名沿用 `test_cat.jpg` 作占位；实际图内容只要类别在 ImageNet 内即可，端到端测试只断言结构性属性，不绑定具体标签。）
若离线：放入任意一张 ImageNet 内类别清晰的 jpg，命名为 `test_cat.jpg`。

- [ ] **Step 3: 验证行数与可解码**

Run: `wc -l 02_Inference_Analysis/w15_classify_pipeline/models/imagenet_classes.txt && file 02_Inference_Analysis/w15_classify_pipeline/models/test_cat.jpg`
Expected: 1000 行 + 合法 JPEG。

- [ ] **Step 4: Commit（小文件可入库）**

```bash
git add 02_Inference_Analysis/w15_classify_pipeline/models/imagenet_classes.txt \
        02_Inference_Analysis/w15_classify_pipeline/models/test_cat.jpg
git commit -m "chore(W15): 加入 ImageNet 标签表与测试图"
```

---

## Task 5: Classifier 编排类（端到端）

**Files:**
- Create: `02_Inference_Analysis/w15_classify_pipeline/classifier.hpp`
- Create: `02_Inference_Analysis/w15_classify_pipeline/classifier.cpp`
- Test: `02_Inference_Analysis/w15_classify_pipeline/classifier_test.cpp`
- Modify: `CMakeLists.txt`（lib 加 `classifier.cpp`，加回 `w15_classifier_test` 段）

- [ ] **Step 1: 写 `classifier.hpp`**

```cpp
// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：W15 Classifier 编排类 —— 串联预处理、W14 推理、后处理，
//           对外提供「图片 -> Top-K 分类结果」一站式接口。
// ============================================================================

#ifndef EDGE_AI_GENESIS_2026_W15_CLASSIFIER_HPP_
#define EDGE_AI_GENESIS_2026_W15_CLASSIFIER_HPP_

#include <string>
#include <vector>

#include "inference_engine.hpp"  // w14::InferenceEngine（经 lib PUBLIC include 可达）
#include "postprocess.hpp"
#include "preprocess.hpp"

namespace w15 {

class Classifier {
 public:
  Classifier(const std::string& model_path, const std::string& labels_path,
             PreprocConfig cfg = {});

  // 从已解码 BGR 图分类。
  [[nodiscard]] std::vector<TopK> Classify(const cv::Mat& bgr, int top_k = 5);

  // 从文件路径分类（内部 cv::imread；读取失败抛 std::runtime_error）。
  [[nodiscard]] std::vector<TopK> Classify(const std::string& image_path,
                                           int top_k = 5);

 private:
  w14::InferenceEngine engine_;
  std::vector<std::string> labels_;
  PreprocConfig cfg_;
};

}  // namespace w15

#endif  // EDGE_AI_GENESIS_2026_W15_CLASSIFIER_HPP_
```

- [ ] **Step 2: 写端到端失败测试 `classifier_test.cpp`**

```cpp
// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：W15 Classifier 端到端测试。依赖模型/标签/测试图，缺失则优雅跳过。
// ============================================================================

#include "classifier.hpp"

#include <gtest/gtest.h>

#include <filesystem>

namespace {

bool AssetsReady() {
  namespace fs = std::filesystem;
  return fs::exists(W15_MODEL_PATH) && fs::exists(W15_LABELS_PATH) &&
         fs::exists(W15_IMAGE_PATH);
}

TEST(W15Classifier, EndToEndTop5StructureAndDeterminism) {
  if (!AssetsReady()) {
    GTEST_SKIP() << "缺少模型/标签/测试图，跳过端到端测试。";
  }
  w15::Classifier clf(W15_MODEL_PATH, W15_LABELS_PATH);

  std::vector<w15::TopK> r1 = clf.Classify(std::string(W15_IMAGE_PATH), 5);
  ASSERT_EQ(r1.size(), 5u);

  // 概率降序、落在 (0,1]、Top-1 不至于过低（真实图应有明确倾向）
  for (size_t i = 0; i + 1 < r1.size(); ++i) {
    EXPECT_GE(r1[i].score, r1[i + 1].score);
  }
  EXPECT_GT(r1[0].score, 0.0f);
  EXPECT_LE(r1[0].score, 1.0f);
  EXPECT_GT(r1[0].score, 0.1f);
  EXPECT_FALSE(r1[0].label.empty());

  // 确定性：同图两次推理 Top-1 索引一致
  std::vector<w15::TopK> r2 = clf.Classify(std::string(W15_IMAGE_PATH), 5);
  ASSERT_EQ(r2.size(), 5u);
  EXPECT_EQ(r1[0].index, r2[0].index);
}

TEST(W15Classifier, MissingImageThrows) {
  if (!std::filesystem::exists(W15_MODEL_PATH) ||
      !std::filesystem::exists(W15_LABELS_PATH)) {
    GTEST_SKIP() << "缺少模型/标签，跳过。";
  }
  w15::Classifier clf(W15_MODEL_PATH, W15_LABELS_PATH);
  EXPECT_THROW(clf.Classify(std::string("/no/such/image.jpg")), std::runtime_error);
}

}  // namespace
```

- [ ] **Step 3: 写实现 `classifier.cpp`**

```cpp
// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：W15 Classifier 实现 —— 详见 classifier.hpp。
// ============================================================================

#include "classifier.hpp"

#include <cstdint>
#include <stdexcept>

#include <opencv2/imgcodecs.hpp>

namespace w15 {

Classifier::Classifier(const std::string& model_path,
                       const std::string& labels_path, PreprocConfig cfg)
    : engine_(model_path), labels_(LoadLabels(labels_path)), cfg_(cfg) {}

std::vector<TopK> Classifier::Classify(const cv::Mat& bgr, int top_k) {
  // 预处理 -> CHW float 张量
  const std::vector<float> input = Preprocess(bgr, cfg_);
  const std::vector<int64_t> shape{1, 3, cfg_.crop, cfg_.crop};

  // 复用 W14 推理（零拷贝喂入）
  std::vector<Ort::Value> outputs = engine_.Run(input, shape);
  const float* logits = outputs[0].GetTensorData<float>();
  const size_t n =
      outputs[0].GetTensorTypeAndShapeInfo().GetElementCount();

  // 后处理 -> Top-K
  return TopKResults(std::span<const float>(logits, n), labels_, top_k);
}

std::vector<TopK> Classifier::Classify(const std::string& image_path,
                                       int top_k) {
  const cv::Mat bgr = cv::imread(image_path, cv::IMREAD_COLOR);
  if (bgr.empty()) {
    throw std::runtime_error("[W15] 图片读取失败: " + image_path);
  }
  return Classify(bgr, top_k);
}

}  // namespace w15
```

- [ ] **Step 4: CMake 接线 + 编译 + 跑测试**

lib 源文件加 `classifier.cpp`，加回 `w15_classifier_test` 段（含三个 compile definitions + BUILD_RPATH）。

Run: `cmake -B build -S . -DCMAKE_CXX_COMPILER=g++-15 -G Ninja && cmake --build build --target w15_classifier_test -j$(nproc) && ctest --test-dir build -R W15_ClassifierTest --output-on-failure`
Expected: PASS（资产齐全时 2 用例跑过；若资产缺失则 SKIP 也算通过，但本任务前置 Task 4 已备齐资产，应实跑 PASS）

- [ ] **Step 5: 格式检查 + Commit**

```bash
find . -maxdepth 3 -regex '.*0[1-4]_.*' \( -name "*.cpp" -o -name "*.hpp" \) | xargs clang-format-21 --dry-run --Werror
git add 02_Inference_Analysis/w15_classify_pipeline/classifier.hpp \
        02_Inference_Analysis/w15_classify_pipeline/classifier.cpp \
        02_Inference_Analysis/w15_classify_pipeline/classifier_test.cpp \
        02_Inference_Analysis/w15_classify_pipeline/CMakeLists.txt
git commit -m "feat(W15): Classifier 编排类串联预处理/推理/后处理 + 端到端单测"
```

---

## Task 6: Demo 可执行 + 真实图眼检

**Files:**
- Create: `02_Inference_Analysis/w15_classify_pipeline/classify_demo.cpp`
- Modify: `CMakeLists.txt`（加回 `w15_classify_demo` 段）

- [ ] **Step 1: 写 `classify_demo.cpp`**

```cpp
// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：W15 分类 demo —— 命令行传入图片路径，打印 Top-5 分类结果。
//           用法：./w15_classify_demo <image_path>
// ============================================================================

#include <cstdlib>
#include <exception>
#include <format>
#include <iostream>
#include <string>

#include "classifier.hpp"

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "用法: " << argv[0] << " <image_path>\n";
    return EXIT_FAILURE;
  }
  try {
    w15::Classifier clf(W15_MODEL_PATH, W15_LABELS_PATH);
    const std::vector<w15::TopK> top = clf.Classify(std::string(argv[1]), 5);
    std::cout << "[W15] 分类结果 Top-5:\n";
    for (size_t i = 0; i < top.size(); ++i) {
      std::cout << std::format("  Top-{}: {} ({:.4f})\n", i + 1, top[i].label,
                               top[i].score);
    }
  } catch (const std::exception& e) {
    std::cerr << "[W15] 错误: " << e.what() << "\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
```

- [ ] **Step 2: 加回 demo 的 CMake 段，编译**

Run: `cmake -B build -S . -DCMAKE_CXX_COMPILER=g++-15 -G Ninja && cmake --build build --target w15_classify_demo -j$(nproc)`
Expected: 编译成功。

- [ ] **Step 3: 跑 demo 眼检真实输出**

Run: `./build/02_Inference_Analysis/w15_classify_pipeline/w15_classify_demo 02_Inference_Analysis/w15_classify_pipeline/models/test_cat.jpg`
Expected: 打印 Top-5，且 Top-1 标签与图片内容**语义吻合**（如狗图出现 dog 相关类别）。这是预处理正确性的最终人工确认——若 Top-1 明显牛头不对马嘴，回查归一化/BGR-RGB/标签对齐（见 spec 第 9 节风险）。

- [ ] **Step 4: Commit**

```bash
find . -maxdepth 3 -regex '.*0[1-4]_.*' \( -name "*.cpp" -o -name "*.hpp" \) | xargs clang-format-21 --dry-run --Werror
git add 02_Inference_Analysis/w15_classify_pipeline/classify_demo.cpp \
        02_Inference_Analysis/w15_classify_pipeline/CMakeLists.txt
git commit -m "feat(W15): 分类 demo 可执行 + 真实图眼检通过"
```

---

## Task 7: 文档同步（notes + tech-debt + 进度）

**Files:**
- Create: `02_Inference_Analysis/w15_classify_pipeline/notes.md`
- Modify: `docs/archive/tech-debt.md`
- Modify: `README.md`、`CLAUDE.md`

- [ ] **Step 1: 写 `notes.md`**（记录闭环结果、预处理参数、踩坑、与 W14 衔接）。至少含：实际 Top-5 输出截图/文本、归一化参数、标签对齐确认、resize/centercrop 选型理由。

- [ ] **Step 2: tech-debt 标 FIXED**

打开 `docs/archive/tech-debt.md`，找到归一化参数未定义的条目（搜索"归一化"/"Q2 W15"），把状态从 OPEN 改为 `[FIXED]`，注明「W15 PreprocConfig 显式定义 ImageNet mean/std，端到端闭环验证」。

- [ ] **Step 3: 更新进度**

- `CLAUDE.md`「当前进度」节：从 W14 更新为「W15（分类推理端到端闭环）已完成」。
- `README.md` 周次进度表：W15 行标记 ✅，简述「真图片→Top-5 分类」。

- [ ] **Step 4: 全量测试 + 格式 + Commit**

Run: `ctest --test-dir build -R W15_ --output-on-failure && find . -maxdepth 3 -regex '.*0[1-4]_.*' \( -name "*.cpp" -o -name "*.hpp" \) | xargs clang-format-21 --dry-run --Werror`
Expected: W15 三个测试全绿（端到端 2 + 预处理 2 + 后处理 3）+ 格式无违规。

```bash
git add 02_Inference_Analysis/w15_classify_pipeline/notes.md docs/archive/tech-debt.md README.md CLAUDE.md
git commit -m "docs(W15): notes 入库 + tech-debt 归一化项 FIXED + 进度同步"
```

---

## 完成定义（Definition of Done）

- [ ] `ctest -R W15_` 全绿：`W15_PreprocessTest`、`W15_PostprocessTest`、`W15_ClassifierTest`
- [ ] `./w15_classify_demo <真实图>` 输出语义正确的 Top-5
- [ ] clang-format-21 检查通过
- [ ] tech-debt「归一化参数未定义」标记 FIXED
- [ ] CLAUDE.md / README.md 进度更新到 W15
- [ ] 所有提交在 `dev-W15-ClassifyPipeline-chunbo` 分支（合入 dev/main 按项目 Branch Policy，需用户许可才入 main）
