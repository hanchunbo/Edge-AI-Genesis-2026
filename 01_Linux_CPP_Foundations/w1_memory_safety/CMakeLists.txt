# Copyright 2026 Edge-AI-Genesis
# W1: Memory Safety - RAII + SafeTensorBuffer (C++23)

add_executable(safe_tensor_demo main.cpp)
set_target_properties(safe_tensor_demo PROPERTIES CXX_STANDARD 23)

if(BUILD_TESTING)
  add_executable(w1_memory_safety_test w1_memory_safety_test.cpp)
  set_target_properties(w1_memory_safety_test PROPERTIES CXX_STANDARD 23)
  target_include_directories(w1_memory_safety_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(w1_memory_safety_test PRIVATE GTest::gtest_main)
  add_test(NAME W1_MemorySafetyTest COMMAND w1_memory_safety_test)
endif()

# W8 覆盖率支持：cmake -DW8_COVERAGE=ON 时注入 gcov 插桩
# --coverage 等价于 -fprofile-arcs -ftest-coverage；-O0 防止插桩被优化掉
if(W8_COVERAGE)
  target_compile_options(w1_memory_safety_test PRIVATE --coverage -O0 -g)
  target_link_options(w1_memory_safety_test PRIVATE --coverage)
endif()
