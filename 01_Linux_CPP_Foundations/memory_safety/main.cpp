/**
 * @file main.cpp
 * @brief SafeTensorBuffer 演示程序 - W1 实战作业
 * @author Edge-AI-Genesis-2026 Team
 * @date 2026-01-11
 * 
 * 本程序演示以下核心概念：
 * 1. RAII 机制的基本使用
 * 2. 移动语义的正确应用
 * 3. 异常场景下的资源自动回收
 * 4. shared_ptr 引用计数与 weak_ptr 使用
 */

#include "safe_tensor_buffer.hpp"
#include <vector>
#include <functional>

// =============================================================================
//                              测试用例
// =============================================================================

/**
 * @brief 测试 1：基本 RAII - 作用域结束自动释放
 * 
 * 【知识点】
 * 当对象离开作用域时，析构函数自动被调用，资源自动释放。
 * 无需手动调用 delete，不会忘记释放，不会内存泄漏。
 */
void test_basic_raii() {
    std::cout << "\n========== 测试 1: 基本 RAII ==========\n" << std::endl;
    
    {
        // 在内部作用域创建对象
        SafeTensorBuffer buffer(1024);  // 分配 1KB
        buffer.fill(0xAB);
        
        std::cout << "缓冲区大小: " << buffer.size() << " 字节" << std::endl;
        std::cout << "首字节值: 0x" << std::hex 
                  << static_cast<int>(buffer.data()[0]) << std::dec << std::endl;
        
        // 作用域即将结束...
        std::cout << "\n>>> 即将离开作用域，析构函数将被自动调用 <<<\n" << std::endl;
    }
    // ← 离开作用域，buffer 自动析构，内存自动释放
    
    std::cout << ">>> 已离开作用域，资源已释放 <<<\n" << std::endl;
}

/**
 * @brief 测试 2：移动语义 - 零拷贝转移所有权
 * 
 * 【知识点】
 * std::move 并不移动任何东西！它只是将左值转换为右值引用。
 * 真正的移动发生在移动构造函数或移动赋值运算符中。
 * 
 * 【性能优势】
 * 移动操作只涉及指针赋值，时间复杂度 O(1)。
 * 拷贝操作需要复制全部数据，时间复杂度 O(n)。
 */
void test_move_semantics() {
    std::cout << "\n========== 测试 2: 移动语义 ==========\n" << std::endl;
    
    // 创建原始缓冲区
    SafeTensorBuffer original(2048);  // 2KB
    original.fill(0x55);
    
    std::cout << "原始缓冲区 - 大小: " << original.size() 
              << ", 有效: " << (original.valid() ? "是" : "否") << std::endl;
    
    // 使用 std::move 转移所有权
    // 【重要】move 后，original 变为"空壳"，不再持有资源
    std::cout << "\n>>> 执行 std::move <<<\n" << std::endl;
    SafeTensorBuffer moved(std::move(original));
    
    std::cout << "原始缓冲区 - 大小: " << original.size() 
              << ", 有效: " << (original.valid() ? "是" : "否") << " (已被移动)" << std::endl;
    std::cout << "新缓冲区   - 大小: " << moved.size() 
              << ", 有效: " << (moved.valid() ? "是" : "否") << std::endl;
    
    // 验证数据完整性
    std::cout << "新缓冲区首字节: 0x" << std::hex 
              << static_cast<int>(moved.data()[0]) << std::dec << std::endl;
}

/**
 * @brief 测试 3：异常安全 - 异常发生时资源自动回收
 * 
 * 【知识点】
 * 当异常发生时，栈展开（Stack Unwinding）会调用所有已构造对象的析构函数。
 * 这就是 RAII 保证异常安全的核心机制。
 * 
 * 【对比手动管理】
 * 如果使用 new/delete：
 *   T* p = new T();
 *   risky_operation();  // 如果这里抛异常，p 就泄漏了！
 *   delete p;
 * 
 * 使用 RAII：
 *   SafeWrapper wrapper;  // RAII 对象
 *   risky_operation();    // 即使抛异常，wrapper 也会被析构
 */
void test_exception_safety() {
    std::cout << "\n========== 测试 3: 异常安全 ==========\n" << std::endl;
    
    try {
        // 创建一个缓冲区
        SafeTensorBuffer buffer1(512);
        std::cout << "buffer1 创建成功" << std::endl;
        
        // 创建第二个缓冲区
        SafeTensorBuffer buffer2(1024);
        std::cout << "buffer2 创建成功" << std::endl;
        
        // 模拟一个可能抛出异常的操作
        std::cout << "\n>>> 模拟异常抛出 <<<\n" << std::endl;
        throw std::runtime_error("模拟的运行时错误!");
        
        // 这行代码不会执行
        std::cout << "这行不会被执行" << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "\n>>> 捕获异常: " << e.what() << " <<<" << std::endl;
        std::cout << ">>> 注意：上面的析构函数已被自动调用，资源已释放 <<<\n" << std::endl;
    }
}

/**
 * @brief 测试 4：shared_ptr 引用计数
 * 
 * 【知识点】
 * shared_ptr 使用引用计数来管理对象生命周期：
 * - 每次拷贝，引用计数 +1
 * - 每次销毁，引用计数 -1
 * - 引用计数归零时，删除对象
 * 
 * 【性能注意】
 * 引用计数的修改是原子操作（线程安全），但有开销。
 * 在高频场景下，应尽量使用 const 引用传递 shared_ptr。
 */
void test_shared_ptr_refcount() {
    std::cout << "\n========== 测试 4: shared_ptr 引用计数 ==========\n" << std::endl;
    
    // 创建 shared_ptr
    TensorBufferPtr ptr1 = make_tensor_buffer(4096);
    std::cout << "ptr1 创建，引用计数: " << ptr1.use_count() << std::endl;
    
    {
        // 拷贝 shared_ptr
        TensorBufferPtr ptr2 = ptr1;
        std::cout << "ptr2 = ptr1，引用计数: " << ptr1.use_count() << std::endl;
        
        TensorBufferPtr ptr3 = ptr1;
        std::cout << "ptr3 = ptr1，引用计数: " << ptr1.use_count() << std::endl;
        
        std::cout << "\n>>> ptr2, ptr3 即将离开作用域 <<<\n" << std::endl;
    }
    // ptr2, ptr3 销毁，引用计数 -2
    
    std::cout << "ptr2, ptr3 已销毁，引用计数: " << ptr1.use_count() << std::endl;
    
    std::cout << "\n>>> ptr1 即将离开作用域，引用计数归零，对象将被删除 <<<\n" << std::endl;
}

/**
 * @brief 演示 shared_ptr 通过引用传递（避免引用计数开销）
 * 
 * 【最佳实践】
 * - 如果函数只是"使用"对象，传 const shared_ptr<T>& 或 const T&
 * - 如果函数需要"持有"对象（延长生命周期），传 shared_ptr<T>
 */
void use_buffer_by_ref(const TensorBufferPtr& ptr) {
    // 使用 const 引用传递，不增加引用计数
    std::cout << "  [use_buffer_by_ref] 引用计数（未增加）: " << ptr.use_count() << std::endl;
}

void use_buffer_by_value(TensorBufferPtr ptr) {
    // 值传递，引用计数 +1
    std::cout << "  [use_buffer_by_value] 引用计数（+1）: " << ptr.use_count() << std::endl;
}

void test_shared_ptr_passing() {
    std::cout << "\n========== 测试 5: shared_ptr 传递方式对比 ==========\n" << std::endl;
    
    TensorBufferPtr ptr = make_tensor_buffer(1024);
    std::cout << "初始引用计数: " << ptr.use_count() << std::endl;
    
    std::cout << "\n调用 use_buffer_by_ref（引用传递）:" << std::endl;
    use_buffer_by_ref(ptr);
    std::cout << "调用后引用计数: " << ptr.use_count() << std::endl;
    
    std::cout << "\n调用 use_buffer_by_value（值传递）:" << std::endl;
    use_buffer_by_value(ptr);
    std::cout << "调用后引用计数: " << ptr.use_count() << std::endl;
}

/**
 * @brief 测试 6：weak_ptr 解决循环引用
 * 
 * 【问题场景】
 * 如果 A 持有 B 的 shared_ptr，B 也持有 A 的 shared_ptr：
 * A.ref_count = 1 (被 B 引用)
 * B.ref_count = 1 (被 A 引用)
 * 即使外部没有引用 A 和 B，它们的引用计数也不会归零，导致内存泄漏。
 * 
 * 【解决方案】
 * 其中一方使用 weak_ptr，weak_ptr 不增加引用计数。
 */
void test_weak_ptr() {
    std::cout << "\n========== 测试 6: weak_ptr 使用 ==========\n" << std::endl;
    
    TensorBufferWeakPtr weak;
    
    {
        TensorBufferPtr shared = make_tensor_buffer(2048);
        std::cout << "shared 创建，引用计数: " << shared.use_count() << std::endl;
        
        // 从 shared_ptr 创建 weak_ptr
        weak = shared;
        std::cout << "weak = shared，引用计数（不变）: " << shared.use_count() << std::endl;
        std::cout << "weak.expired(): " << (weak.expired() ? "是" : "否") << std::endl;
        
        // 使用 lock() 获取 shared_ptr
        if (TensorBufferPtr locked = weak.lock()) {
            std::cout << "weak.lock() 成功，引用计数: " << locked.use_count() << std::endl;
        }
        
        std::cout << "\n>>> shared 即将离开作用域 <<<\n" << std::endl;
    }
    // shared 销毁，引用计数归零，对象被删除
    
    std::cout << "shared 已销毁" << std::endl;
    std::cout << "weak.expired(): " << (weak.expired() ? "是" : "否") << std::endl;
    
    // 尝试 lock()
    if (TensorBufferPtr locked = weak.lock()) {
        std::cout << "weak.lock() 成功" << std::endl;
    } else {
        std::cout << "weak.lock() 失败 - 对象已被销毁" << std::endl;
    }
}

/**
 * @brief 测试 7：在 vector 中使用移动语义
 * 
 * 【知识点】
 * std::vector 扩容时会移动/拷贝元素。
 * 如果移动构造是 noexcept 的，vector 优先使用移动。
 */
void test_vector_move() {
    std::cout << "\n========== 测试 7: vector 中的移动语义 ==========\n" << std::endl;
    
    std::vector<SafeTensorBuffer> buffers;
    
    // 使用 emplace_back 直接在容器中构造（避免额外移动）
    std::cout << ">>> emplace_back 构造 <<<\n" << std::endl;
    buffers.emplace_back(1024);
    
    std::cout << "\n>>> push_back + std::move <<<\n" << std::endl;
    SafeTensorBuffer temp(2048);
    buffers.push_back(std::move(temp));
    
    std::cout << "\ntemp.valid() after move: " << (temp.valid() ? "是" : "否") << std::endl;
    std::cout << "vector 大小: " << buffers.size() << std::endl;
}

// =============================================================================
//                              主函数
// =============================================================================

int main() {
    std::cout << "╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║     W1 实战作业：SafeTensorBuffer - RAII 与智能指针演示       ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    
    try {
        test_basic_raii();
        test_move_semantics();
        test_exception_safety();
        test_shared_ptr_refcount();
        test_shared_ptr_passing();
        test_weak_ptr();
        test_vector_move();
        
        std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
        std::cout << "║                    所有测试执行完成!                          ║" << std::endl;
        std::cout << "║     使用 valgrind --leak-check=full ./safe_tensor_demo       ║" << std::endl;
        std::cout << "║     验证内存泄漏情况                                          ║" << std::endl;
        std::cout << "╚══════════════════════════════════════════════════════════════╝" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "程序异常: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
