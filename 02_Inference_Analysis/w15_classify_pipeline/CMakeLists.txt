# Copyright 2026 Edge-AI-Genesis
#
# =============================================================================
# W15 分类推理端到端闭环
# =============================================================================
# 复用 W14 InferenceEngine（推理）+ OpenCV（解码/resize/crop）。
# 模型文件复用 W14 的 mobilenetv2.onnx，不在本模块重复存放。
# =============================================================================

# W14 库未构建（ORT 包缺失会让 W14 early-return）则本模块整体跳过。
if(NOT TARGET w14_inference_engine)
  message(WARNING "[W15] 未找到 w14_inference_engine 目标（ORT 包可能缺失），跳过 W15 构建。")
  return()
endif()

find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)
find_package(Threads REQUIRED)

# 库目标：w15_classify_lib（后续任务逐步加入 postprocess.cpp / classifier.cpp）
add_library(w15_classify_lib STATIC
  preprocess.cpp
  postprocess.cpp
  classifier.cpp
)
target_include_directories(w15_classify_lib PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${OpenCV_INCLUDE_DIRS}
)
target_link_libraries(w15_classify_lib PUBLIC
  w14_inference_engine
  ${OpenCV_LIBS}
  Threads::Threads
)
target_compile_features(w15_classify_lib PUBLIC cxx_std_20)

# -----------------------------------------------------------------------------
# Demo 可执行：w15_classify_demo
# -----------------------------------------------------------------------------
add_executable(w15_classify_demo classify_demo.cpp)
target_link_libraries(w15_classify_demo PRIVATE w15_classify_lib)
target_compile_definitions(w15_classify_demo PRIVATE
  W15_MODEL_PATH="${CMAKE_SOURCE_DIR}/02_Inference_Analysis/w14_ort_basics/models/mobilenetv2.onnx"
  W15_LABELS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/models/imagenet_classes.txt"
)
set_target_properties(w15_classify_demo PROPERTIES
  BUILD_RPATH "${ONNXRUNTIME_ROOT}/lib")

if(BUILD_TESTING)
  add_executable(w15_preprocess_test preprocess_test.cpp)
  target_link_libraries(w15_preprocess_test
    PRIVATE w15_classify_lib GTest::gtest_main Threads::Threads)
  add_test(NAME W15_PreprocessTest COMMAND w15_preprocess_test)

  add_executable(w15_postprocess_test postprocess_test.cpp)
  target_link_libraries(w15_postprocess_test
    PRIVATE w15_classify_lib GTest::gtest_main Threads::Threads)
  add_test(NAME W15_PostprocessTest COMMAND w15_postprocess_test)

  add_executable(w15_classifier_test classifier_test.cpp)
  target_link_libraries(w15_classifier_test
    PRIVATE w15_classify_lib GTest::gtest_main Threads::Threads)
  target_compile_definitions(w15_classifier_test PRIVATE
    W15_MODEL_PATH="${CMAKE_SOURCE_DIR}/02_Inference_Analysis/w14_ort_basics/models/mobilenetv2.onnx"
    W15_LABELS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/models/imagenet_classes.txt"
    W15_IMAGE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/models/test_cat.jpg"
  )
  set_target_properties(w15_classifier_test PROPERTIES
    BUILD_RPATH "${ONNXRUNTIME_ROOT}/lib")
  add_test(NAME W15_ClassifierTest COMMAND w15_classifier_test)
endif()
