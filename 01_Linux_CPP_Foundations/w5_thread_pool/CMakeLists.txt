cmake_minimum_required(VERSION 3.16)
project(w5_thread_pool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 添加线程库支持
find_package(Threads REQUIRED)

# 主测试程序
add_executable(thread_pool_test thread_pool_test.cpp)
target_link_libraries(thread_pool_test PRIVATE Threads::Threads)

# 启用编译器警告
target_compile_options(thread_pool_test PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

# ThreadSanitizer 构建选项
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)
if(ENABLE_TSAN)
  target_compile_options(thread_pool_test PRIVATE -fsanitize=thread -g)
  target_link_options(thread_pool_test PRIVATE -fsanitize=thread)
endif()
