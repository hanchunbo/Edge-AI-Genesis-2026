# Copyright 2026 Edge-AI-Genesis
# W1: Memory Safety - RAII + SafeTensorBuffer (C++23)

add_executable(safe_tensor_demo main.cpp)

# W1 需要 C++23 (std::expected)
set_target_properties(safe_tensor_demo PROPERTIES CXX_STANDARD 23)
