// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：C++20 具名模块示例 —— 演示 module/export/import 语法与 CMake 配置
// ============================================================================

// ── 模块声明，模块纯视图（module purview）从此开始 ──
// [Legacy C++]: 传统 .hpp / .cpp 分离，消费者 #include 头文件，
//              编译器每次重新解析头文件，大型项目编译极慢
// [Pain Point]: 头文件保护（#pragma once）、宏泄漏、隐式依赖顺序问题
// [Modern C++20/23]: 具名模块只编译一次，消费者 import 直接使用预编译结果
//                   无宏泄漏，接口与实现明确分离
export module w7.hello;

// 生产级写法：用 import std; 替代 global module fragment 里的 #include
// 原因：global fragment 的 #include 在 GCC 14 中会把 std 符号标记为 @模块名，
//       导致 consumer 再 #include 同一头文件时产生符号重定义冲突。
// import std; 是 C++23 标准库模块，GCC 15+ 稳定支持，编译器只解析一次，
// 且符号不会"泄漏"到 consumer 的全局命名空间，完全规避了上述问题。
// export import std;：把 std 标准库完整地重新导出给所有消费者
// 效果：consumer 只需 import w7.hello; 就能使用 std::cout、std::string 等，
//       完全不需要 #include 任何头文件，彻底规避 import+#include 混用的符号冲突
export import std;

export namespace w7 {

// 导出的函数：消费者 import w7.hello 后即可直接调用
// [[nodiscard]]：防止调用方忽略返回值（边缘 AI 资源约束下的防御性编程）
[[nodiscard]] auto Greet(std::string_view name) -> std::string {
  return std::format("Hello from C++20 named module, {}!", name);
}

// 导出的常量：模块级别的编译期常量，无宏泄漏风险
// 注意：已在 export namespace 块内，不能再加 export，否则 GCC 报"only once"错误
constexpr int kModuleVersion = 2;

}  // namespace w7
