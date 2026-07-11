cmake_minimum_required(VERSION 3.16)
project(opencode-cli VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(CURL REQUIRED)
find_package(nlohmann_json REQUIRED)

include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CURL_INCLUDE_DIRS})

set(SOURCES
    src/main.cpp
    src/config_manager.cpp
    src/git_manager.cpp
    src/mcp_manager.cpp
    src/http_client.cpp
    src/ui.cpp
    src/prompt.cpp
)

set(HEADERS
    include/config_manager.hpp
    include/git_manager.hpp
    include/mcp_manager.hpp
    include/http_client.hpp
    include/ui.hpp
    include/prompt.hpp
)

add_executable(opencode ${SOURCES} ${HEADERS})

target_link_libraries(opencode 
    CURL::libcurl
    nlohmann_json::nlohmann_json
)

target_compile_options(opencode PRIVATE -Wall -Wextra -O2)

install(TARGETS opencode DESTINATION bin)
