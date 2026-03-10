# Copyright 2026 Edge-AI-Genesis
# Top-level CMake configuration for Edge-AI-Genesis-2026

cmake_minimum_required(VERSION 3.16)

# 编译器设置（必须在 project() 之前）
set(CMAKE_CXX_COMPILER "g++-13" CACHE STRING "C++ compiler")

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
# Google Test 集成（支持离线 / 内网环境）
# =============================================================================
#
# 完全跳过测试（无需 GTest，只编译功能程序）：
#   cmake -DBUILD_TESTING=OFF ...
#
# 需要测试时的降级策略（按优先级）：
#   ① 系统已安装 libgtest-dev  →  find_package 直接使用，零网络请求
#   ② 本地缓存目录             →  cmake -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=/path
#   ③ 网络下载（默认）         →  FetchContent 从 GitHub 拉取 v1.15.2
#
# 离线环境一次性配置：
#   方式A（推荐）：sudo apt install libgtest-dev libgmock-dev
#   方式B（无 apt）：下载 v1.15.2.zip 解压后，cmake 时加
#                   -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=/path/to/googletest-1.15.2

option(BUILD_TESTING "构建单元测试（关闭后无需 GTest，仅编译功能程序）" ON)

if(BUILD_TESTING)
  enable_testing()
  include(CTest)

  # 已下载的缓存在重新 configure 时不再联网（解决增量构建/离线重配问题）
  set(FETCHCONTENT_UPDATES_DISCONNECTED
      ON
      CACHE BOOL "已下载的依赖不重复联网更新" FORCE)

  # ── 优先使用系统安装的 GTest ──────────────────────────────────────────────
  find_package(GTest QUIET)

  if(GTest_FOUND)
    message(STATUS "[GTest] 使用系统版本（离线环境友好）")
    # CMake < 3.20 的 FindGTest 只提供 GTest::GTest / GTest::Main，
    # 本项目所有子模块统一使用 GTest::gtest_main，这里补全别名保持兼容
    if(NOT TARGET GTest::gtest_main AND TARGET GTest::Main)
      add_library(GTest::gtest_main ALIAS GTest::Main)
    endif()
    if(NOT TARGET GTest::gtest AND TARGET GTest::GTest)
      add_library(GTest::gtest ALIAS GTest::GTest)
    endif()
  else()
    # ── 降级到 FetchContent（需网络或本地缓存）──────────────────────────────
    message(STATUS "[GTest] 系统未安装，尝试 FetchContent（需网络或本地缓存目录）")
    include(FetchContent)
    FetchContent_Declare(
      googletest
      URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    # Prevent GTest from overriding compiler/linker settings on Windows
    set(gtest_force_shared_crt
        ON
        CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
  endif()
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
