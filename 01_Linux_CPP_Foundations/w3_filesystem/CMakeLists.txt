# Copyright 2026 Edge-AI-Genesis
# W3: Filesystem - Model Scanner (C++20)

add_executable(model_scanner model_scanner.cpp)
set_target_properties(model_scanner PROPERTIES CXX_STANDARD 20)
target_include_directories(model_scanner PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(BUILD_TESTING)
  add_executable(w3_filesystem_test w3_filesystem_test.cpp)
  set_target_properties(w3_filesystem_test PROPERTIES CXX_STANDARD 20)
  target_include_directories(w3_filesystem_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(w3_filesystem_test PRIVATE GTest::gtest_main)
  add_test(NAME W3_FilesystemTest COMMAND w3_filesystem_test)
endif()

# W8 覆盖率支持：cmake -DW8_COVERAGE=ON 时注入 gcov 插桩
# --coverage 等价于 -fprofile-arcs -ftest-coverage；-O0 防止插桩被优化掉
if(W8_COVERAGE)
  target_compile_options(w3_filesystem_test PRIVATE --coverage -O0 -g)
  target_link_options(w3_filesystem_test PRIVATE --coverage)
endif()
