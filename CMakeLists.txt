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
    src/doip/session.cpp
    src/uds/read_did.cpp
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
    add_executable(doiplite-test-api tests/test_api.cpp)
    target_link_libraries(doiplite-test-api PRIVATE doiplite)
    add_test(NAME doiplite-test-api COMMAND doiplite-test-api)
endif()
