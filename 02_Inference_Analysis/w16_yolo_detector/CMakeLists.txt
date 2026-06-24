# Copyright 2026 Edge-AI-Genesis
#
# =============================================================================
# W16 YOLOv8n 检测：NMS + 输出头解析 + YOLODetector + ORT 进阶
# =============================================================================
# 分两层：
#   - w16_detect_core：纯 C++ 后处理核心（NMS / 解码），不依赖 ORT/OpenCV，
#     可独立单测——逐类 NMS 与坐标反算是最高 bug 风险点，隔离测试。
#   - YOLODetector / demo / benchmark：依赖 w14 InferenceEngine + OpenCV + w10
#     Letterbox，按 W15 的守卫模式，w14 库缺失（ORT 包不在）则整体跳过。
# =============================================================================

find_package(Threads REQUIRED)

# -----------------------------------------------------------------------------
# 纯 C++ 后处理核心库：w16_detect_core
# -----------------------------------------------------------------------------
add_library(w16_detect_core STATIC
  nms.cpp
  decode.cpp
)
target_include_directories(w16_detect_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(w16_detect_core PUBLIC cxx_std_20)

# -----------------------------------------------------------------------------
# 单元测试（纯 C++，不需要模型）
# -----------------------------------------------------------------------------
if(BUILD_TESTING)
  add_executable(w16_nms_test nms_test.cpp)
  target_link_libraries(w16_nms_test
    PRIVATE w16_detect_core GTest::gtest_main Threads::Threads)
  add_test(NAME W16_NmsTest COMMAND w16_nms_test)

  add_executable(w16_decode_test decode_test.cpp)
  target_link_libraries(w16_decode_test
    PRIVATE w16_detect_core GTest::gtest_main Threads::Threads)
  add_test(NAME W16_DecodeTest COMMAND w16_decode_test)
endif()

# =============================================================================
# YOLODetector / demo / benchmark —— 依赖 W14 InferenceEngine（ORT）+ OpenCV
# + w10 Letterbox。ORT 包缺失会让 W14 库 early-return，此处整体跳过（同 W15）。
# =============================================================================
if(NOT TARGET w14_inference_engine)
  message(WARNING "[W16] 未找到 w14_inference_engine 目标（ORT 包可能缺失），跳过 YOLODetector 构建。")
  return()
endif()

find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)

# 编排库：组合 detect_core（NMS/解码）+ w10 Letterbox + w14 推理引擎。
add_library(w16_yolo_lib STATIC yolo_detector.cpp)
target_include_directories(w16_yolo_lib PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${OpenCV_INCLUDE_DIRS}
)
target_link_libraries(w16_yolo_lib PUBLIC
  w16_detect_core
  w10_resize_lib
  w14_inference_engine
  ${OpenCV_LIBS}
)
target_compile_features(w16_yolo_lib PUBLIC cxx_std_20)

# 测试图 / 模型 / 参考文件路径（编译期注入，缺失时测试 GTEST_SKIP）。
set(_W16_MODELS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/models")

# Demo 可执行：w16_yolo_demo（画框存图 + 打印检测结果）
add_executable(w16_yolo_demo yolo_demo.cpp)
target_link_libraries(w16_yolo_demo PRIVATE w16_yolo_lib)
target_compile_definitions(w16_yolo_demo PRIVATE
  W16_MODEL_PATH="${_W16_MODELS_DIR}/yolov8n.onnx"
  W16_IMAGE_PATH="${_W16_MODELS_DIR}/test_image.jpg"
  W16_LABELS_PATH="${_W16_MODELS_DIR}/coco_classes.txt"
)
set_target_properties(w16_yolo_demo PROPERTIES
  BUILD_RPATH "${ONNXRUNTIME_ROOT}/lib")

# Benchmark 可执行：w16_yolo_benchmark（batch 1v4 × {CPU,CUDA} × {Run,IOBinding}）
add_executable(w16_yolo_benchmark yolo_benchmark.cpp)
target_link_libraries(w16_yolo_benchmark PRIVATE w16_yolo_lib)
target_compile_definitions(w16_yolo_benchmark PRIVATE
  W16_MODEL_PATH="${_W16_MODELS_DIR}/yolov8n.onnx"
  W16_IMAGE_PATH="${_W16_MODELS_DIR}/test_image.jpg"
)
set_target_properties(w16_yolo_benchmark PROPERTIES
  BUILD_RPATH "${ONNXRUNTIME_ROOT}/lib")

if(BUILD_TESTING)
  add_executable(w16_yolo_detector_test yolo_detector_test.cpp)
  target_link_libraries(w16_yolo_detector_test
    PRIVATE w16_yolo_lib GTest::gtest_main Threads::Threads)
  target_compile_definitions(w16_yolo_detector_test PRIVATE
    W16_MODEL_PATH="${_W16_MODELS_DIR}/yolov8n.onnx"
    W16_IMAGE_PATH="${_W16_MODELS_DIR}/test_image.jpg"
    W16_REFERENCE_PATH="${_W16_MODELS_DIR}/reference_detections.txt"
  )
  set_target_properties(w16_yolo_detector_test PROPERTIES
    BUILD_RPATH "${ONNXRUNTIME_ROOT}/lib")
  add_test(NAME W16_DetectorTest COMMAND w16_yolo_detector_test)
endif()
