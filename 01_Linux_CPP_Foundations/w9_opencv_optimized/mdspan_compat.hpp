// SPDX-License-Identifier: MIT
//
// 文件功能：std::mdspan 兼容层
//   优先使用标准库 <mdspan>（GCC 13+ / MSVC 2022+）；
//   如果标准库没有该头文件（如当前 Debian GCC 15.2.0 打包缺失），
//   则回退到 third_party/mdspan（kokkos P0009 参考实现）并映射到 std::
//   命名空间。

#ifndef W9_MDSPAN_COMPAT_HPP_
#define W9_MDSPAN_COMPAT_HPP_

// 检测标准库是否提供 <mdspan>
#if __has_include(<mdspan>)
#include <mdspan>
#else
// 回退：用宏将 kokkos 参考实现注入 std:: 命名空间
// MDSPAN_IMPL_STANDARD_NAMESPACE 控制 kokkos mdspan 使用的命名空间名称
// 设为 std 后，Kokkos::mdspan 即等同于 std::mdspan
#define MDSPAN_IMPL_STANDARD_NAMESPACE std
#define MDSPAN_IMPL_PROPOSED_NAMESPACE std
#include "mdspan/mdspan.hpp"
#endif

#endif  // W9_MDSPAN_COMPAT_HPP_
