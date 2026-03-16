# Copyright 2026 Edge-AI-Genesis
# W5: Thread Pool - jthread + stop_token (C++20)

find_package(Threads REQUIRED)

# 演示程序：展示 Submit / SubmitWithToken / WaitForAll 等核心接口
add_executable(w5_thread_pool_demo thread_pool_demo.cpp)
target_link_libraries(w5_thread_pool_demo PRIVATE Threads::Threads)

if(BUILD_TESTING)
  add_executable(w5_thread_pool_test thread_pool_test.cpp)
  target_link_libraries(w5_thread_pool_test PRIVATE Threads::Threads GTest::gtest_main)
  add_test(NAME W5_ThreadPoolTest COMMAND w5_thread_pool_test)

  # 注册为 CTest，CI 可验证演示程序能正常编译并运行
  add_test(NAME W5_ThreadPoolDemo COMMAND w5_thread_pool_demo)
endif()

# W8 覆盖率支持：cmake -DW8_COVERAGE=ON 时注入 gcov 插桩
# --coverage 等价于 -fprofile-arcs -ftest-coverage；-O0 防止插桩被优化掉
if(W8_COVERAGE)
  # -fprofile-update=atomic：线程池并发执行任务时防止 gcov 计数器竞态（负数计数）
  target_compile_options(w5_thread_pool_test PRIVATE
    --coverage -O0 -g -fprofile-update=atomic)
  target_link_options(w5_thread_pool_test PRIVATE --coverage)
endif()
