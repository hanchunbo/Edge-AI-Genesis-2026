# Copyright 2026 Edge-AI-Genesis
#
# =============================================================================
# trt：TensorRT C++ Engine（FP16/INT8 + GPU 端到端）
# =============================================================================
# 依赖系统 deb 安装的 TensorRT 10.x + CUDA Toolkit 12.3。
# 无 TRT/CUDA 的环境（VPS/CI）：warning + return 整体跳过，对齐 w14/quant 守卫模式。
# M1/M2 不编 .cu（GPU buffer 走 cudart 主机 API），enable_language(CUDA) 留到 M3。

find_package(CUDAToolkit QUIET)
find_path(TRT_INCLUDE_DIR NvInfer.h)
find_library(TRT_NVINFER_LIB nvinfer)
find_library(TRT_NVONNXPARSER_LIB nvonnxparser)

if(NOT CUDAToolkit_FOUND OR NOT TRT_INCLUDE_DIR
   OR NOT TRT_NVINFER_LIB OR NOT TRT_NVONNXPARSER_LIB)
  message(WARNING "[trt] 未找到 TensorRT/CUDAToolkit，跳过 trt 模块构建。")
  return()
endif()

message(STATUS "[trt] TensorRT: ${TRT_NVINFER_LIB}")
message(STATUS "[trt] CUDAToolkit: ${CUDAToolkit_VERSION}")

find_package(Threads REQUIRED)
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)

# 复用 W16 模型资产与统一 engine 缓存目录（缓存戳编入文件名，见 engine_builder）。
set(_TRT_W16_MODELS
  "${CMAKE_SOURCE_DIR}/02_Inference_Analysis/w16_yolo_detector/models")
set(_TRT_ENGINE_CACHE "${CMAKE_CURRENT_BINARY_DIR}/engine_cache")

# -----------------------------------------------------------------------------
# trt_core：公共设施 + engine 构建/运行时（随 M1 各 Task 增量加源文件）
# -----------------------------------------------------------------------------
add_library(trt_core STATIC
  trt_common.cpp
)
target_include_directories(trt_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${TRT_INCLUDE_DIR}
)
target_link_libraries(trt_core PUBLIC
  ${TRT_NVINFER_LIB}
  ${TRT_NVONNXPARSER_LIB}
  CUDA::cudart
)
target_compile_features(trt_core PUBLIC cxx_std_20)

if(BUILD_TESTING)
  add_executable(trt_common_test trt_common_test.cpp)
  target_link_libraries(trt_common_test
    PRIVATE trt_core GTest::gtest_main Threads::Threads)
  add_test(NAME Trt_CommonTest COMMAND trt_common_test)
endif()
