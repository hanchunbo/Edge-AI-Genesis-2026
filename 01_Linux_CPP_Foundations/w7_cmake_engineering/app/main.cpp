// SPDX-License-Identifier: MIT
//
// 文件功能：W7 演示程序 —— 验证 PUBLIC include 自动继承，无需手动指定头文件路径

// 关键演示点：
//   app/CMakeLists.txt 只写了 target_link_libraries(w7_app PRIVATE
//   w7_tensor_utils) 没有任何 target_include_directories！ 但这里直接 #include
//   "tensor_utils.hpp" 可以编译通过， 因为 CMake 自动把 lib/ 的 PUBLIC include
//   路径传递给所有链接该库的目标。
#include "tensor_utils.hpp"

#include <format>
#include <iostream>

int main() {
  // ResNet / MobileNet 典型输入形状：{batch, channels, height, width}
  w7::TensorShape input_shape{1, 3, 224, 224};

  std::cout << std::format("=== W7 TensorShape 演示 ===\n");
  std::cout << std::format("形状: {{1, 3, 224, 224}}\n");
  std::cout << std::format("Rank（维度数）: {}\n", input_shape.Rank());
  std::cout << std::format("NumElements（总元素数）: {}\n",
                           input_shape.NumElements());
  std::cout << std::format("C 维度（index=1）: {}\n", input_shape[1]);

  // 广播兼容性检查：{1, 224, 224} 与 {3, 224, 224} 可广播
  w7::TensorShape bias_shape{1, 224, 224};
  w7::TensorShape feat_shape{3, 224, 224};
  std::cout << std::format(
      "\n广播检查 {{1,224,224}} vs {{3,224,224}}: {}\n",
      w7::IsBroadcastable(bias_shape, feat_shape) ? "兼容" : "不兼容");

  // 验证 W7_TENSOR_UTILS_AVAILABLE 宏已通过 INTERFACE 传入
#ifdef W7_TENSOR_UTILS_AVAILABLE
  std::cout << std::format(
      "W7_TENSOR_UTILS_AVAILABLE={} (通过 INTERFACE 宏注入)\n",
      W7_TENSOR_UTILS_AVAILABLE);
#endif

  return 0;
}
