find_package(OpenCV REQUIRED COMPONENTS core imgproc)
find_package(Threads REQUIRED)

# ============================================================================
# w10_resize_lib：自定义 Resize 算子静态库（V1 最近邻 / V2 双线性 / V3 Letterbox）
# ============================================================================
add_library(w10_resize_lib STATIC custom_resize.cpp)
set_property(TARGET w10_resize_lib PROPERTY CXX_STANDARD 20)
target_include_directories(w10_resize_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${OpenCV_INCLUDE_DIRS})
target_link_libraries(w10_resize_lib PUBLIC ${OpenCV_LIBS})

# ============================================================================
# w10_resize：benchmark 可执行文件
# ============================================================================
add_executable(w10_resize resize_bench.cpp)
set_property(TARGET w10_resize PROPERTY CXX_STANDARD 20)
target_link_libraries(w10_resize PRIVATE w10_resize_lib)

# ============================================================================
# w10_resize_test：GTest 单元测试
# ============================================================================
if(BUILD_TESTING)
  add_executable(w10_resize_test resize_test.cpp)
  set_property(TARGET w10_resize_test PROPERTY CXX_STANDARD 20)
  target_link_libraries(w10_resize_test
      PRIVATE
      w10_resize_lib
      GTest::gtest_main
      Threads::Threads)

  add_test(NAME W10_ResizeTest COMMAND w10_resize_test)
endif()
