# Copyright 2026 Edge-AI-Genesis
# W2: Move Semantics - Benchmark (C++20)

add_executable(benchmark_move_vs_copy benchmark_move_vs_copy.cpp)
set_target_properties(benchmark_move_vs_copy PROPERTIES CXX_STANDARD 20)
target_compile_options(benchmark_move_vs_copy PRIVATE -O2)

# GTest 单元测试
add_executable(w2_move_semantics_test w2_move_semantics_test.cpp)
set_target_properties(w2_move_semantics_test PROPERTIES CXX_STANDARD 20)
target_include_directories(w2_move_semantics_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(w2_move_semantics_test PRIVATE GTest::gtest_main)
add_test(NAME W2_MoveSemanticsTest COMMAND w2_move_semantics_test)
