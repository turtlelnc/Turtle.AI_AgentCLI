cmake_minimum_required(VERSION 3.16)
project(opencode VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 查找 CURL
find_package(CURL REQUIRED)

# 包含目录（含本地 nlohmann/json.hpp）
include_directories(${CMAKE_SOURCE_DIR}/include)

# 源文件
set(SOURCES
    src/main.cpp
    src/config_manager.cpp
    src/token_tracker.cpp
    src/git_manager.cpp
    src/mcp_manager.cpp
    src/http_client.cpp
    src/ui.cpp
    src/prompt.cpp
)

# 创建可执行文件
add_executable(opencode ${SOURCES})

# 链接库（仅 CURL，nlohmann_json 为 header-only）
target_link_libraries(opencode PRIVATE CURL::libcurl)

# 编译选项
target_compile_options(opencode PRIVATE -Wall -Wextra -O2)
