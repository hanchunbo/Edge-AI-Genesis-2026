# Copyright 2026 Edge-AI-Genesis
# W6: mmap + std::span 零拷贝文件加载器 (C++23)

add_executable(w6_mmap_loader_test mmap_loader_test.cpp)

# MmapLoader 使用 std::expected（C++23），局部覆盖根目录的 C++20 设置
set_target_properties(w6_mmap_loader_test PROPERTIES CXX_STANDARD 23)

target_include_directories(w6_mmap_loader_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(w6_mmap_loader_test PRIVATE GTest::gtest_main)

add_test(NAME W6_MmapLoaderTest COMMAND w6_mmap_loader_test)

# Benchmark：mmap vs ifstream 顺序读取 128MB 文件吞吐量对比
add_executable(w6_mmap_benchmark mmap_benchmark.cpp)
set_target_properties(w6_mmap_benchmark PROPERTIES CXX_STANDARD 23)
target_include_directories(w6_mmap_benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# 注册为 CTest，CI 可验证 benchmark 能正常编译并运行（不检查性能数值）
add_test(NAME W6_MmapBenchmark COMMAND w6_mmap_benchmark)
