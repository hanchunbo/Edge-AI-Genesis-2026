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
endif()
