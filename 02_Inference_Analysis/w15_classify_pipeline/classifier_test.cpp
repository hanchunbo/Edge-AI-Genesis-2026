// SPDX-License-Identifier: MIT
//
// 文件功能：W15 Classifier 端到端测试。依赖模型/标签/测试图，缺失则优雅跳过。

#include "classifier.hpp"

#include <filesystem>
#include <gtest/gtest.h>

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
  EXPECT_THROW(clf.Classify(std::string("/no/such/image.jpg")),
               std::runtime_error);
}

}  // namespace
