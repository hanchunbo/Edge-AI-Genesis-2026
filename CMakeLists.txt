# Copyright 2026 Edge-AI-Genesis
# Top-level CMake configuration for Edge-AI-Genesis-2026

cmake_minimum_required(VERSION 3.16)

# 编译器设置（必须在 project() 之前）
set(CMAKE_CXX_COMPILER "g++-15" CACHE STRING "C++ compiler")

# import std; 实验性支持（必须在 project() 之前，即 CXX 语言启用之前）
# UUID 是 CMake 内部令牌，用于确认开发者知道这是实验性 API（版本变化时 UUID 会变）
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "d0edc3af-4c50-42ea-a356-e2862fe7a444")

project(EdgeAIGenesis2026 LANGUAGES CXX)

# =============================================================================
# 公共默认配置
# =============================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 公共编译警告
add_compile_options(
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall>
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wextra>
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wpedantic>
)

# =============================================================================
# 可选 Sanitizer 支持
# =============================================================================
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)

if(ENABLE_TSAN)
  add_compile_options(-fsanitize=thread -g)
  add_link_options(-fsanitize=thread)
endif()

if(ENABLE_ASAN)
  add_compile_options(-fsanitize=address -g)
  add_link_options(-fsanitize=address)
endif()

# =============================================================================
# Google Test 集成（本地 zip，离线 / 内网环境零网络依赖）
# =============================================================================
#
# GTest 源码 zip 存放于 third_party/v1.15.2.zip，FetchContent 直接读取本地文件，
# configure / build / ctest 全程无需网络访问。
#
# 完全跳过测试（只编译功能程序）：
#   cmake -DBUILD_TESTING=OFF ...

option(BUILD_TESTING "构建单元测试（关闭后无需 GTest，仅编译功能程序）" ON)

if(BUILD_TESTING)
  enable_testing()
  include(CTest)
  include(FetchContent)

  # 使用项目内 zip，file:// 协议指向本地路径，不发起任何网络请求
  # DOWNLOAD_EXTRACT_TIMESTAMP TRUE：解压后使用当前时间戳，避免 CMP0135 警告
  FetchContent_Declare(
    googletest
    URL file://${CMAKE_SOURCE_DIR}/third_party/v1.15.2.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )

  # 防止 googletest 的 install() 规则污染父项目安装树
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  # Windows 下保持 CRT 链接方式一致（Linux 无影响，保留以备跨平台）
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

  FetchContent_MakeAvailable(googletest)

  message(STATUS "[GTest] 使用本地 zip: third_party/v1.15.2.zip")
endif() # BUILD_TESTING

# =============================================================================
# 子模块
# =============================================================================
add_subdirectory(01_Linux_CPP_Foundations/w1_memory_safety)
add_subdirectory(01_Linux_CPP_Foundations/w2_move_semantics)
add_subdirectory(01_Linux_CPP_Foundations/w3_filesystem)
add_subdirectory(01_Linux_CPP_Foundations/w4_threading)
add_subdirectory(01_Linux_CPP_Foundations/w5_thread_pool)
add_subdirectory(01_Linux_CPP_Foundations/w6_mmap_loader)
add_subdirectory(01_Linux_CPP_Foundations/w7_cmake_engineering)

# W8 覆盖率支持：-DW8_COVERAGE=ON 时为各周测试目标注入 gcov 插桩
option(W8_COVERAGE "为 W1-W7 所有测试目标启用 gcov/lcov 覆盖率插桩（同时强制 -O0）" OFF)

add_subdirectory(01_Linux_CPP_Foundations/w8_testing)
