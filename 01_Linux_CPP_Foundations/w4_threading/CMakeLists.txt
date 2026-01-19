cmake_minimum_required(VERSION 3.16)
project(w4_threading LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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
