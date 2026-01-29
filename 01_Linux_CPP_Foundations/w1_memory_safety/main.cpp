// Copyright 2026 Edge-AI-Genesis-2026
//
// ============================================================================
// 文件功能：SafeTensorBuffer 演示程序 - W1 实战作业
// ============================================================================
//
// 本程序演示以下核心概念：
// 1. RAII 机制的基本使用
// 2. 移动语义的正确应用
// 3. 异常场景下的资源自动回收
// 4. shared_ptr 引用计数与 weak_ptr 使用
// 5. C++23 std::expected 错误处理
//
// ============================================================================

#include "safe_tensor_buffer.hpp"

#include <format>
#include <functional>
#include <vector>

// ============================================================================
// 测试用例 - 使用 PascalCase 命名 (Google Style)
// ============================================================================

/**
 * @brief 测试 1：基本 RAII - 作用域结束自动释放
 */
// [Legacy C++11/17]: 手动 new/delete，容易忘记释放
// [Pain Point]: 异常发生时资源泄漏，多处 new 难以追踪
// [Modern C++20/23]: RAII 对象自动管理资源，离开作用域自动释放
void TestBasicRaii() {
  std::cout << std::format("\n========== 测试 1: 基本 RAII ==========\n\n");

  {
    // 在内部作用域创建对象
    SafeTensorBuffer buffer(1024);  // 分配 1KB
    buffer.fill(0xAB);

    std::cout << std::format("缓冲区大小: {} 字节\n", buffer.size());
    std::cout << std::format("首字节值: 0x{:02X}\n", buffer.data()[0]);

    // 作用域即将结束...
    std::cout << std::format("\n>>> 即将离开作用域，析构函数将被自动调用 <<<\n\n");
  }
  // ← 离开作用域，buffer 自动析构，内存自动释放

  std::cout << std::format(">>> 已离开作用域，资源已释放 <<<\n\n");
}

/**
 * @brief 测试 2：移动语义 - 零拷贝转移所有权
 */
// [Legacy C++11/17]: 默认拷贝导致深拷贝，大缓冲区开销巨大
// [Pain Point]: 4K 图像拷贝约 24MB，FPS 严重下降
// [Modern C++20/23]: std::move 转移所有权，O(1) 指针赋值
void TestMoveSemantics() {
  std::cout << std::format("\n========== 测试 2: 移动语义 ==========\n\n");

  // 创建原始缓冲区
  SafeTensorBuffer original(2048);  // 2KB
  original.fill(0x55);

  std::cout << std::format("原始缓冲区 - 大小: {}, 有效: {}\n",
                           original.size(),
                           original.valid() ? "是" : "否");

  // 使用 std::move 转移所有权
  std::cout << std::format("\n>>> 执行 std::move <<<\n\n");
  SafeTensorBuffer moved(std::move(original));

  std::cout << std::format("原始缓冲区 - 大小: {}, 有效: {} (已被移动)\n",
                           original.size(),
                           original.valid() ? "是" : "否");
  std::cout << std::format("新缓冲区   - 大小: {}, 有效: {}\n",
                           moved.size(),
                           moved.valid() ? "是" : "否");

  // 验证数据完整性
  std::cout << std::format("新缓冲区首字节: 0x{:02X}\n", moved.data()[0]);
}

/**
 * @brief 测试 3：异常安全 - 异常发生时资源自动回收
 */
// [Legacy C++11/17]: try/catch 中 new 的资源需手动 delete
// [Pain Point]: 异常路径遗漏 delete 导致泄漏
// [Modern C++20/23]: RAII 栈展开自动调用析构函数
void TestExceptionSafety() {
  std::cout << std::format("\n========== 测试 3: 异常安全 ==========\n\n");

  try {
    SafeTensorBuffer buffer1(512);
    std::cout << std::format("buffer1 创建成功\n");

    SafeTensorBuffer buffer2(1024);
    std::cout << std::format("buffer2 创建成功\n");

    std::cout << std::format("\n>>> 模拟异常抛出 <<<\n\n");
    throw std::runtime_error("模拟的运行时错误!");

  } catch (const std::exception& e) {
    std::cout << std::format("\n>>> 捕获异常: {} <<<\n", e.what());
    std::cout << std::format(">>> 注意：上面的析构函数已被自动调用 <<<\n\n");
  }
}

/**
 * @brief 测试 4：shared_ptr 引用计数
 */
// [Legacy C++11/17]: 手动计数引用，容易出错
// [Pain Point]: 循环引用导致内存泄漏
// [Modern C++20/23]: shared_ptr 自动引用计数，weak_ptr 打破循环
void TestSharedPtrRefcount() {
  std::cout << std::format(
      "\n========== 测试 4: shared_ptr 引用计数 ==========\n\n");

  TensorBufferPtr ptr1 = MakeTensorBuffer(4096);
  std::cout << std::format("ptr1 创建，引用计数: {}\n", ptr1.use_count());

  {
    TensorBufferPtr ptr2 = ptr1;
    std::cout << std::format("ptr2 = ptr1，引用计数: {}\n", ptr1.use_count());

    TensorBufferPtr ptr3 = ptr1;
    std::cout << std::format("ptr3 = ptr1，引用计数: {}\n", ptr1.use_count());

    std::cout << std::format("\n>>> ptr2, ptr3 即将离开作用域 <<<\n\n");
  }

  std::cout << std::format("ptr2, ptr3 已销毁，引用计数: {}\n",
                           ptr1.use_count());
  std::cout << std::format(
      "\n>>> ptr1 即将离开作用域，引用计数归零，对象将被删除 <<<\n\n");
}

/**
 * @brief 演示 shared_ptr 引用传递（避免引用计数开销）
 */
void UseBufferByRef(const TensorBufferPtr& ptr) {
  std::cout << std::format("  [UseBufferByRef] 引用计数（未增加）: {}\n",
                           ptr.use_count());
}

void UseBufferByValue(TensorBufferPtr ptr) {
  std::cout << std::format("  [UseBufferByValue] 引用计数（+1）: {}\n",
                           ptr.use_count());
}

/**
 * @brief 测试 5：shared_ptr 传递方式对比
 */
void TestSharedPtrPassing() {
  std::cout << std::format(
      "\n========== 测试 5: shared_ptr 传递方式对比 ==========\n\n");

  TensorBufferPtr ptr = MakeTensorBuffer(1024);
  std::cout << std::format("初始引用计数: {}\n", ptr.use_count());

  std::cout << std::format("\n调用 UseBufferByRef（引用传递）:\n");
  UseBufferByRef(ptr);
  std::cout << std::format("调用后引用计数: {}\n", ptr.use_count());

  std::cout << std::format("\n调用 UseBufferByValue（值传递）:\n");
  UseBufferByValue(ptr);
  std::cout << std::format("调用后引用计数: {}\n", ptr.use_count());
}

/**
 * @brief 测试 6：weak_ptr 解决循环引用
 */
void TestWeakPtr() {
  std::cout << std::format("\n========== 测试 6: weak_ptr 使用 ==========\n\n");

  TensorBufferWeakPtr weak;

  {
    TensorBufferPtr shared = MakeTensorBuffer(2048);
    std::cout << std::format("shared 创建，引用计数: {}\n", shared.use_count());

    weak = shared;
    std::cout << std::format("weak = shared，引用计数（不变）: {}\n",
                             shared.use_count());
    std::cout << std::format("weak.expired(): {}\n",
                             weak.expired() ? "是" : "否");

    if (TensorBufferPtr locked = weak.lock()) {
      std::cout << std::format("weak.lock() 成功，引用计数: {}\n",
                               locked.use_count());
    }

    std::cout << std::format("\n>>> shared 即将离开作用域 <<<\n\n");
  }

  std::cout << std::format("shared 已销毁\n");
  std::cout << std::format("weak.expired(): {}\n",
                           weak.expired() ? "是" : "否");

  if (TensorBufferPtr locked = weak.lock()) {
    std::cout << std::format("weak.lock() 成功\n");
  } else {
    std::cout << std::format("weak.lock() 失败 - 对象已被销毁\n");
  }
}

/**
 * @brief 测试 7：在 vector 中使用移动语义
 */
void TestVectorMove() {
  std::cout << std::format(
      "\n========== 测试 7: vector 中的移动语义 ==========\n\n");

  std::vector<SafeTensorBuffer> buffers;

  std::cout << std::format(">>> emplace_back 构造 <<<\n\n");
  buffers.emplace_back(1024);

  std::cout << std::format("\n>>> push_back + std::move <<<\n\n");
  SafeTensorBuffer temp(2048);
  buffers.push_back(std::move(temp));

  std::cout << std::format("\ntemp.valid() after move: {}\n",
                           temp.valid() ? "是" : "否");
  std::cout << std::format("vector 大小: {}\n", buffers.size());
}

/**
 * @brief 测试 8：C++23 std::expected 错误处理
 */
// [Legacy C++11/17]: 异常或错误码处理
// [Pain Point]: 异常开销；错误码容易被忽略
// [Modern C++20/23]: std::expected 编译期强制处理，零开销
void TestExpectedErrorHandling() {
  std::cout << std::format(
      "\n========== 测试 8: C++23 std::expected ==========\n\n");

  // 正常创建
  auto result1 = SafeTensorBuffer::Create(1024);
  if (result1.has_value()) {
    std::cout << std::format("Create(1024) 成功，大小: {}\n",
                             result1->size());
  }

  // 错误情况：size = 0
  auto result2 = SafeTensorBuffer::Create(0);
  if (!result2.has_value()) {
    std::cout << std::format("Create(0) 失败，错误: {}\n",
                             TensorErrorToString(result2.error()));
  }

  // 使用 span 视图
  if (result1.has_value()) {
    auto view = result1->View();
    std::cout << std::format("span 视图大小: {}\n", view.size());

    // 安全索引访问
    auto elem = result1->At(0);
    if (elem.has_value()) {
      elem->get() = 0x42;
      std::cout << std::format("At(0) = 0x{:02X}\n", result1->data()[0]);
    }

    // 越界访问
    auto bad = result1->At(9999);
    if (!bad.has_value()) {
      std::cout << std::format("At(9999) 错误: {}\n",
                               TensorErrorToString(bad.error()));
    }
  }
}

// ============================================================================
// 主函数
// ============================================================================
int main() {
  std::cout << std::format(
      "╔══════════════════════════════════════════════════════════════╗\n"
      "║     W1 实战作业：SafeTensorBuffer - RAII 与智能指针演示       ║\n"
      "╚══════════════════════════════════════════════════════════════╝\n");

  try {
    TestBasicRaii();
    TestMoveSemantics();
    TestExceptionSafety();
    TestSharedPtrRefcount();
    TestSharedPtrPassing();
    TestWeakPtr();
    TestVectorMove();
    TestExpectedErrorHandling();

    std::cout << std::format(
        "\n╔══════════════════════════════════════════════════════════════╗\n"
        "║                    所有测试执行完成!                          ║\n"
        "║     使用 valgrind --leak-check=full ./safe_tensor_demo       ║\n"
        "║     验证内存泄漏情况                                          ║\n"
        "╚══════════════════════════════════════════════════════════════╝\n");

  } catch (const std::exception& e) {
    std::cerr << std::format("程序异常: {}\n", e.what());
    return 1;
  }

  return 0;
}
