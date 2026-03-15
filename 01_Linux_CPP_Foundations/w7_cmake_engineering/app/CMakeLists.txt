# Copyright 2026 Edge-AI-Genesis
# W7 app/：演示 PUBLIC include 自动继承 —— 无需 target_include_directories
#
# 这个文件只做一件事：把 main.cpp 编译成可执行文件，并链接 tensor_utils 库。
# 核心演示点：只写 target_link_libraries，不写任何 target_include_directories，
# CMake 会自动把 lib/ 里声明的 PUBLIC 头文件路径传递过来。

# add_executable(目标名  源文件...)
# 作用：告诉 CMake "把这些 .cpp 文件编译成一个可执行文件（二进制程序）"
# 目标名 w7_app：后续用这个名字引用，也是最终生成的二进制文件名
add_executable(w7_app main.cpp)

# target_link_libraries(目标名  可见性  依赖库...)
# 作用：把依赖库链接到目标上，相当于告诉链接器"编译 w7_app 时需要 w7_tensor_utils"
#
# 可见性 PRIVATE：
#   "w7_tensor_utils 是 w7_app 的内部实现依赖，不对外暴露"
#   → 如果将来有其他目标链接 w7_app，它不会自动得到 w7_tensor_utils
#   → 这里用 PRIVATE 是正确的，因为 w7_app 是最终可执行文件，没有"消费者"
#
# 自动继承的效果（本文件的核心演示）：
#   w7_tensor_utils 在 lib/CMakeLists.txt 里用 PUBLIC 声明了 include 路径（lib/ 目录）
#   只写这一行 target_link_libraries，CMake 就自动把那个路径告诉 w7_app 的编译器
#   → main.cpp 里 #include "tensor_utils.hpp" 能直接找到，不需要写完整路径
#   → 也不需要在这里额外写 target_include_directories(w7_app ...)
target_link_libraries(w7_app PRIVATE w7_tensor_utils)

# add_test(NAME 测试名  COMMAND 要运行的命令)
# 作用：向 CTest 注册一个测试项
#   运行 ctest 时，CTest 会执行 w7_app 这个程序
#   程序退出码为 0 → 测试通过；非 0 → 测试失败
#   可以用 ctest -R W7_App 单独跑这一条
add_test(NAME W7_App COMMAND w7_app)
