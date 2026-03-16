# Copyright 2026 Edge-AI-Genesis
# W4: Threading - Producer/Consumer with counting_semaphore (C++20)

find_package(Threads REQUIRED)

add_executable(producer_consumer producer_consumer.cpp)
set_target_properties(producer_consumer PROPERTIES CXX_STANDARD 20)
target_include_directories(producer_consumer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(producer_consumer PRIVATE Threads::Threads)

if(BUILD_TESTING)
  add_executable(w4_threading_test w4_threading_test.cpp)
  set_target_properties(w4_threading_test PROPERTIES CXX_STANDARD 20)
  target_include_directories(w4_threading_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(w4_threading_test PRIVATE GTest::gtest_main Threads::Threads)
  add_test(NAME W4_ThreadingTest COMMAND w4_threading_test)
endif()

# W8 覆盖率支持：cmake -DW8_COVERAGE=ON 时注入 gcov 插桩
# --coverage 等价于 -fprofile-arcs -ftest-coverage；-O0 防止插桩被优化掉
if(W8_COVERAGE)
  # -fprofile-update=atomic：多线程并发写覆盖率计数器时防止竞态导致负数
  target_compile_options(w4_threading_test PRIVATE
    --coverage -O0 -g -fprofile-update=atomic)
  target_link_options(w4_threading_test PRIVATE --coverage)
endif()
