# Copyright 2026 Edge-AI-Genesis
#
# =============================================================================
# W14 ONNX Runtime C++ 基础闭环
# =============================================================================
# 接入 ONNX Runtime 1.26 预编译包（third_party/onnxruntime/...）。
# 当前为 CPU EP 单环境（B 路径），CUDA EP 推到 W14 末或 W15 起手补。
# =============================================================================

find_package(Threads REQUIRED)

# -----------------------------------------------------------------------------
# ONNX Runtime 依赖定位
# -----------------------------------------------------------------------------
# 预编译包默认放在 third_party/onnxruntime/onnxruntime-linux-x64-1.26.0/，
# 可通过 -DONNXRUNTIME_ROOT=<path> 覆盖（例如指向系统安装、其他版本）。
#
# 不入库说明：包体 ~22MB，已在 .gitignore 排除 third_party/onnxruntime/。
# 下载方式：见 README.md「前提条件」节。
if(NOT DEFINED ONNXRUNTIME_ROOT)
  set(ONNXRUNTIME_ROOT
      "${CMAKE_SOURCE_DIR}/third_party/onnxruntime/onnxruntime-linux-x64-1.26.0"
      CACHE PATH "ONNX Runtime 预编译包根目录（含 include/ lib/）")
endif()

set(_W14_ORT_INCLUDE "${ONNXRUNTIME_ROOT}/include")
set(_W14_ORT_LIB "${ONNXRUNTIME_ROOT}/lib/libonnxruntime.so")

if(NOT EXISTS "${_W14_ORT_INCLUDE}/onnxruntime_cxx_api.h"
    OR NOT EXISTS "${_W14_ORT_LIB}")
  message(WARNING
    "[W14] 未找到 ONNX Runtime 预编译包：${ONNXRUNTIME_ROOT}\n"
    "      跳过 W14 目标构建。下载方式见 README.md「前提条件」节。")
  return()
endif()

# 上游 1.26.0 包的 onnxruntimeConfig.cmake 把库目录写死成 lib64/，
# 但实际 tarball 是 lib/（packaging bug）。绕开 find_package，
# 手工 IMPORTED 目标更显式、不依赖上游修复。
if(NOT TARGET onnxruntime::onnxruntime)
  add_library(onnxruntime::onnxruntime SHARED IMPORTED GLOBAL)
  set_target_properties(onnxruntime::onnxruntime PROPERTIES
    IMPORTED_LOCATION "${_W14_ORT_LIB}"
    IMPORTED_SONAME "libonnxruntime.so.1"
    INTERFACE_INCLUDE_DIRECTORIES "${_W14_ORT_INCLUDE}"
  )
endif()

message(STATUS "[W14] ONNX Runtime @ ${ONNXRUNTIME_ROOT}")

# -----------------------------------------------------------------------------
# 库目标：w14_inference_engine
# -----------------------------------------------------------------------------
# 提取 InferenceEngine 实现为静态库，让 demo 与 test 都直接复用，
# 避免一份 .cpp 被两次编译。
add_library(w14_inference_engine STATIC
  inference_engine.cpp
)

target_include_directories(w14_inference_engine
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(w14_inference_engine
  PUBLIC onnxruntime::onnxruntime
)

target_compile_features(w14_inference_engine PUBLIC cxx_std_20)

# -----------------------------------------------------------------------------
# Demo 可执行：w14_ort_basics_demo
# -----------------------------------------------------------------------------
add_executable(w14_ort_basics_demo
  ort_basics_demo.cpp
)

target_link_libraries(w14_ort_basics_demo PRIVATE w14_inference_engine)

# 运行时让二进制能直接 dlopen 到 ORT .so，避免每次 export LD_LIBRARY_PATH
set_target_properties(w14_ort_basics_demo PROPERTIES
  BUILD_RPATH "${ONNXRUNTIME_ROOT}/lib"
)

# -----------------------------------------------------------------------------
# 单元测试：w14_inference_engine_test
# -----------------------------------------------------------------------------
# 步骤 5 实装；BUILD_TESTING 关闭或测试源文件仍为骨架时跳过。
if(BUILD_TESTING)
  add_executable(w14_inference_engine_test
    inference_engine_test.cpp
  )
  target_link_libraries(w14_inference_engine_test
    PRIVATE w14_inference_engine
            GTest::gtest_main
            Threads::Threads
  )
  # 给测试编译期常量：模型绝对路径，让 ctest 与裸跑都能定位模型，
  # 不依赖工作目录。模型缺失时测试用 GTEST_SKIP() 优雅跳过。
  target_compile_definitions(w14_inference_engine_test
    PRIVATE W14_MODEL_PATH="${CMAKE_CURRENT_SOURCE_DIR}/models/mobilenetv2.onnx"
  )
  set_target_properties(w14_inference_engine_test PROPERTIES
    BUILD_RPATH "${ONNXRUNTIME_ROOT}/lib"
  )
  add_test(NAME W14_InferenceEngineTest COMMAND w14_inference_engine_test)
endif()
