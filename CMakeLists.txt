cmake_minimum_required(VERSION 3.14)
project(swarmmind VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find libcurl
find_package(CURL REQUIRED)

# Threads (Blackboard mutex, thread-based agents later)
find_package(Threads REQUIRED)

# Sources
set(SOURCES
    src/main.cpp
    src/config.cpp
    src/agent.cpp
    src/llm_client.cpp
    src/tool.cpp
    src/tools/read_file.cpp
    src/tools/write_file.cpp
    src/tools/edit_file.cpp
    src/tools/bash.cpp
    src/tools/blackboard_post.cpp
    src/blackboard.cpp
    src/inline_command.cpp
    third_party/mini-yaml/yaml/Yaml.cpp
)

# Executable
add_executable(swarmmind ${SOURCES})

# Include directories
target_include_directories(swarmmind PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
    ${CMAKE_SOURCE_DIR}/third_party/mini-yaml/yaml
)

# Link libraries
target_link_libraries(swarmmind PRIVATE CURL::libcurl Threads::Threads)

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(swarmmind PRIVATE -Wall -Wextra -Wpedantic)
    # Vendored third-party code — not held to our warning standards
    set_source_files_properties(third_party/mini-yaml/yaml/Yaml.cpp
        PROPERTIES COMPILE_OPTIONS "-w")
endif()

# Tool integration test (doesn't need curl)
set(TEST_SOURCES
    tests/test_tools.cpp
    src/tool.cpp
    src/tools/read_file.cpp
    src/tools/write_file.cpp
    src/tools/edit_file.cpp
    src/tools/bash.cpp
)
add_executable(test_tools ${TEST_SOURCES})
target_include_directories(test_tools PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(test_tools PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Blackboard unit test (doesn't need curl)
add_executable(test_blackboard tests/test_blackboard.cpp src/blackboard.cpp)
target_include_directories(test_blackboard PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(test_blackboard PRIVATE Threads::Threads)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(test_blackboard PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Inline command parser unit test (doesn't need curl)
add_executable(test_inline_command tests/test_inline_command.cpp src/inline_command.cpp)
target_include_directories(test_inline_command PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(test_inline_command PRIVATE -Wall -Wextra -Wpedantic)
endif()
