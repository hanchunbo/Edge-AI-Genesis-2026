# Copyright 2026 Edge-AI-Genesis
#
# W13：高性能多线程图像预处理引擎（Q1 综合项目）
# 复用：w5（header-only 线程池）/ w9_bgr2gray_lib / w10_resize_lib

find_package(OpenCV REQUIRED COMPONENTS core imgproc)
find_package(Threads REQUIRED)

# ============================================================================
# w13_image_pipeline_lib：核心管道静态库
# ============================================================================
add_library(w13_image_pipeline_lib STATIC image_pipeline.cpp)
set_property(TARGET w13_image_pipeline_lib PROPERTY CXX_STANDARD 20)

target_include_directories(w13_image_pipeline_lib
    PUBLIC
        # 让包含方可用 "image_pipeline.hpp" 直接 include
        ${CMAKE_CURRENT_SOURCE_DIR}
        # 让 image_pipeline.hpp 内的 "w5_thread_pool/thread_pool.hpp" 和
        # "w10_resize/custom_resize.hpp" 等相对路径可寻址
        ${CMAKE_SOURCE_DIR}/01_Linux_CPP_Foundations
        ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(w13_image_pipeline_lib
    PUBLIC
        # W9/W10 静态库（已在前置 add_subdirectory 中定义）
        w9_bgr2gray_lib
        w10_resize_lib
        ${OpenCV_LIBS}
        Threads::Threads
)

# ============================================================================
# w13_pipeline_demo：基准测试与演示可执行文件
# ============================================================================
add_executable(w13_pipeline_demo pipeline_demo.cpp)
set_property(TARGET w13_pipeline_demo PROPERTY CXX_STANDARD 20)
target_link_libraries(w13_pipeline_demo PRIVATE w13_image_pipeline_lib)

# ============================================================================
# w13_image_pipeline_test：GTest 单元测试
# ============================================================================
if(BUILD_TESTING)
    add_executable(w13_image_pipeline_test image_pipeline_test.cpp)
    set_property(TARGET w13_image_pipeline_test PROPERTY CXX_STANDARD 20)
    target_link_libraries(w13_image_pipeline_test
        PRIVATE
            w13_image_pipeline_lib
            GTest::gtest_main
            Threads::Threads
    )
    add_test(NAME W13_ImagePipelineTest COMMAND w13_image_pipeline_test)

    # W8 覆盖率支持（多线程需要 -fprofile-update=atomic 避免数据竞争）
    if(W8_COVERAGE)
        target_compile_options(w13_image_pipeline_test PRIVATE
            --coverage -O0 -g -fprofile-update=atomic)
        target_link_options(w13_image_pipeline_test PRIVATE --coverage)
    endif()
endif()
