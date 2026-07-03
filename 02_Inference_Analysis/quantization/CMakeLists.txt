# Copyright 2026 Edge-AI-Genesis
#
# =============================================================================
# quant：YOLO 部署硬化与 INT8 评估 harness
# =============================================================================

if(NOT TARGET w16_yolo_lib)
  message(WARNING "[quant] 未找到 w16_yolo_lib 目标，跳过 quant 模块构建。")
  return()
endif()

find_package(Threads REQUIRED)

if(NOT DEFINED Python3_EXECUTABLE
    AND EXISTS "${CMAKE_SOURCE_DIR}/.venv/bin/python")
  set(Python3_EXECUTABLE "${CMAKE_SOURCE_DIR}/.venv/bin/python"
      CACHE FILEPATH "Python interpreter for quantization tools")
endif()
find_package(Python3 QUIET COMPONENTS Interpreter)

set(_QUANT_W16_MODEL
  "${CMAKE_SOURCE_DIR}/02_Inference_Analysis/w16_yolo_detector/models/yolov8n.onnx")
set(_QUANT_W16_IMAGE
  "${CMAKE_SOURCE_DIR}/02_Inference_Analysis/w16_yolo_detector/models/test_image.jpg")
set(_QUANT_MODELS_DIR "${CMAKE_CURRENT_BINARY_DIR}/models")
set(_QUANT_STATIC_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/tools/quantize_yolov8_static.py")

add_library(quant_core STATIC
  rolling_stats.cpp
  eval_harness.cpp
)
target_include_directories(quant_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(quant_core PUBLIC cxx_std_20)
target_link_libraries(quant_core PUBLIC w16_yolo_lib)

add_executable(quant_benchmark quant_benchmark.cpp)
target_link_libraries(quant_benchmark PRIVATE quant_core)
target_compile_definitions(quant_benchmark PRIVATE
  QUANT_W16_MODEL_PATH="${CMAKE_SOURCE_DIR}/02_Inference_Analysis/w16_yolo_detector/models/yolov8n.onnx"
  QUANT_W16_IMAGE_PATH="${CMAKE_SOURCE_DIR}/02_Inference_Analysis/w16_yolo_detector/models/test_image.jpg"
)
target_compile_features(quant_benchmark PRIVATE cxx_std_20)
set_target_properties(quant_benchmark PROPERTIES
  BUILD_RPATH "${ONNXRUNTIME_ROOT}/lib")

# 校准集优先 coco128（ultralytics 默认数据目录）；缺失时回退单张测试图，
# 仅用于验证工具链闭环，不作为正式校准结论。
set(_QUANT_CALIB_DIR "$ENV{HOME}/code/datasets/coco128/images/train2017")
if(EXISTS "${_QUANT_CALIB_DIR}")
  set(_QUANT_CALIB_ARGS --calib-dir "${_QUANT_CALIB_DIR}")
else()
  set(_QUANT_CALIB_ARGS --calib-image "${_QUANT_W16_IMAGE}")
endif()

if(Python3_Interpreter_FOUND)
  add_custom_target(quant_yolov8_static
    COMMAND ${Python3_EXECUTABLE} "${_QUANT_STATIC_SCRIPT}"
            --model "${_QUANT_W16_MODEL}"
            ${_QUANT_CALIB_ARGS}
            --output-dir "${_QUANT_MODELS_DIR}"
    COMMENT "[quant] 生成 YOLOv8n ORT static INT8 QDQ 模型（检测头保 FP32）"
    USES_TERMINAL)
else()
  add_custom_target(quant_yolov8_static
    COMMAND ${CMAKE_COMMAND} -E echo
            "[quant] 未找到 Python3 Interpreter，无法运行 ORT static quantization"
    COMMAND ${CMAKE_COMMAND} -E false
    USES_TERMINAL)
endif()

if(BUILD_TESTING)
  add_executable(quant_rolling_stats_test rolling_stats_test.cpp)
  target_link_libraries(quant_rolling_stats_test
    PRIVATE quant_core GTest::gtest_main Threads::Threads)
  target_compile_features(quant_rolling_stats_test PRIVATE cxx_std_20)
  add_test(NAME Quant_RollingStatsTest COMMAND quant_rolling_stats_test)

  add_executable(quant_eval_harness_test eval_harness_test.cpp)
  target_link_libraries(quant_eval_harness_test
    PRIVATE quant_core GTest::gtest_main Threads::Threads)
  target_compile_definitions(quant_eval_harness_test PRIVATE
    QUANT_W16_MODEL_PATH="${CMAKE_SOURCE_DIR}/02_Inference_Analysis/w16_yolo_detector/models/yolov8n.onnx"
    QUANT_W16_IMAGE_PATH="${CMAKE_SOURCE_DIR}/02_Inference_Analysis/w16_yolo_detector/models/test_image.jpg"
  )
  target_compile_features(quant_eval_harness_test PRIVATE cxx_std_20)
  set_target_properties(quant_eval_harness_test PROPERTIES
    BUILD_RPATH "${ONNXRUNTIME_ROOT}/lib")
  add_test(NAME Quant_EvalHarnessTest COMMAND quant_eval_harness_test)

  add_executable(quant_batch_consistency_test batch_consistency_test.cpp)
  target_link_libraries(quant_batch_consistency_test
    PRIVATE quant_core GTest::gtest_main Threads::Threads)
  target_compile_definitions(quant_batch_consistency_test PRIVATE
    QUANT_W16_MODEL_PATH="${CMAKE_SOURCE_DIR}/02_Inference_Analysis/w16_yolo_detector/models/yolov8n.onnx"
    QUANT_W16_IMAGE_PATH="${CMAKE_SOURCE_DIR}/02_Inference_Analysis/w16_yolo_detector/models/test_image.jpg"
  )
  target_compile_features(quant_batch_consistency_test PRIVATE cxx_std_20)
  set_target_properties(quant_batch_consistency_test PROPERTIES
    BUILD_RPATH "${ONNXRUNTIME_ROOT}/lib")
  add_test(NAME Quant_BatchConsistencyTest COMMAND quant_batch_consistency_test)

  add_executable(quant_int8_consistency_test int8_consistency_test.cpp)
  target_link_libraries(quant_int8_consistency_test
    PRIVATE quant_core GTest::gtest_main Threads::Threads)
  target_compile_definitions(quant_int8_consistency_test PRIVATE
    QUANT_W16_MODEL_PATH="${_QUANT_W16_MODEL}"
    QUANT_W16_IMAGE_PATH="${_QUANT_W16_IMAGE}"
    QUANT_INT8_MINMAX_PATH="${_QUANT_MODELS_DIR}/yolov8n.int8.minmax.onnx"
    QUANT_INT8_ENTROPY_PATH="${_QUANT_MODELS_DIR}/yolov8n.int8.entropy.onnx"
  )
  target_compile_features(quant_int8_consistency_test PRIVATE cxx_std_20)
  set_target_properties(quant_int8_consistency_test PROPERTIES
    BUILD_RPATH "${ONNXRUNTIME_ROOT}/lib")
  add_test(NAME Quant_Int8ConsistencyTest COMMAND quant_int8_consistency_test)
endif()
