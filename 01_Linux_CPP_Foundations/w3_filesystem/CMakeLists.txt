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
