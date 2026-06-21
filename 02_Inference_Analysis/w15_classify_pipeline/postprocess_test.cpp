// SPDX-License-Identifier: MIT
//
// 文件功能：W15 后处理单元测试（纯函数，无需模型）。

#include "postprocess.hpp"

#include <cstdio>
#include <fstream>
#include <gtest/gtest.h>
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
  EXPECT_GT(top[0].score, top[1].score);  // 降序
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
