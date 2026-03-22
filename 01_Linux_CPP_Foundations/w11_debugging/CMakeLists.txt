find_package(Threads REQUIRED)

# ============================================================================
# w11_buggy_lab：包含三种故意bug的演示程序（仅用于理解问题，不接入CI测试）
# ============================================================================
add_executable(w11_buggy_lab buggy_lab.cpp)
set_property(TARGET w11_buggy_lab PROPERTY CXX_STANDARD 20)
target_link_libraries(w11_buggy_lab PRIVATE Threads::Threads)

# ============================================================================
# w11_fixed_lab：修复后的版本（Valgrind 验证无泄漏）
# ============================================================================
add_executable(w11_fixed_lab fixed_lab.cpp)
set_property(TARGET w11_fixed_lab PROPERTY CXX_STANDARD 20)
target_link_libraries(w11_fixed_lab PRIVATE Threads::Threads)

# ============================================================================
# w11_debugging_test：GTest 单元测试（验证修复后接口行为正确）
# ============================================================================
if(BUILD_TESTING)
  add_executable(w11_debugging_test debugging_test.cpp fixed_lab.cpp)
  set_property(TARGET w11_debugging_test PROPERTY CXX_STANDARD 20)
  target_link_libraries(w11_debugging_test
      PRIVATE
      GTest::gtest_main
      Threads::Threads)
  target_compile_definitions(w11_debugging_test PRIVATE W11_TEST_MODE)

  add_test(NAME W11_DebuggingTest COMMAND w11_debugging_test)
endif()
