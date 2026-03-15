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
