# ---------------------------------------------------------------------------
# Local dependency prefix
# ---------------------------------------------------------------------------

Include(ExternalProject)

set(ANYBLOB_DEPS_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.deps" CACHE PATH "Dependency source, build, and install root")
set(ANYBLOB_DEPS_SOURCE_DIR "${ANYBLOB_DEPS_ROOT}/src")
set(ANYBLOB_DEPS_BUILD_DIR "${ANYBLOB_DEPS_ROOT}/build")
set(ANYBLOB_DEPS_INSTALL_DIR "${ANYBLOB_DEPS_ROOT}/prefix")
set(ANYBLOB_DEPS_UPDATE_DISCONNECTED OFF CACHE BOOL "Do not update downloaded dependencies")
set(ANYBLOB_JOBS 16 CACHE STRING "Parallel jobs for downloaded dependencies")
set(AWS_LC_GIT_TAG 75a73bfabf1be384b49c7f92da6fdfd9d867069e CACHE STRING "AWS-LC git tag or commit (1.3.0, pinned by AWS SDK 1.11.31)")
set(CURL_GIT_TAG 01346829096c61b372692f6dc43ffa778c6caccd CACHE STRING "curl git tag or commit (curl 8.22.0)")
set(ZLIB_GIT_TAG 51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf CACHE STRING "zlib git tag or commit (1.3.1)")
set(ANYBLOB_GIT_REPOSITORY https://github.com/durner/AnyBlob.git CACHE STRING "AnyBlob git repository")
set(ANYBLOB_GIT_TAG "" CACHE STRING "AnyBlob git tag or commit when not using a local source tree")
set(ANYBLOB_LOCAL_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../.." CACHE PATH "Use a local AnyBlob source tree instead of downloading it")
set(AWS_SDK_GIT_TAG ee48dbe5ba4be1f17fb24cae177a42ee5128e02d CACHE STRING "AWS SDK for C++ git tag or commit (1.11.31)")
set(AZURE_CORE_GIT_TAG b6e7a28c6200d50080c38a598cf92d96d45cf976 CACHE STRING "Azure Core SDK commit (1.15.0)")
set(AZURE_STORAGE_COMMON_GIT_TAG d693099d44c4f965cde6ff06d6f3cd0358ef01a8 CACHE STRING "Azure Storage Common SDK commit (12.3.2)")
set(AZURE_STORAGE_BLOBS_GIT_TAG 2850c5d32c8a86491b49e801433b8f186fa81745 CACHE STRING "Azure Storage Blobs SDK commit (12.7.0)")
set(LIBXML2_GIT_TAG d23960a130c5bb82779c9405fbbf85e65fb3c57c CACHE STRING "libxml2 commit (2.14.6)")

file(MAKE_DIRECTORY
    "${ANYBLOB_DEPS_SOURCE_DIR}"
    "${ANYBLOB_DEPS_BUILD_DIR}"
    "${ANYBLOB_DEPS_INSTALL_DIR}"
    "${ANYBLOB_DEPS_INSTALL_DIR}/include"
    "${ANYBLOB_DEPS_INSTALL_DIR}/lib")

find_package(Threads REQUIRED)

ExternalProject_Add(zlib
    GIT_REPOSITORY https://github.com/madler/zlib.git
    GIT_TAG ${ZLIB_GIT_TAG}
    SOURCE_DIR "${ANYBLOB_DEPS_SOURCE_DIR}/zlib"
    BINARY_DIR "${ANYBLOB_DEPS_BUILD_DIR}/zlib"
    STAMP_DIR "${ANYBLOB_DEPS_BUILD_DIR}/stamps/zlib"
    CMAKE_ARGS
        -DBUILD_SHARED_LIBS=OFF
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_INSTALL_LIBDIR=lib
        -DCMAKE_INSTALL_PREFIX=${ANYBLOB_DEPS_INSTALL_DIR}
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --parallel ${ANYBLOB_JOBS}
    INSTALL_COMMAND ${CMAKE_COMMAND} --install <BINARY_DIR>
    UPDATE_DISCONNECTED ${ANYBLOB_DEPS_UPDATE_DISCONNECTED})

ExternalProject_Add(aws-lc
    GIT_REPOSITORY https://github.com/aws/aws-lc.git
    GIT_TAG ${AWS_LC_GIT_TAG}
    SOURCE_DIR "${ANYBLOB_DEPS_SOURCE_DIR}/aws-lc"
    BINARY_DIR "${ANYBLOB_DEPS_BUILD_DIR}/aws-lc"
    STAMP_DIR "${ANYBLOB_DEPS_BUILD_DIR}/stamps/aws-lc"
    CMAKE_ARGS
        -DBUILD_LIBSSL=ON
        -DBUILD_SHARED_LIBS=OFF
        -DBUILD_TESTING=OFF
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_C_FLAGS=-Wno-error=stringop-overflow
        -DCMAKE_INSTALL_LIBDIR=lib
        -DCMAKE_INSTALL_PREFIX=${ANYBLOB_DEPS_INSTALL_DIR}
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --parallel ${ANYBLOB_JOBS}
    INSTALL_COMMAND ${CMAKE_COMMAND} --install <BINARY_DIR>
    UPDATE_DISCONNECTED ${ANYBLOB_DEPS_UPDATE_DISCONNECTED})

ExternalProject_Add(curl
    GIT_REPOSITORY https://github.com/curl/curl.git
    GIT_TAG ${CURL_GIT_TAG}
    SOURCE_DIR "${ANYBLOB_DEPS_SOURCE_DIR}/curl"
    BINARY_DIR "${ANYBLOB_DEPS_BUILD_DIR}/curl"
    STAMP_DIR "${ANYBLOB_DEPS_BUILD_DIR}/stamps/curl"
    DEPENDS aws-lc
    CMAKE_ARGS
        -DBUILD_CURL_EXE=OFF
        -DBUILD_EXAMPLES=OFF
        -DBUILD_LIBCURL_DOCS=OFF
        -DBUILD_MISC_DOCS=OFF
        -DBUILD_SHARED_LIBS=OFF
        -DBUILD_TESTING=OFF
        -DCURL_BROTLI=OFF
        -DCURL_USE_GSSAPI=OFF
        -DCURL_USE_LIBSSH=OFF
        -DCURL_USE_LIBSSH2=OFF
        -DCURL_USE_LIBPSL=OFF
        -DCURL_USE_OPENSSL=ON
        -DCURL_ZLIB=OFF
        -DCURL_ZSTD=OFF
        -DHTTP_ONLY=ON
        -DUSE_LIBIDN2=OFF
        -DUSE_LIBRTMP=OFF
        -DUSE_NGHTTP2=OFF
        -DOPENSSL_CRYPTO_LIBRARY=${ANYBLOB_DEPS_INSTALL_DIR}/lib/libcrypto.a
        -DOPENSSL_INCLUDE_DIR=${ANYBLOB_DEPS_INSTALL_DIR}/include
        -DOPENSSL_ROOT_DIR=${ANYBLOB_DEPS_INSTALL_DIR}
        -DOPENSSL_SSL_LIBRARY=${ANYBLOB_DEPS_INSTALL_DIR}/lib/libssl.a
        -DOPENSSL_USE_STATIC_LIBS=TRUE
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_INSTALL_LIBDIR=lib
        -DCMAKE_INSTALL_PREFIX=${ANYBLOB_DEPS_INSTALL_DIR}
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DCMAKE_PREFIX_PATH=${ANYBLOB_DEPS_INSTALL_DIR}
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --parallel ${ANYBLOB_JOBS}
    INSTALL_COMMAND ${CMAKE_COMMAND} --install <BINARY_DIR>
    UPDATE_DISCONNECTED ${ANYBLOB_DEPS_UPDATE_DISCONNECTED})

ExternalProject_Add(liburing
    GIT_REPOSITORY https://github.com/axboe/liburing.git
    GIT_TAG 80272cbeb42bcd0b39a75685a50b0009b77cd380
    SOURCE_DIR "${ANYBLOB_DEPS_SOURCE_DIR}/liburing"
    BINARY_DIR "${ANYBLOB_DEPS_BUILD_DIR}/liburing"
    STAMP_DIR "${ANYBLOB_DEPS_BUILD_DIR}/stamps/liburing"
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E chdir <SOURCE_DIR> <SOURCE_DIR>/configure --prefix=${ANYBLOB_DEPS_INSTALL_DIR}
    BUILD_COMMAND make -C <SOURCE_DIR> -j${ANYBLOB_JOBS}
    INSTALL_COMMAND make -C <SOURCE_DIR> install
    UPDATE_DISCONNECTED ${ANYBLOB_DEPS_UPDATE_DISCONNECTED})

ExternalProject_Add(jemalloc
    GIT_REPOSITORY https://github.com/jemalloc/jemalloc.git
    GIT_TAG 54eaed1d8b56b1aa528be3bdd1877e59c56fa90c
    SOURCE_DIR "${ANYBLOB_DEPS_SOURCE_DIR}/jemalloc"
    BINARY_DIR "${ANYBLOB_DEPS_BUILD_DIR}/jemalloc"
    STAMP_DIR "${ANYBLOB_DEPS_BUILD_DIR}/stamps/jemalloc"
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E chdir <SOURCE_DIR> <SOURCE_DIR>/autogen.sh --prefix=${ANYBLOB_DEPS_INSTALL_DIR}
    BUILD_COMMAND make -C <SOURCE_DIR> -j${ANYBLOB_JOBS}
    INSTALL_COMMAND make -C <SOURCE_DIR> install
    UPDATE_DISCONNECTED ${ANYBLOB_DEPS_UPDATE_DISCONNECTED})

add_custom_target(anyblob-dependencies DEPENDS aws-lc curl zlib liburing jemalloc)

add_library(ZLIB::ZLIB STATIC IMPORTED GLOBAL)
set_target_properties(ZLIB::ZLIB PROPERTIES
    IMPORTED_LOCATION "${ANYBLOB_DEPS_INSTALL_DIR}/lib/libz.a"
    INTERFACE_INCLUDE_DIRECTORIES "${ANYBLOB_DEPS_INSTALL_DIR}/include")
add_dependencies(ZLIB::ZLIB zlib)

add_library(OpenSSL::Crypto STATIC IMPORTED GLOBAL)
set_target_properties(OpenSSL::Crypto PROPERTIES
    IMPORTED_LOCATION "${ANYBLOB_DEPS_INSTALL_DIR}/lib/libcrypto.a"
    INTERFACE_INCLUDE_DIRECTORIES "${ANYBLOB_DEPS_INSTALL_DIR}/include")
add_dependencies(OpenSSL::Crypto aws-lc)

add_library(OpenSSL::SSL STATIC IMPORTED GLOBAL)
set_target_properties(OpenSSL::SSL PROPERTIES
    IMPORTED_LOCATION "${ANYBLOB_DEPS_INSTALL_DIR}/lib/libssl.a"
    INTERFACE_INCLUDE_DIRECTORIES "${ANYBLOB_DEPS_INSTALL_DIR}/include"
    INTERFACE_LINK_LIBRARIES "OpenSSL::Crypto;Threads::Threads;dl")
add_dependencies(OpenSSL::SSL aws-lc)

add_library(CURL::libcurl STATIC IMPORTED GLOBAL)
set_target_properties(CURL::libcurl PROPERTIES
    IMPORTED_LOCATION "${ANYBLOB_DEPS_INSTALL_DIR}/lib/libcurl.a"
    INTERFACE_INCLUDE_DIRECTORIES "${ANYBLOB_DEPS_INSTALL_DIR}/include"
    INTERFACE_LINK_LIBRARIES "OpenSSL::SSL;OpenSSL::Crypto;Threads::Threads;dl")
add_dependencies(CURL::libcurl curl)