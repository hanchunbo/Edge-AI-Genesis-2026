find_package(OpenCV REQUIRED COMPONENTS core imgproc)
find_package(Threads REQUIRED)

# ============================================================================
# w9_bgr2gray_lib：三版实现编译为静态库，供 bench 和 test 共用
# C++23 因 std::mdspan 多下标语法要求
# ============================================================================
add_library(w9_bgr2gray_lib STATIC bgr2gray.cpp)
set_property(TARGET w9_bgr2gray_lib PROPERTY CXX_STANDARD 23)
target_include_directories(w9_bgr2gray_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/third_party/mdspan
    ${OpenCV_INCLUDE_DIRS})
target_link_libraries(w9_bgr2gray_lib PUBLIC ${OpenCV_LIBS})

# ============================================================================
# w9_bgr2gray：benchmark 可执行文件
# ============================================================================
add_executable(w9_bgr2gray bgr2gray_bench.cpp)
set_property(TARGET w9_bgr2gray PROPERTY CXX_STANDARD 23)
target_link_libraries(w9_bgr2gray PRIVATE w9_bgr2gray_lib)

# ============================================================================
# w9_bgr2gray_test：GTest 测试
# ============================================================================
add_executable(w9_bgr2gray_test bgr2gray_test.cpp)
set_property(TARGET w9_bgr2gray_test PROPERTY CXX_STANDARD 23)
target_link_libraries(w9_bgr2gray_test
    PRIVATE
    w9_bgr2gray_lib
    GTest::gtest_main
    Threads::Threads)

add_test(NAME W9_BGR2GrayTest COMMAND w9_bgr2gray_test)
