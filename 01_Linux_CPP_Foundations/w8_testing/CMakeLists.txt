# Copyright 2026 Edge-AI-Genesis
#
# W8：覆盖率汇总 target
# 使用方式：
#   cmake -B build -S . -DCMAKE_CXX_COMPILER=g++-15 -G Ninja -DW8_COVERAGE=ON
#   cmake --build build -j$(nproc)
#   cmake --build build --target w8_coverage
#   报告在 build/w8_coverage_report/index.html

if(W8_COVERAGE)
  find_program(LCOV_PATH lcov REQUIRED)
  find_program(GENHTML_PATH genhtml REQUIRED)

  # 查找与当前 CXX 编译器匹配的 gcov 工具
  # [Legacy]: lcov 默认使用 /usr/bin/gcov（系统版本）。
  # [Pain Point]: 用 GCC 15 编译的 .gcda 文件格式（B52*）与 gcov-13（B33*）不兼容，
  #   geninfo 报 "Incompatible GCC/GCOV version" 错误。
  # [Modern Fix]: 通过 --gcov-tool 指定 gcov-15，保证格式版本一致。
  string(REGEX MATCH "^([0-9]+)" _cxx_major "${CMAKE_CXX_COMPILER_VERSION}")
  find_program(GCOV_PATH
    NAMES gcov-${_cxx_major} gcov
    REQUIRED)
  message(STATUS "[W8 覆盖率] 使用 gcov 工具: ${GCOV_PATH}")

  add_custom_target(w8_coverage
    # 清除历史计数器，避免和上一次运行叠加
    COMMAND ${LCOV_PATH} --gcov-tool ${GCOV_PATH}
            --zerocounters --directory ${CMAKE_BINARY_DIR}
    # 运行 W1-W7 全部注册测试
    COMMAND ctest --test-dir ${CMAKE_BINARY_DIR} -R "W[1-7]_" --output-on-failure
    # 采集覆盖率数据
    # --ignore-errors inconsistent：lcov 2.x + GCC 15 对内联函数行号有误判，忽略即可
    COMMAND ${LCOV_PATH}
            --gcov-tool ${GCOV_PATH}
            --capture
            --directory ${CMAKE_BINARY_DIR}
            --output-file ${CMAKE_BINARY_DIR}/w8_raw.info
            --ignore-errors mismatch,inconsistent,negative
    # 过滤第三方代码（系统头文件 + FetchContent 解压的 googletest 源码位于 _deps/）
    # --ignore-errors unused：shell 展开 /usr/* 后部分路径（如 /usr/bin）不在 tracefile 中，
    #   lcov 2.x 会将"无匹配的排除模式"视为 error，加 unused 忽略即可
    COMMAND ${LCOV_PATH}
            --gcov-tool ${GCOV_PATH}
            --remove ${CMAKE_BINARY_DIR}/w8_raw.info
            "/usr/*"
            "*/_deps/*"
            --output-file ${CMAKE_BINARY_DIR}/w8_filtered.info
            --ignore-errors mismatch,inconsistent,unused
    # 生成 HTML 可视化报告
    COMMAND ${GENHTML_PATH}
            ${CMAKE_BINARY_DIR}/w8_filtered.info
            --output-directory ${CMAKE_BINARY_DIR}/w8_coverage_report
            --title "Edge-AI-Genesis-2026 W1-W7 Coverage"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "W8 覆盖率报告 → build/w8_coverage_report/index.html"
  )
endif()
