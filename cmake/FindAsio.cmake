# Provides target doiplite_asio (header-only standalone Asio).
# Override with -DASIO_ROOT=/path/to/asio (directory that contains include/asio.hpp).

if(TARGET doiplite_asio)
    return()
endif()

function(doiplite_add_asio include_dir)
    add_library(doiplite_asio INTERFACE)
    target_include_directories(doiplite_asio INTERFACE "${include_dir}")
    target_compile_definitions(doiplite_asio INTERFACE ASIO_STANDALONE ASIO_NO_DEPRECATED)
    find_package(Threads REQUIRED)
    target_link_libraries(doiplite_asio INTERFACE Threads::Threads)
endfunction()

if(ASIO_ROOT)
    if(NOT EXISTS "${ASIO_ROOT}/include/asio.hpp")
        message(FATAL_ERROR "ASIO_ROOT=${ASIO_ROOT} does not contain include/asio.hpp")
    endif()
    doiplite_add_asio("${ASIO_ROOT}/include")
    return()
endif()

include(FetchContent)
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()
FetchContent_Declare(
    asio
    URL https://github.com/chriskohlhoff/asio/archive/refs/tags/asio-1-32-0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_GetProperties(asio)
if(NOT asio_POPULATED)
    FetchContent_Populate(asio)
endif()

if(EXISTS "${asio_SOURCE_DIR}/include/asio.hpp")
    doiplite_add_asio("${asio_SOURCE_DIR}/include")
elseif(EXISTS "${asio_SOURCE_DIR}/asio/include/asio.hpp")
    doiplite_add_asio("${asio_SOURCE_DIR}/asio/include")
else()
    message(FATAL_ERROR "Fetched Asio source does not contain asio.hpp")
endif()
