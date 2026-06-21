// SPDX-License-Identifier: MIT
//
// 文件功能：fixed_lab 公开接口声明（供 GTest 引用）

#ifndef LINUX_CPP_FOUNDATIONS_W11_DEBUGGING_FIXED_LAB_HPP_
#define LINUX_CPP_FOUNDATIONS_W11_DEBUGGING_FIXED_LAB_HPP_

#include <vector>

namespace fixed {

// Fix 1：RAII 内存管理，frame_count 次分配，Valgrind 无泄漏
void SimulateFrameProcessing(int frame_count);

// Fix 2：scoped_lock 并发传输，无死锁
void TransferData();

// Fix 3：O(n log n) 搜索，返回 targets 中在 data 里存在的元素
[[nodiscard]] std::vector<int> FindTarget(const std::vector<int>& data,
                                          const std::vector<int>& targets);

}  // namespace fixed

#endif  // LINUX_CPP_FOUNDATIONS_W11_DEBUGGING_FIXED_LAB_HPP_
