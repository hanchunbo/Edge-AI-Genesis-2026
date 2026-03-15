# Copyright 2026 Edge-AI-Genesis
# W6: mmap + std::span 零拷贝文件加载器 (C++23)

# Benchmark：mmap vs ifstream 顺序读取 128MB 文件吞吐量对比（不依赖 GTest）
add_executable(w6_mmap_benchmark mmap_benchmark.cpp)
set_target_properties(w6_mmap_benchmark PROPERTIES CXX_STANDARD 23)
target_include_directories(w6_mmap_benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(BUILD_TESTING)
  add_executable(w6_mmap_loader_test mmap_loader_test.cpp)
  # MmapLoader 使用 std::expected（C++23），局部覆盖根目录的 C++20 设置
  set_target_properties(w6_mmap_loader_test PROPERTIES CXX_STANDARD 23)
  target_include_directories(w6_mmap_loader_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(w6_mmap_loader_test PRIVATE GTest::gtest_main)
  add_test(NAME W6_MmapLoaderTest COMMAND w6_mmap_loader_test)

  # 注册为 CTest，CI 可验证 benchmark 能正常编译并运行（不检查性能数值）
  add_test(NAME W6_MmapBenchmark COMMAND w6_mmap_benchmark)
endif()

# W8 覆盖率支持：cmake -DW8_COVERAGE=ON 时注入 gcov 插桩
# --coverage 等价于 -fprofile-arcs -ftest-coverage；-O0 防止插桩被优化掉
# 只给测试目标插桩，不给 benchmark 插桩（benchmark 需要 -O2 优化，插桩会干扰结果）
if(W8_COVERAGE)
  target_compile_options(w6_mmap_loader_test PRIVATE --coverage -O0 -g)
  target_link_options(w6_mmap_loader_test PRIVATE --coverage)
endif()
