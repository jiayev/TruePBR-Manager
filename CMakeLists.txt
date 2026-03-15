cmake_minimum_required(VERSION 3.21)
project(TruePBR-Manager VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# vcpkg x64-windows ships only release libraries (/MD).
# Force release CRT in all configurations to avoid CRT mismatch heap corruption.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

# Qt auto-processing
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# --- Version info: git hash + build timestamp ---
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT GIT_HASH)
    set(GIT_HASH "unknown")
endif()

string(TIMESTAMP BUILD_TIMESTAMP "%Y-%m-%d %H:%M:%S" UTC)

configure_file(
    "${CMAKE_SOURCE_DIR}/src/Version.h.in"
    "${CMAKE_BINARY_DIR}/generated/Version.h"
    @ONLY
)

add_subdirectory(src)
