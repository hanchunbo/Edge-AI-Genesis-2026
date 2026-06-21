// SPDX-License-Identifier: MIT
//
// 文件功能：C++20 具名模块消费端 —— 演示 import 替代 #include 的完整调用链
//
// [Legacy C++]: #include "tensor_utils.hpp" 将头文件内容直接粘贴进翻译单元，
//              编译器每次都要重新解析整个头文件，大型项目中重复解析数百次。
// [Pain Point]: 宏定义全局泄漏、头文件顺序敏感、#pragma once 无法跨目录去重。
// [Modern C++20/23]: import 模块名; 直接使用编译好的 BMI，
//                   不再重复解析，无宏泄漏，接口与实现边界清晰。

// 生产级写法：consumer 不写任何 #include，所有依赖通过 import 获取
// w7.hello 模块内部做了 export import std;，所以 consumer 导入 w7.hello 后
// 自动获得 std::cout、std::string 等全部标准库符号
import w7.hello;

int main() {
  // 调用从 w7.hello 模块导出的 Greet 函数
  // 若改为 #include 头文件，此处代码完全相同，但编译代价截然不同
  std::cout << w7::Greet("W7 CMake") << "\n";

  // 访问模块导出的编译期常量，验证模块接口完整性
  std::cout << "模块版本：" << w7::kModuleVersion << "\n";

  return 0;
}
