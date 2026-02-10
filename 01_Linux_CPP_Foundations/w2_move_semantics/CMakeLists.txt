# Copyright 2026 Edge-AI-Genesis
# W2: Move Semantics - Benchmark (C++20)

add_executable(benchmark_move_vs_copy benchmark_move_vs_copy.cpp)

# Benchmark 需要优化以获得准确的性能数据
target_compile_options(benchmark_move_vs_copy PRIVATE -O2)
