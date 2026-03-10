# Copyright 2026 Edge-AI-Genesis
# W4: Threading - Producer/Consumer with counting_semaphore (C++20)

find_package(Threads REQUIRED)

add_executable(producer_consumer producer_consumer.cpp)
set_target_properties(producer_consumer PROPERTIES CXX_STANDARD 20)
target_include_directories(producer_consumer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(producer_consumer PRIVATE Threads::Threads)

# GTest 单元测试
add_executable(w4_threading_test w4_threading_test.cpp)
set_target_properties(w4_threading_test PROPERTIES CXX_STANDARD 20)
target_include_directories(w4_threading_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(w4_threading_test PRIVATE GTest::gtest_main Threads::Threads)
add_test(NAME W4_ThreadingTest COMMAND w4_threading_test)
