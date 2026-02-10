# Copyright 2026 Edge-AI-Genesis
# W5: Thread Pool - jthread + stop_token (C++20)

find_package(Threads REQUIRED)

# GTest 测试
add_executable(w5_thread_pool_test thread_pool_test.cpp)
target_link_libraries(w5_thread_pool_test PRIVATE Threads::Threads GTest::gtest_main)
add_test(NAME W5_ThreadPoolTest COMMAND w5_thread_pool_test)
