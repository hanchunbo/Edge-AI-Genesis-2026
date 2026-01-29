cmake_minimum_required(VERSION 3.16)
project(w4_threading LANGUAGES CXX)

# ============================================================================
# C++20 配置
# ============================================================================
# 本项目使用 C++20 特性：
# - std::counting_semaphore (轻量级同步原语)
# - std::atomic::wait/notify (无锁等待)
# - std::jthread & std::stop_token (RAII 线程)
# - std::format (类型安全格式化)
# ============================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 强制使用 GCC 13
set(CMAKE_CXX_COMPILER "g++-13")

# 添加线程库支持
find_package(Threads REQUIRED)

# 主程序
add_executable(producer_consumer producer_consumer.cpp)
target_link_libraries(producer_consumer PRIVATE Threads::Threads)

# 启用编译器警告
target_compile_options(producer_consumer PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

# ThreadSanitizer 构建选项（用于检测数据竞争）
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)
if(ENABLE_TSAN)
  target_compile_options(producer_consumer PRIVATE -fsanitize=thread -g)
  target_link_options(producer_consumer PRIVATE -fsanitize=thread)
endif()
