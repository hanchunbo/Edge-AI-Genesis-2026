/**
 * @file main.cpp
 * @brief SafeTensorBuffer 演示程序
 * @note 详细知识点见 notes.md
 */

#include "safe_tensor_buffer.hpp"
#include <vector>

/// 测试 1: RAII - 作用域结束自动释放
void test_basic_raii() {
    std::cout << "\n========== 测试 1: 基本 RAII ==========\n" << std::endl;
    
    {
        SafeTensorBuffer buffer(1024);
        buffer.fill(0xAB);
        std::cout << "缓冲区大小: " << buffer.size() << " 字节" << std::endl;
        std::cout << "首字节值: 0x" << std::hex 
                  << static_cast<int>(buffer.data()[0]) << std::dec << std::endl;
        std::cout << "\n>>> 即将离开作用域 <<<\n" << std::endl;
    }
    
    std::cout << ">>> 资源已自动释放 <<<\n" << std::endl;
}

/// 测试 2: 移动语义 - 零拷贝转移所有权
void test_move_semantics() {
    std::cout << "\n========== 测试 2: 移动语义 ==========\n" << std::endl;
    
    SafeTensorBuffer original(2048);
    original.fill(0x55);
    std::cout << "原始缓冲区 - 大小: " << original.size() 
              << ", 有效: " << (original.valid() ? "是" : "否") << std::endl;
    
    std::cout << "\n>>> 执行 std::move <<<\n" << std::endl;
    SafeTensorBuffer moved(std::move(original));
    
    std::cout << "原始缓冲区 - 大小: " << original.size() 
              << ", 有效: " << (original.valid() ? "是" : "否") << " (已移动)" << std::endl;
    std::cout << "新缓冲区 - 大小: " << moved.size() 
              << ", 有效: " << (moved.valid() ? "是" : "否") << std::endl;
}

/// 测试 3: 异常安全 - 异常时资源自动回收
void test_exception_safety() {
    std::cout << "\n========== 测试 3: 异常安全 ==========\n" << std::endl;
    
    try {
        SafeTensorBuffer buffer1(512);
        SafeTensorBuffer buffer2(1024);
        std::cout << "buffer1, buffer2 创建成功\n" << std::endl;
        
        std::cout << ">>> 模拟异常抛出 <<<\n" << std::endl;
        throw std::runtime_error("模拟错误");
        
    } catch (const std::exception& e) {
        std::cout << ">>> 捕获异常: " << e.what() << std::endl;
        std::cout << ">>> 资源已自动释放 <<<\n" << std::endl;
    }
}

/// 测试 4: shared_ptr 引用计数
void test_shared_ptr_refcount() {
    std::cout << "\n========== 测试 4: shared_ptr 引用计数 ==========\n" << std::endl;
    
    TensorBufferPtr ptr1 = make_tensor_buffer(4096);
    std::cout << "ptr1 创建，引用计数: " << ptr1.use_count() << std::endl;
    
    {
        TensorBufferPtr ptr2 = ptr1;
        TensorBufferPtr ptr3 = ptr1;
        std::cout << "ptr2, ptr3 复制后，引用计数: " << ptr1.use_count() << std::endl;
    }
    
    std::cout << "ptr2, ptr3 销毁后，引用计数: " << ptr1.use_count() << std::endl;
}

/// 演示引用传递 vs 值传递
void use_buffer_by_ref(const TensorBufferPtr& ptr) {
    std::cout << "  引用传递，引用计数: " << ptr.use_count() << std::endl;
}

void use_buffer_by_value(TensorBufferPtr ptr) {
    std::cout << "  值传递，引用计数: " << ptr.use_count() << std::endl;
}

/// 测试 5: shared_ptr 传递方式对比
void test_shared_ptr_passing() {
    std::cout << "\n========== 测试 5: shared_ptr 传递方式 ==========\n" << std::endl;
    
    TensorBufferPtr ptr = make_tensor_buffer(1024);
    std::cout << "初始引用计数: " << ptr.use_count() << std::endl;
    
    std::cout << "调用 by_ref:" << std::endl;
    use_buffer_by_ref(ptr);
    
    std::cout << "调用 by_value:" << std::endl;
    use_buffer_by_value(ptr);
}

/// 测试 6: weak_ptr 使用
void test_weak_ptr() {
    std::cout << "\n========== 测试 6: weak_ptr ==========\n" << std::endl;
    
    TensorBufferWeakPtr weak;
    
    {
        TensorBufferPtr shared = make_tensor_buffer(2048);
        weak = shared;
        std::cout << "weak.expired(): " << (weak.expired() ? "是" : "否") << std::endl;
        
        if (auto locked = weak.lock()) {
            std::cout << "lock() 成功，引用计数: " << locked.use_count() << std::endl;
        }
    }
    
    std::cout << "shared 销毁后 weak.expired(): " << (weak.expired() ? "是" : "否") << std::endl;
}

/// 测试 7: vector 中的移动语义
void test_vector_move() {
    std::cout << "\n========== 测试 7: vector 移动 ==========\n" << std::endl;
    
    std::vector<SafeTensorBuffer> buffers;
    
    std::cout << ">>> emplace_back <<<" << std::endl;
    buffers.emplace_back(1024);
    
    std::cout << "\n>>> push_back + move <<<" << std::endl;
    SafeTensorBuffer temp(2048);
    buffers.push_back(std::move(temp));
    
    std::cout << "\ntemp.valid(): " << (temp.valid() ? "是" : "否") << std::endl;
    std::cout << "vector 大小: " << buffers.size() << std::endl;
}

int main() {
    std::cout << "╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║          W1: SafeTensorBuffer 演示程序                        ║" << std::endl;
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
        std::cout << "║  所有测试完成! 使用 valgrind 验证: 0 bytes leaked            ║" << std::endl;
        std::cout << "╚══════════════════════════════════════════════════════════════╝" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "异常: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
