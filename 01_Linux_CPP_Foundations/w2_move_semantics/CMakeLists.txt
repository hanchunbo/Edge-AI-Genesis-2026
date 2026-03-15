# Copyright 2026 Edge-AI-Genesis
# W2: Move Semantics - Benchmark (C++20)

add_executable(benchmark_move_vs_copy benchmark_move_vs_copy.cpp)
set_target_properties(benchmark_move_vs_copy PROPERTIES CXX_STANDARD 20)
target_compile_options(benchmark_move_vs_copy PRIVATE -O2)

if(BUILD_TESTING)
  add_executable(w2_move_semantics_test w2_move_semantics_test.cpp)
  set_target_properties(w2_move_semantics_test PROPERTIES CXX_STANDARD 20)
  target_include_directories(w2_move_semantics_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(w2_move_semantics_test PRIVATE GTest::gtest_main)
  add_test(NAME W2_MoveSemanticsTest COMMAND w2_move_semantics_test)
endif()

# W8 覆盖率支持：cmake -DW8_COVERAGE=ON 时注入 gcov 插桩
# --coverage 等价于 -fprofile-arcs -ftest-coverage；-O0 防止插桩被优化掉
if(W8_COVERAGE)
  target_compile_options(w2_move_semantics_test PRIVATE --coverage -O0 -g)
  target_link_options(w2_move_semantics_test PRIVATE --coverage)
endif()
