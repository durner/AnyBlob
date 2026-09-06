# ---------------------------------------------------------------------------
# AnyBlob Include CMake
#
# This should be used to include AnyBlob as static library for your project
# ---------------------------------------------------------------------------

Include(ExternalProject)

if(ANYBLOB_LOCAL_SOURCE_DIR)
  set(ANYBLOB_SOURCE_DIR "${ANYBLOB_LOCAL_SOURCE_DIR}")
  set(ANYBLOB_DOWNLOAD_ARGS DOWNLOAD_COMMAND "" UPDATE_COMMAND "")
else()
  if(NOT ANYBLOB_GIT_TAG)
    message(FATAL_ERROR "Set ANYBLOB_GIT_TAG to an immutable commit when ANYBLOB_LOCAL_SOURCE_DIR is empty")
  endif()
  set(ANYBLOB_SOURCE_DIR "${ANYBLOB_DEPS_SOURCE_DIR}/anyblob")
  set(ANYBLOB_DOWNLOAD_ARGS
    GIT_REPOSITORY "${ANYBLOB_GIT_REPOSITORY}"
    GIT_TAG "${ANYBLOB_GIT_TAG}")
endif()
set(ANYBLOB_BINARY_DIR "${ANYBLOB_DEPS_BUILD_DIR}/anyblob")

# ---------------------------------------------------------------------------
# Get AnyBlob
# ---------------------------------------------------------------------------

ExternalProject_Add(anyblob
  ${ANYBLOB_DOWNLOAD_ARGS}
    PREFIX              "${ANYBLOB_DEPS_BUILD_DIR}/external/anyblob"
    SOURCE_DIR          "${ANYBLOB_SOURCE_DIR}"
    BINARY_DIR          "${ANYBLOB_BINARY_DIR}"
    STAMP_DIR           "${ANYBLOB_DEPS_BUILD_DIR}/stamps/anyblob"
  INSTALL_COMMAND     ""
    DEPENDS             anyblob-dependencies
  CMAKE_ARGS
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DCMAKE_PREFIX_PATH=${ANYBLOB_DEPS_INSTALL_DIR}
        -DCMAKE_INCLUDE_PATH=${ANYBLOB_DEPS_INSTALL_DIR}/include
        -DCMAKE_LIBRARY_PATH=${ANYBLOB_DEPS_INSTALL_DIR}/lib
        -DCMAKE_EXE_LINKER_FLAGS=-L${ANYBLOB_DEPS_INSTALL_DIR}/lib
        -DCMAKE_SHARED_LINKER_FLAGS=-L${ANYBLOB_DEPS_INSTALL_DIR}/lib
        -DOPENSSL_CRYPTO_LIBRARY=${ANYBLOB_DEPS_INSTALL_DIR}/lib/libcrypto.a
        -DOPENSSL_INCLUDE_DIR=${ANYBLOB_DEPS_INSTALL_DIR}/include
        -DOPENSSL_ROOT_DIR=${ANYBLOB_DEPS_INSTALL_DIR}
        -DOPENSSL_SSL_LIBRARY=${ANYBLOB_DEPS_INSTALL_DIR}/lib/libssl.a
        -DOPENSSL_USE_STATIC_LIBS=TRUE
        -DANYBLOB_BUILD_BENCHMARK=OFF
        -DLIBURING_INCLUDE_DIR=${ANYBLOB_DEPS_INSTALL_DIR}/include
        -DLIBURING_LIBRARY=${ANYBLOB_DEPS_INSTALL_DIR}/lib/liburing.a
      CMAKE_CACHE_ARGS
        -DCMAKE_CXX_FLAGS:STRING=${CMAKE_CXX_FLAGS}\ -isystem\ ${ANYBLOB_DEPS_INSTALL_DIR}/include
)

# ---------------------------------------------------------------------------
# Get SOURCE and BINARY DIR
# ---------------------------------------------------------------------------

ExternalProject_Get_Property(anyblob SOURCE_DIR)
ExternalProject_Get_Property(anyblob BINARY_DIR)

set(ANYBLOB_INCLUDE_DIR ${SOURCE_DIR}/include)
file(MAKE_DIRECTORY ${ANYBLOB_INCLUDE_DIR})

# ---------------------------------------------------------------------------
# Configure OpenSSL, Threads, and Uring
# ---------------------------------------------------------------------------

set(LIBURING_INCLUDE_DIR "${ANYBLOB_DEPS_INSTALL_DIR}/include")
set(LIBURING_LIBRARY "${ANYBLOB_DEPS_INSTALL_DIR}/lib/liburing.a")
set(LIBURING_FOUND TRUE)

# ---------------------------------------------------------------------------
# Build Library with dependencies
# ---------------------------------------------------------------------------

add_library(AnyBlob STATIC IMPORTED)
set_property(TARGET AnyBlob PROPERTY IMPORTED_LOCATION ${BINARY_DIR}/libAnyBlob.a)
set_property(TARGET AnyBlob APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${ANYBLOB_INCLUDE_DIR} ${LIBURING_INCLUDE_DIR})
add_dependencies(AnyBlob anyblob)
target_link_libraries(AnyBlob INTERFACE OpenSSL::SSL Threads::Threads ${ANYBLOB_DEPS_INSTALL_DIR}/lib/libjemalloc.so)
if (ANYBLOB_LIBCXX_COMPAT)
    target_compile_definitions(AnyBlob INTERFACE ANYBLOB_LIBCXX_COMPAT)
endif()
if(LIBURING_FOUND AND NOT ANYBLOB_URING_COMPAT)
    target_compile_definitions(AnyBlob INTERFACE ANYBLOB_HAS_IO_URING)
    target_link_libraries(AnyBlob INTERFACE ${LIBURING_LIBRARY})
endif()
