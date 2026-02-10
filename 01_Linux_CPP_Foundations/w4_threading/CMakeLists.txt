# Copyright 2026 Edge-AI-Genesis
# W4: Threading - Producer/Consumer with counting_semaphore (C++20)

find_package(Threads REQUIRED)

add_executable(producer_consumer producer_consumer.cpp)
target_link_libraries(producer_consumer PRIVATE Threads::Threads)
