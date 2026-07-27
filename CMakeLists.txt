cmake_minimum_required(VERSION 3.16)
project(Turtle.AI_AgentCLI VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 查找 CURL
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

# 包含目录（含本地 nlohmann/json.hpp）
include_directories(${CMAKE_SOURCE_DIR}/include)

# 源文件
set(SOURCES
    src/main.cpp
    src/config_manager.cpp
    src/token_tracker.cpp
    src/git_manager.cpp
    src/tool_registry.cpp
    src/sandbox_runner.cpp
    src/tool_policy.cpp
    src/stream_parser.cpp
    src/text_formatter.cpp
    src/skill_manager.cpp
    src/change_journal.cpp
    src/session_manager.cpp
    src/session_model.cpp
    src/secret_store.cpp
    src/endpoint_policy.cpp
    src/agent_session.cpp
    src/subagent_runner.cpp
    src/session_controls.cpp
    src/diff_view.cpp
    src/context_manager.cpp
    src/model_provider.cpp
    src/http_client.cpp
    src/http_retry_policy.cpp
    src/mcp_client.cpp
    src/hooks.cpp
    src/ui.cpp
    src/prompt.cpp
)

# 创建可执行文件
add_executable(Turtle.AI_AgentCLI ${SOURCES})

# 链接库（仅 CURL，nlohmann_json 为 header-only）
target_link_libraries(Turtle.AI_AgentCLI PRIVATE CURL::libcurl Threads::Threads)
if(APPLE)
    target_link_libraries(Turtle.AI_AgentCLI PRIVATE
        "-framework Security" "-framework CoreFoundation")
endif()
if(MINGW)
    target_link_options(Turtle.AI_AgentCLI PRIVATE -static)
endif()

# 编译选项
target_compile_options(Turtle.AI_AgentCLI PRIVATE -Wall -Wextra -O2)

include(CTest)
if(BUILD_TESTING)
    add_executable(tool_registry_tests
        tests/tool_registry_tests.cpp
        src/tool_registry.cpp
        src/sandbox_runner.cpp
        src/change_journal.cpp
    )
    target_include_directories(tool_registry_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_options(tool_registry_tests PRIVATE -Wall -Wextra)
    target_link_libraries(tool_registry_tests PRIVATE Threads::Threads)
    add_test(NAME tool_registry_workspace_boundary COMMAND tool_registry_tests)

    add_executable(sandbox_runner_tests
        tests/sandbox_runner_tests.cpp
        src/sandbox_runner.cpp
    )
    target_include_directories(sandbox_runner_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_options(sandbox_runner_tests PRIVATE -Wall -Wextra)
    target_link_libraries(sandbox_runner_tests PRIVATE Threads::Threads)
    add_test(NAME sandbox_process_isolation COMMAND sandbox_runner_tests)

    add_executable(tool_policy_tests
        tests/tool_policy_tests.cpp
        src/tool_policy.cpp
    )
    target_include_directories(tool_policy_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_options(tool_policy_tests PRIVATE -Wall -Wextra)
    add_test(NAME tool_approval_policy COMMAND tool_policy_tests)

    add_executable(stream_parser_tests
        tests/stream_parser_tests.cpp
        src/stream_parser.cpp
    )
    target_include_directories(stream_parser_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_options(stream_parser_tests PRIVATE -Wall -Wextra)
    add_test(NAME provider_stream_aggregation COMMAND stream_parser_tests)

    add_executable(text_formatter_tests
        tests/text_formatter_tests.cpp
        src/text_formatter.cpp
    )
    target_include_directories(text_formatter_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_options(text_formatter_tests PRIVATE -Wall -Wextra)
    add_test(NAME terminal_table_alignment COMMAND text_formatter_tests)

    add_executable(skill_manager_tests
        tests/skill_manager_tests.cpp
        src/skill_manager.cpp
        src/change_journal.cpp
    )
    target_include_directories(skill_manager_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_options(skill_manager_tests PRIVATE -Wall -Wextra)
    add_test(NAME skill_discovery_and_loading COMMAND skill_manager_tests)

    add_executable(change_journal_tests
        tests/change_journal_tests.cpp
        src/change_journal.cpp
    )
    target_include_directories(change_journal_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_options(change_journal_tests PRIVATE -Wall -Wextra)
    add_test(NAME reversible_change_journal COMMAND change_journal_tests)

    add_executable(session_manager_tests
        tests/session_manager_tests.cpp
        src/session_manager.cpp
        src/session_model.cpp
    )
    target_include_directories(session_manager_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_options(session_manager_tests PRIVATE -Wall -Wextra)
    add_test(NAME persistent_session_roundtrip COMMAND session_manager_tests)

    add_executable(config_security_tests
        tests/config_security_tests.cpp
        src/config_manager.cpp
        src/secret_store.cpp
    )
    target_include_directories(config_security_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_options(config_security_tests PRIVATE -Wall -Wextra)
    if(APPLE)
        target_link_libraries(config_security_tests PRIVATE
            "-framework Security" "-framework CoreFoundation")
    endif()
    add_test(NAME credential_storage_security COMMAND config_security_tests)

    add_executable(endpoint_policy_tests
        tests/endpoint_policy_tests.cpp
        src/endpoint_policy.cpp
    )
    target_include_directories(endpoint_policy_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_options(endpoint_policy_tests PRIVATE -Wall -Wextra)
    add_test(NAME provider_endpoint_security COMMAND endpoint_policy_tests)

    add_executable(agent_session_tests
        tests/agent_session_tests.cpp
        src/agent_session.cpp
        src/context_manager.cpp
        src/token_tracker.cpp
        src/tool_policy.cpp
        src/config_manager.cpp
    )
    target_include_directories(agent_session_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_options(agent_session_tests PRIVATE -Wall -Wextra)
    add_test(NAME agent_model_tool_loop COMMAND agent_session_tests)

    add_executable(subagent_runner_tests
        tests/subagent_runner_tests.cpp
        src/subagent_runner.cpp
        src/agent_session.cpp
        src/context_manager.cpp
        src/token_tracker.cpp
        src/tool_policy.cpp
        src/config_manager.cpp
    )
    target_include_directories(subagent_runner_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_options(subagent_runner_tests PRIVATE -Wall -Wextra)
    add_test(NAME delegated_subagent_loop COMMAND subagent_runner_tests)

    add_executable(session_controls_tests
        tests/session_controls_tests.cpp
        src/session_controls.cpp
    )
    target_include_directories(session_controls_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_options(session_controls_tests PRIVATE -Wall -Wextra)
    add_test(NAME persistent_goal_memory_controls COMMAND session_controls_tests)

    add_executable(diff_view_tests
        tests/diff_view_tests.cpp
        src/diff_view.cpp
    )
    target_include_directories(diff_view_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_options(diff_view_tests PRIVATE -Wall -Wextra)
    add_test(NAME collapsed_diff_summary COMMAND diff_view_tests)

    add_executable(context_manager_tests
        tests/context_manager_tests.cpp
        src/context_manager.cpp
    )
    target_include_directories(context_manager_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_options(context_manager_tests PRIVATE -Wall -Wextra)
    add_test(NAME bounded_model_context COMMAND context_manager_tests)

    add_executable(model_provider_tests
        tests/model_provider_tests.cpp
        src/model_provider.cpp
        src/http_client.cpp
        src/http_retry_policy.cpp
        src/stream_parser.cpp
        src/endpoint_policy.cpp
    )
    target_include_directories(model_provider_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_options(model_provider_tests PRIVATE -Wall -Wextra)
    target_link_libraries(model_provider_tests PRIVATE CURL::libcurl Threads::Threads)
    add_test(NAME provider_adapters COMMAND model_provider_tests)

    add_executable(http_retry_policy_tests
        tests/http_retry_policy_tests.cpp
        src/http_retry_policy.cpp
    )
    target_include_directories(http_retry_policy_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_options(http_retry_policy_tests PRIVATE -Wall -Wextra)
    target_link_libraries(http_retry_policy_tests PRIVATE CURL::libcurl)
    add_test(NAME bounded_http_retries COMMAND http_retry_policy_tests)

endif()
