# Copyright 2026 Edge-AI-Genesis
# W7 lib/：核心演示 —— add_library + INTERFACE/PRIVATE/PUBLIC 三种权限
#
# 这个文件做两件事：
#   1. 定义 w7_tensor_utils 静态库，并设置头文件路径/编译选项/宏定义
#   2. 配置 install() 规则，让外部项目能通过 find_package() 使用本库

# =============================================================================
# include(模块名)：加载 CMake 内置模块，扩展 CMake 的能力
# GNUInstallDirs 模块的作用：定义平台无关的标准安装路径变量
#   CMAKE_INSTALL_LIBDIR     → Linux x86_64 上是 "lib/x86_64-linux-gnu"
#                              普通 x86 上是 "lib"
#   CMAKE_INSTALL_INCLUDEDIR → 始终是 "include"
# 如果不用这个模块、直接硬写 "lib"，在 Debian/Ubuntu 64 位系统上路径就错了
# =============================================================================
include(GNUInstallDirs)

# =============================================================================
# add_library(目标名  类型  源文件...)
# 作用：告诉 CMake "我要把这些 .cpp 文件编译成一个库"
#
# 目标名 w7_tensor_utils：后续所有 target_* 命令和 target_link_libraries 都用这个名字
# STATIC：编译成静态库（.a 文件），链接时整个库被打包进可执行文件
#   对比：SHARED → 动态库（.so），运行时才加载；INTERFACE → 纯头文件库，没有 .a
# =============================================================================
add_library(w7_tensor_utils STATIC tensor_utils.cpp)

# =============================================================================
# target_include_directories(目标名  可见性  头文件搜索路径...)
# 作用：告诉编译器 #include "xxx.hpp" 时去哪个目录找文件
#
# 可见性 PUBLIC 的含义（这是本文件的核心演示）：
#   "本库自己编译时需要这个路径，并且链接本库的目标（app/tests）也自动继承这个路径"
#   → 效果：app/CMakeLists.txt 只需写 target_link_libraries(w7_app PRIVATE w7_tensor_utils)
#           main.cpp 就能直接写 #include "tensor_utils.hpp"，无需再配置任何路径
#
# $<BUILD_INTERFACE:路径>：生成器表达式，构建阶段才展开
#   ${CMAKE_CURRENT_SOURCE_DIR} = 当前 CMakeLists.txt 所在目录，即 lib/
#   → 构建时头文件在源码树的 lib/ 目录下，直接用源码路径
#
# $<INSTALL_INTERFACE:路径>：生成器表达式，cmake --install 阶段才展开
#   → 安装后头文件在 ${CMAKE_INSTALL_PREFIX}/include/w7/ 下
#   → 外部项目 find_package() 后使用的是这个路径
#
# 为什么要两个表达式而不是写一个固定路径？
#   构建时和安装后头文件位置不同，必须分开描述，否则一个场景会找不到头文件
# =============================================================================
target_include_directories(
  w7_tensor_utils
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
         $<INSTALL_INTERFACE:include/w7>)

# =============================================================================
# target_compile_options(目标名  可见性  编译器参数...)
# 作用：给编译器传参数（相当于 g++ 命令行加选项）
#
# 可见性 PRIVATE：
#   "只影响 tensor_utils.cpp 自己的编译，不传染给链接本库的 app/tests"
#   → app 和 tests 不会因此被加上 -O2，Debug 模式下它们仍可正常调试
#
# $<$<CONFIG:Release>:-O2>：嵌套生成器表达式，意思是：
#   "当构建类型是 Release 时，加上 -O2 优化选项；否则什么都不加"
#   为什么不直接写 -O2？
#   → 直接写会导致 Debug 构建也被优化，调试时变量消失、断点错位
# =============================================================================
target_compile_options(w7_tensor_utils
                       PRIVATE $<$<CONFIG:Release>:-O2>)

# =============================================================================
# target_compile_definitions(目标名  可见性  宏定义...)
# 作用：给编译器传 -D 宏定义，相当于代码里写 #define
#
# 可见性 INTERFACE：
#   "本库自身编译时不定义这个宏，但链接本库的 app/tests 编译时自动获得"
#   → app.cpp 里可以直接写 #ifdef W7_TENSOR_UTILS_AVAILABLE 来检测库是否存在
#   → tensor_utils.cpp 本身不需要知道自己"是否存在"，所以不定义给自己
# =============================================================================
target_compile_definitions(w7_tensor_utils
                           INTERFACE W7_TENSOR_UTILS_AVAILABLE=1)

# =============================================================================
# 以下是 install() 规则
# 作用：声明执行 cmake --install build 时，把什么文件装到什么位置
# 注意：install() 不影响构建，只在你主动安装时才执行
# =============================================================================

# install(TARGETS ...)：安装编译产物（库文件）
#   TARGETS w7_tensor_utils：要安装的目标（就是上面 add_library 定义的）
#   EXPORT W7TensorUtilsTargets：把这个目标加入"导出集"
#     导出集是一个命名的目标列表，后续 install(EXPORT...) 会把它写成 .cmake 文件
#     外部项目 find_package() 时需要这个 .cmake 文件才能找到库
#   ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}：
#     把 .a 静态库文件安装到 /usr/local/lib/（或平台对应的 lib 目录）
#   INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}：
#     声明头文件根路径，配合上面 INSTALL_INTERFACE:include/w7，
#     让 find_package 后的消费者知道 #include 时去 include/w7/ 找
install(
  TARGETS w7_tensor_utils
  EXPORT W7TensorUtilsTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# install(FILES ...)：安装普通文件（这里是头文件）
#   把 tensor_utils.hpp 复制到安装目录的 include/w7/ 下
#   放在子目录 w7/ 而不是直接放 include/ 根，避免和其他库的头文件同名冲突
install(FILES tensor_utils.hpp
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/w7)

# install(EXPORT ...)：把"导出集"写成 .cmake 文件并安装
#   FILE W7TensorUtilsTargets.cmake：生成的文件名
#   NAMESPACE w7::：给目标加命名空间前缀
#     外部项目写 target_link_libraries(xxx PRIVATE w7::w7_tensor_utils)
#     加命名空间是为了避免不同库的目标名撞车（比如两个库都叫 utils）
#   DESTINATION：安装到 lib/cmake/W7TensorUtils/（find_package 的默认搜索路径）
install(
  EXPORT W7TensorUtilsTargets
  FILE W7TensorUtilsTargets.cmake
  NAMESPACE w7::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/W7TensorUtils)

# configure_file(输入模板  输出文件  @ONLY)
# 作用：读取 .cmake.in 模板文件，把 @变量名@ 替换为实际值，写出一个 .cmake 文件
#   W7TensorUtilsConfig.cmake 是 find_package(W7TensorUtils) 的入口文件
#   没有这个文件，find_package 就找不到本库（只有 Targets.cmake 不够）
# @ONLY：只替换 @VAR@ 格式，不替换 ${VAR}，防止 CMake 变量被误替换
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/W7TensorUtilsConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/W7TensorUtilsConfig.cmake"
  @ONLY)

# 把生成好的 Config.cmake 安装到和 Targets.cmake 同一目录
# find_package(W7TensorUtils) 会在这个目录下找 W7TensorUtilsConfig.cmake
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/W7TensorUtilsConfig.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/W7TensorUtils)
