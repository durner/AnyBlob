cmake_minimum_required(VERSION 3.18)

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

include("${PROJECT_SOURCE_DIR}/thirdparty/dependencies.cmake")

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
# Azure SDK
# ---------------------------------------------------------------------------

include("${PROJECT_SOURCE_DIR}/thirdparty/azuresdk.cmake")

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
set(ANYBLOB_BENCHMARK_LINK_LIBRARIES
    AnyBlob
    ${AWS_LINK_LIBRARIES}
    Azure::azure-storage-blobs
    ${ANYBLOB_DEPS_INSTALL_DIR}/lib/libjemalloc.so
    dl)
target_link_libraries(AnyBlobBenchmark ${ANYBLOB_BENCHMARK_LINK_LIBRARIES})

enable_testing()
add_executable(AzureSdkBackendTest test/azure_sdk_backend_test.cpp ${SRC_CPP} ${INCLUDE_HPP})
target_link_libraries(AzureSdkBackendTest ${ANYBLOB_BENCHMARK_LINK_LIBRARIES})
add_test(NAME AzureSdkBackendTest COMMAND AzureSdkBackendTest)
