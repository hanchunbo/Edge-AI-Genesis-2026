# Copyright 2026 Edge-AI-Genesis
# W7 app/：演示 PUBLIC include 自动继承 —— 无需 target_include_directories

add_executable(w7_app main.cpp)

# 核心演示：只写 target_link_libraries，不写任何 target_include_directories
# CMake 自动将 w7_tensor_utils 的 PUBLIC include 路径传递给 w7_app
# → main.cpp 中 #include "tensor_utils.hpp" 无需写完整路径即可找到头文件
target_link_libraries(w7_app PRIVATE w7_tensor_utils)

# 注册为 CTest，CI 可验证演示程序能正常编译并运行
add_test(NAME W7_App COMMAND w7_app)
