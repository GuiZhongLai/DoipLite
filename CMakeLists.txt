cmake_minimum_required(VERSION 3.16)
project(DoipLite VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(DOIPLITE_BUILD_CLI "Build doiplite-cli" ON)
option(DOIPLITE_BUILD_TESTS "Build unit/smoke tests" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(FindAsio)

add_library(doiplite
    src/status.cpp
    src/net/endpoint.cpp
    src/net/channel.cpp
    src/doip/protocol.cpp
    src/doip/session.cpp
    src/uds/read_did.cpp
    src/uds/session_control.cpp
    src/flash/flash.cpp
)
add_library(DoipLite::doiplite ALIAS doiplite)

target_include_directories(doiplite
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(doiplite PRIVATE doiplite_asio)

if(WIN32)
    target_compile_definitions(doiplite PRIVATE _WIN32_WINNT=0x0A00)
    target_link_libraries(doiplite PRIVATE ws2_32)
endif()

if(DOIPLITE_BUILD_CLI)
    add_executable(doiplite-cli tools/doiplite-cli/main.cpp)
    target_link_libraries(doiplite-cli PRIVATE doiplite)
endif()

if(DOIPLITE_BUILD_TESTS)
    enable_testing()
    add_executable(doiplite-test-api
        tests/test_api.cpp
        tests/loopback_entity.cpp
    )
    target_link_libraries(doiplite-test-api PRIVATE doiplite doiplite_asio)
    target_include_directories(doiplite-test-api PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
    if(WIN32)
        target_compile_definitions(doiplite-test-api PRIVATE _WIN32_WINNT=0x0A00)
        target_link_libraries(doiplite-test-api PRIVATE ws2_32)
    endif()
    if(DOIPLITE_BUILD_CLI)
        target_compile_definitions(doiplite-test-api PRIVATE
            "DOIPLITE_CLI_PATH=\"$<TARGET_FILE:doiplite-cli>\""
        )
        add_dependencies(doiplite-test-api doiplite-cli)
    endif()
    add_test(NAME doiplite-test-api COMMAND doiplite-test-api)

    add_executable(doiplite-loopback-sim
        tests/loopback_sim_main.cpp
        tests/loopback_entity.cpp
    )
    target_link_libraries(doiplite-loopback-sim PRIVATE doiplite_asio)
    target_include_directories(doiplite-loopback-sim PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
    if(WIN32)
        target_compile_definitions(doiplite-loopback-sim PRIVATE _WIN32_WINNT=0x0A00)
        target_link_libraries(doiplite-loopback-sim PRIVATE ws2_32)
    endif()
endif()
