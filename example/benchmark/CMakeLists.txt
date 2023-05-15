cmake_minimum_required(VERSION 3.8)

project(AnyBlobBenchmark)

# ---------------------------------------------------------------------------
# Environment
# ---------------------------------------------------------------------------

if (NOT UNIX)
    message(STATUS FATAL_ERROR "Unsupported OS")
endif (NOT UNIX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address -Wall -fsanitize=undefined")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -march=native -Wall")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -g -march=native -Wall")
add_compile_options(-Wall -Wextra -Werror)

# ---------------------------------------------------------------------------
# AnyBlob
# ---------------------------------------------------------------------------

include("${PROJECT_SOURCE_DIR}/thirdparty/anyblob.cmake")

# ---------------------------------------------------------------------------
# AWS
# ---------------------------------------------------------------------------

include("${PROJECT_SOURCE_DIR}/thirdparty/awssdk.cmake")

# ---------------------------------------------------------------------------
# Includes
# ---------------------------------------------------------------------------

include_directories(SYSTEM ${ANYBLOB_INCLUDE_DIR})
include("${PROJECT_SOURCE_DIR}/include/local.cmake")
include_directories(
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/thirdparty
)

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------

include("${PROJECT_SOURCE_DIR}/src/local.cmake")

# ---------------------------------------------------------------------------
# Executable
# ---------------------------------------------------------------------------

add_executable(AnyBlobBenchmark src/main.cpp ${SRC_CPP} ${INCLUDE_HPP})
target_link_libraries(AnyBlobBenchmark AnyBlob ${AWS_LINK_LIBRARIES} jemalloc dl)
