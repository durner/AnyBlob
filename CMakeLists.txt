cmake_minimum_required(VERSION 3.8)

project(AnyBlob)

# ---------------------------------------------------------------------------
# OS
# ---------------------------------------------------------------------------

if (NOT UNIX)
    message(STATUS FATAL_ERROR "Unsupported OS")
endif (NOT UNIX)


# ---------------------------------------------------------------------------
# Configure OpenSSL and Threads
# ---------------------------------------------------------------------------

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

# ---------------------------------------------------------------------------
# Environment
# ---------------------------------------------------------------------------

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# use lld
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=lld")
set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -fuse-ld=lld")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fuse-ld=lld")

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address,undefined -Wall -Wextra -Wold-style-cast -Woverloaded-virtual -Wmissing-declarations -Wno-unqualified-std-cast-call -Wvla -Wconversion -Werror")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -march=native -Wall -Wextra -Wold-style-cast -Woverloaded-virtual -Wmissing-declarations -Wno-unqualified-std-cast-call -Wvla -Wconversion -Werror")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -g -march=native -Wall -Wextra -Wold-style-cast -Woverloaded-virtual -Wmissing-declarations -Wno-unqualified-std-cast-call -Wvla -Wconversion -Werror")
set(CMAKE_CXX_FLAGS_COVERAGE "${CMAKE_CXX_FLAGS_DEBUG} -Wall -Wextra -Wold-style-cast -Woverloaded-virtual -Wmissing-declarations -Wno-unqualified-std-cast-call -Wvla -Wconversion -Werror -coverage")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
   # Default to Release build type, similar to Makefile
   set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
   set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "Coverage")
endif()

option(ANYBLOB_LIBCXX_COMPAT "Build AnyBlob in a way that's compatible with libcxx." OFF)
if (ANYBLOB_LIBCXX_COMPAT)
    message("Building AnyBlob with libcxx")
    # Update compiler flags to use libcxx as standard library.
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -stdlib=libc++")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -stdlib=libc++")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -stdlib=libc++")
    set(CMAKE_CXX_FLAGS_COVERAGE "${CMAKE_CXX_FLAGS_COVERAGE} -stdlib=libc++")
endif()

# ---------------------------------------------------------------------------
# Coverage Environment
# ---------------------------------------------------------------------------

if(CMAKE_BUILD_TYPE MATCHES "Coverage")
  #find required tools
  find_program(LCOV lcov REQUIRED)
  find_program(GENHTML genhtml REQUIRED)

  # add coverage target
  add_custom_target(coverage
    # gather data
    COMMAND ${LCOV} --capture --initial --directory . --output-file coverage.base
    COMMAND ./tester
    COMMAND ${LCOV} --capture --directory . --output-file coverage.test
    COMMAND lcov --add-tracefile coverage.base --add-tracefile coverage.test --output-file coverage.info
    COMMAND ${LCOV} --directory . --remove coverage.info '/usr*' '/*thirdparty/*' '/*test/*' -o coverage.info.reduced

    # generate report
    COMMAND ${GENHTML} --demangle-cpp -o coverage coverage.info.reduced
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
endif()

# ---------------------------------------------------------------------------
# Uring
# ---------------------------------------------------------------------------

find_path(LIBURING_INCLUDE_DIR NAMES liburing.h)
mark_as_advanced(LIBURING_INCLUDE_DIR)

find_library(LIBURING_LIBRARY NAMES uring)
mark_as_advanced(LIBURING_LIBRARY)

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(
        LIBURING
        REQUIRED_VARS LIBURING_LIBRARY LIBURING_INCLUDE_DIR)

if(LIBURING_FOUND)
  set(LIBURING_LIBRARIES ${LIBURING_LIBRARY})
  set(LIBURING_INCLUDE_DIRS ${LIBURING_INCLUDE_DIR})
endif()

# ---------------------------------------------------------------------------
# Includes
# ---------------------------------------------------------------------------

include("${PROJECT_SOURCE_DIR}/include/local.cmake")
include_directories(
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/test/unit
    ${PROJECT_SOURCE_DIR}/test/integration
    ${PROJECT_SOURCE_DIR}/thirdparty
)

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------

include("${PROJECT_SOURCE_DIR}/src/local.cmake")
include("${PROJECT_SOURCE_DIR}/test/unit/local.cmake")
include("${PROJECT_SOURCE_DIR}/test/integration/local.cmake")

# ---------------------------------------------------------------------------
# Library
# ---------------------------------------------------------------------------

add_library(AnyBlob STATIC ${SRC_CPP} ${INCLUDE_HPP})
target_link_libraries(AnyBlob PUBLIC OpenSSL::SSL Threads::Threads ${LIBURING_LIBRARY} jemalloc)
target_include_directories(AnyBlob PUBLIC ${PROJECT_SOURCE_DIR}/include)
if (ANYBLOB_LIBCXX_COMPAT)
    target_compile_definitions(AnyBlob PRIVATE ANYBLOB_LIBCXX_COMPAT)
endif()

# ---------------------------------------------------------------------------
# Unit tests
# ---------------------------------------------------------------------------

add_executable(tester test/unit/test.cpp ${TEST_CPP})
target_link_libraries(tester AnyBlob)

# ---------------------------------------------------------------------------
# Integration tests
# ---------------------------------------------------------------------------

add_executable(integration test/integration/test.cpp ${INTEGRATION_CPP})
target_link_libraries(integration AnyBlob)
