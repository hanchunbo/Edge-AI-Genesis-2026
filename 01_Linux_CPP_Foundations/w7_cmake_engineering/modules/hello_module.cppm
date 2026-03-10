// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：C++20 具名模块示例 —— 演示 module/export/import 语法与 CMake 配置
// ============================================================================

// global module fragment：在此放需要从传统头文件 #include 的内容
// 原因：C++20 模块不允许在模块声明后直接 #include 系统头文件（破坏模块边界）
module;
#include <format>
#include <string>
#include <string_view>

// ── 模块声明，模块纯视图（module purview）从此开始 ──
// [Legacy C++]: 传统 .hpp / .cpp 分离，消费者 #include 头文件，
//              编译器每次重新解析头文件，大型项目编译极慢
// [Pain Point]: 头文件保护（#pragma once）、宏泄漏、隐式依赖顺序问题
// [Modern C++20]: 具名模块只编译一次，消费者 import 直接使用预编译结果
//                无宏泄漏，接口与实现明确分离
export module w7.hello;

export namespace w7 {

// 导出的函数：消费者 import w7.hello 后即可直接调用
// 注意：未加 export 的函数对消费者不可见（模块内部实现细节）
[[nodiscard]] auto Greet(std::string_view name) -> std::string {
  return std::format("Hello from C++20 named module, {}!", name);
}

// 导出的常量：模块级别的编译期常量，无宏泄漏风险
export constexpr int kModuleVersion = 1;

}  // namespace w7
