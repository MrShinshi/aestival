cmake_minimum_required(VERSION 3.20...4.3.1)

# Prefer modern Find/Config behavior for boost (suppresses CMP0167 warning)
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

# ══════════════════════════════════════════════════════════════════════════════
# vcpkg auto‑bootstrap
# When vcpkg is not on the machine this block downloads & bootstraps a local
# copy into  dependencies/vcpkg, then re‑invokes cmake with the proper
# toolchain so the rest of the file runs in a normal vcpkg‑aware environment.
# ══════════════════════════════════════════════════════════════════════════════
if(NOT VCPKG_BOOTSTRAPPED AND NOT CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg\\.cmake$")
    # --- resolve vcpkg root ------------------------------------------------
    set(vcpkg_root_from_env OFF)
    if(DEFINED ENV{VCPKG_ROOT} AND NOT "$ENV{VCPKG_ROOT}" STREQUAL "")
        set(vcpkg_root_from_env ON)
        set(VCPKG_ROOT "$ENV{VCPKG_ROOT}" CACHE PATH "vcpkg root directory")
    elseif(NOT DEFINED VCPKG_ROOT OR VCPKG_ROOT STREQUAL "")
        set(VCPKG_ROOT "${CMAKE_SOURCE_DIR}/dependencies/vcpkg"
            CACHE PATH "vcpkg root directory")
    endif()

    if(WIN32)
        set(vcpkg_exe "${VCPKG_ROOT}/vcpkg.exe")
    else()
        set(vcpkg_exe "${VCPKG_ROOT}/vcpkg")
    endif()

    # --- acquire checkout only when the root is missing --------------------
    if(NOT EXISTS "${VCPKG_ROOT}")
        message(STATUS "vcpkg checkout not found — acquiring into")
        message(STATUS "  ${VCPKG_ROOT}")

        # 1) try git clone (fast, small download)
        find_package(Git QUIET)
        if(GIT_FOUND)
            execute_process(
                COMMAND "${GIT_EXECUTABLE}" clone --depth 1
                        "https://github.com/microsoft/vcpkg.git"
                        "${VCPKG_ROOT}"
                RESULT_VARIABLE git_result
            )
        endif()

        # 2) fallback to zip download
        if(NOT GIT_FOUND OR NOT git_result EQUAL 0)
            set(zip "${CMAKE_BINARY_DIR}/vcpkg-master.zip")
            set(extract_root "${CMAKE_BINARY_DIR}/vcpkg-bootstrap")
            set(extracted_vcpkg "${extract_root}/vcpkg-master")
            file(REMOVE_RECURSE "${extract_root}")
            file(MAKE_DIRECTORY "${extract_root}")
            message(STATUS "git unavailable — downloading vcpkg zip …")
            file(DOWNLOAD
                "https://github.com/microsoft/vcpkg/archive/refs/heads/master.zip"
                "${zip}"
                SHOW_PROGRESS STATUS dl_status
            )
            list(GET dl_status 0 dl_code)
            if(NOT dl_code EQUAL 0)
                message(FATAL_ERROR "Cannot download vcpkg (status ${dl_code})")
            endif()
            file(ARCHIVE_EXTRACT INPUT "${zip}"
                 DESTINATION "${extract_root}")
            file(RENAME "${extracted_vcpkg}" "${VCPKG_ROOT}")
            file(REMOVE "${zip}")
            file(REMOVE_RECURSE "${extract_root}")
        endif()
    endif()

    # --- bootstrap an existing checkout when missing -----------------------
    if(NOT EXISTS "${vcpkg_exe}")
        message(STATUS "vcpkg executable not found — bootstrapping into")
        message(STATUS "  ${VCPKG_ROOT}")

        if(WIN32)
            set(bootstrap "${VCPKG_ROOT}/bootstrap-vcpkg.bat")
        else()
            set(bootstrap "${VCPKG_ROOT}/bootstrap-vcpkg.sh")
        endif()
        if(NOT EXISTS "${bootstrap}")
            message(FATAL_ERROR "Existing vcpkg checkout at ${VCPKG_ROOT} is missing ${bootstrap}")
        endif()

        message(STATUS "Bootstrapping vcpkg …")
        execute_process(
            COMMAND "${bootstrap}"
            WORKING_DIRECTORY "${VCPKG_ROOT}"
            RESULT_VARIABLE bs_result
        )
        if(NOT bs_result EQUAL 0)
            message(FATAL_ERROR "vcpkg bootstrap failed")
        endif()
    endif()

    # --- re‑invoke cmake with the vcpkg toolchain ---------------------------
    set(vcpkg_tc "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    set(reinvoke_args)
    set(skip_next OFF)
    math(EXPR last "${CMAKE_ARGC} - 1")
    foreach(i RANGE 1 ${last})
        if(skip_next)
            set(skip_next OFF)
            continue()
        endif()

        set(a "${CMAKE_ARGV${i}}")
        if(a STREQUAL "-S" OR a STREQUAL "--source" OR
           a STREQUAL "-B" OR a STREQUAL "--build" OR
           a STREQUAL "--preset")
            set(skip_next ON)
            continue()
        endif()
        if(a STREQUAL "--fresh")
            continue()
        endif()
        if(a MATCHES "^-D(VCPKG_ROOT|CMAKE_TOOLCHAIN_FILE|VCPKG_BOOTSTRAPPED)=")
            continue()
        endif()
        list(APPEND reinvoke_args "${a}")
    endforeach()

    execute_process(
        COMMAND "${CMAKE_COMMAND}"
                "-DCMAKE_TOOLCHAIN_FILE=${vcpkg_tc}"
                "-DVCPKG_BOOTSTRAPPED=ON"
                "-S" "${CMAKE_SOURCE_DIR}"
                "-B" "${CMAKE_BINARY_DIR}"
                ${reinvoke_args}
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        COMMAND_ERROR_IS_FATAL ANY
    )
    return()
endif()

project(aestival VERSION 1.0.0 DESCRIPTION "aestival" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_SUPPRESS_REGENERATION ON)
set(CMAKE_SKIP_INSTALL_RULES OFF)

find_package(Boost REQUIRED COMPONENTS regex locale)
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(magic_enum CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG QUIET)
if(NOT unofficial-sqlite3_FOUND)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SQLITE3 REQUIRED IMPORTED_TARGET sqlite3)
endif()

set(PCH_HEADER ${CMAKE_SOURCE_DIR}/app/stdafx.h)

# ─── sources per layer ────────────────────────────────────────────────────

set(APP_SOURCES    app/main.cpp app/llm_adapter.cpp app/im_adapter.cpp)
set(APP_HEADERS   app/stdafx.h)

set(CORE_AGENT     core/src/agent_controller.cpp)
set(CORE_AGENT_H   core/include/agent_controller.h)
set(CORE_CONFIG    core/config/bot_config.cpp)
set(CORE_CONFIG_H  core/config/bot_config.h)
set(CORE_SEARCH    core/src/agent_reach_client.cpp)
set(CORE_SEARCH_H  core/include/agent_reach_client.h core/include/agent_reach_plugin.h)
set(CORE_CONTEXT   core/src/chat_context_manager.cpp)
set(CORE_CONTEXT_H core/include/chat_context_manager.h core/include/chat_storage_sqlite.h)
set(CORE_POLICY    core/src/policy_engine.cpp)
set(CORE_POLICY_H  core/include/policy_engine.h)
set(CORE_SELF_ITER core/src/self_iteration.cpp)
set(CORE_SELF_ITER_H core/include/self_iteration.h core/include/self_iteration_prompts.h)
set(CORE_COMMAND   core/src/system_command_handler.cpp core/src/worker_pool.cpp)
set(CORE_COMMAND_H core/include/system_command_handler.h core/include/worker_pool.h)
set(CORE_REGISTRY   core/src/agent_registry.cpp)
set(CORE_REGISTRY_H core/include/agent_registry.h core/include/agent_instance.h
                    core/include/platform_session.h)
set(CORE_MGMT      core/src/management_api.cpp)
set(CORE_MGMT_H   core/include/management_api.h core/include/management_auth.h)
set(CORE_UTIL      core/src/log.cpp)
set(CORE_UTIL_H    core/include/log.h core/include/encode_utils.h
                   core/include/console_api.h core/include/token_counter.h)
set(CORE_INCLUDE_H core/include/bot_messaging.h core/include/message_types.h
                   core/include/model_client.h core/include/plugin.h
                   core/include/plugin_manager.h core/include/runtime_mode.h
                   core/include/simple_test_plugin.h core/include/tool_registry.h)

set(PLAT_LLM       platform/src/deepseek.cpp platform/src/openai.cpp)
set(PLAT_LLM_H     platform/include/platform/deepseek.h platform/include/platform/openai.h)
set(PLAT_HTTP      platform/src/http_client.cpp)
set(PLAT_HTTP_H    platform/include/platform/http_client.h
                   platform/include/platform/detail/http_utils.h
                   platform/include/platform/detail/sanitize.h)
set(PLAT_WS        platform/src/ws/ws_client.cpp platform/src/ws/ws_utils.cpp)
set(PLAT_WS_H      platform/include/platform/ws/ws_client.h
                   platform/include/platform/ws/ws_utils.h)
set(PLAT_QQ_CORE   platform/src/qq/session.cpp platform/src/qq/transport.cpp
                   platform/src/qq/auth.cpp platform/src/qq/dispatch.cpp
                   platform/src/qq/run.cpp)
set(PLAT_QQ_CORE_H platform/include/platform/qq/session.h
                   platform/include/platform/qq/dispatch.h
                   platform/include/platform/qq/api.h
                   platform/include/platform/qq/types.h)
set(PLAT_QQ_API    platform/src/qq/api.cpp platform/src/qq/api_guild.cpp
                   platform/src/qq/api_messaging.cpp)
set(PLAT_LOG_H     platform/include/platform/log.h)

set(ALL_SOURCES
    ${APP_SOURCES}    ${APP_HEADERS}
    ${CORE_AGENT}     ${CORE_AGENT_H}
    ${CORE_CONFIG}    ${CORE_CONFIG_H}
    ${CORE_SEARCH}    ${CORE_SEARCH_H}
    ${CORE_CONTEXT}   ${CORE_CONTEXT_H}
    ${CORE_POLICY}    ${CORE_POLICY_H}
    ${CORE_SELF_ITER} ${CORE_SELF_ITER_H}
    ${CORE_COMMAND}   ${CORE_COMMAND_H}
    ${CORE_REGISTRY}   ${CORE_REGISTRY_H}
    ${CORE_MGMT}      ${CORE_MGMT_H}
    ${CORE_UTIL}      ${CORE_UTIL_H}
    ${CORE_INCLUDE_H}
    ${PLAT_LLM}       ${PLAT_LLM_H}
    ${PLAT_HTTP}      ${PLAT_HTTP_H}
    ${PLAT_WS}        ${PLAT_WS_H}
    ${PLAT_QQ_CORE}   ${PLAT_QQ_CORE_H}
    ${PLAT_QQ_API}
    ${PLAT_LOG_H}
)

add_executable(${PROJECT_NAME} ${ALL_SOURCES})
# PCH via CMake target_precompile_headers.
# NOTE: VS will show an auto-generated "Precompile Header File" folder for
# cmake_pch.cxx — this is cosmetic, built-in CMake behavior, not removable.
target_precompile_headers(${PROJECT_NAME} PRIVATE ${PCH_HEADER})

# Output to bin/ in source tree; build/ stays as intermediate dir
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY           "${CMAKE_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE   "${CMAKE_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG     "${CMAKE_SOURCE_DIR}/bin"
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/app
    ${CMAKE_SOURCE_DIR}/core/config
    ${CMAKE_SOURCE_DIR}/core/include
    ${CMAKE_SOURCE_DIR}/platform/include
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Boost::regex Boost::locale
    OpenSSL::SSL OpenSSL::Crypto
    nlohmann_json::nlohmann_json magic_enum::magic_enum
    $<IF:$<BOOL:${unofficial-sqlite3_FOUND}>,unofficial::sqlite3::sqlite3,PkgConfig::SQLITE3>
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Release>:NDEBUG> $<$<CONFIG:Debug>:_DEBUG>
)

if(MSVC)
    target_compile_definitions(${PROJECT_NAME} PRIVATE _WIN32_WINNT=0x0A00 NOMINMAX)
    target_compile_options(${PROJECT_NAME} PRIVATE /W3 /utf-8 /Zc:__cplusplus)
    target_link_options(${PROJECT_NAME} PRIVATE /SUBSYSTEM:CONSOLE)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(${PROJECT_NAME} PRIVATE pthread)
endif()

# ─── IDE source_group (filters in VS / CLion) ──────────────────────────────

source_group("app"                                       FILES ${APP_SOURCES}    ${APP_HEADERS})
source_group("core\\agent"                               FILES ${CORE_AGENT}     ${CORE_AGENT_H})
source_group("core\\config"                              FILES ${CORE_CONFIG}    ${CORE_CONFIG_H})
source_group("core\\search"                              FILES ${CORE_SEARCH}    ${CORE_SEARCH_H})
source_group("core\\context"                             FILES ${CORE_CONTEXT}   ${CORE_CONTEXT_H})
source_group("core\\policy"                              FILES ${CORE_POLICY}    ${CORE_POLICY_H})
source_group("core\\self_iteration"                      FILES ${CORE_SELF_ITER} ${CORE_SELF_ITER_H})
source_group("core\\command"                             FILES ${CORE_COMMAND}   ${CORE_COMMAND_H})
source_group("core\registry"                           FILES ${CORE_REGISTRY}   ${CORE_REGISTRY_H})
source_group("core\management"                          FILES ${CORE_MGMT}      ${CORE_MGMT_H})
source_group("core\\include"                             FILES ${CORE_INCLUDE_H})
source_group("core\\util"                                FILES ${CORE_UTIL}      ${CORE_MGMT_H}
	    ${CORE_UTIL}      ${CORE_UTIL_H})
source_group("platform\\llm"                             FILES ${PLAT_LLM}       ${PLAT_LLM_H})
source_group("platform\\http"                            FILES ${PLAT_HTTP}      ${PLAT_HTTP_H})
source_group("platform\\websocket"                       FILES ${PLAT_WS}        ${PLAT_WS_H})
source_group("platform\\qq\\core"                        FILES ${PLAT_QQ_CORE}   ${PLAT_QQ_CORE_H})
source_group("platform\\qq\\api"                         FILES ${PLAT_QQ_API})
source_group("platform"                                  FILES ${PLAT_LOG_H})

# ─── post-build: copy runtime assets ──────────────────────────────────────

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/contexts"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/config"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/bot_config.json"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/config/bot_config.json"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/workspace"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/workspace"
)

