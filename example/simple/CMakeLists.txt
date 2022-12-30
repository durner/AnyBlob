cmake_minimum_required(VERSION 3.8)

project(SimpleAnyBlob)

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
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -fsanitize=address -march=native -Wall")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -g -march=native -Wall")
add_compile_options(-Wall -Wextra -Werror)

# ---------------------------------------------------------------------------
# AnyBlob
# ---------------------------------------------------------------------------

include("${PROJECT_SOURCE_DIR}/anyblob.cmake")

# ---------------------------------------------------------------------------
# Includes
# ---------------------------------------------------------------------------

include_directories(SYSTEM ${ANYBLOB_INCLUDE_DIR})

# ---------------------------------------------------------------------------
# Executable
# ---------------------------------------------------------------------------

add_executable(SimpleAnyBlob main.cpp ${INCLUDE_HPP})
target_link_libraries(SimpleAnyBlob AnyBlob)
