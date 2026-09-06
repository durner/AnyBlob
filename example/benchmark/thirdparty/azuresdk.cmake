# ---------------------------------------------------------------------------
# Azure Storage SDK
# ---------------------------------------------------------------------------

include(ExternalProject)
find_package(Git REQUIRED)

file(SHA256 "${CMAKE_CURRENT_LIST_DIR}/apply-patch.cmake" AZURE_PATCH_DRIVER_SHA256)
file(SHA256 "${CMAKE_CURRENT_LIST_DIR}/patches/azure-core-awslc.patch" AZURE_CORE_AWSLC_PATCH_SHA256)
file(SHA256 "${CMAKE_CURRENT_LIST_DIR}/patches/azure-core-no-transport-retry.patch" AZURE_CORE_RETRY_PATCH_SHA256)
file(SHA256 "${CMAKE_CURRENT_LIST_DIR}/patches/azure-storage-no-reliable-stream-retry.patch" AZURE_STORAGE_RETRY_PATCH_SHA256)
string(
    SHA256 AZURE_SDK_GENERATION
    "${AZURE_CORE_GIT_TAG};${AZURE_STORAGE_COMMON_GIT_TAG};${AZURE_STORAGE_BLOBS_GIT_TAG};${LIBXML2_GIT_TAG};${AZURE_PATCH_DRIVER_SHA256};${AZURE_CORE_AWSLC_PATCH_SHA256};${AZURE_CORE_RETRY_PATCH_SHA256};${AZURE_STORAGE_RETRY_PATCH_SHA256};${CMAKE_C_COMPILER_ID};${CMAKE_C_COMPILER_VERSION};${CMAKE_CXX_COMPILER_ID};${CMAKE_CXX_COMPILER_VERSION}")
string(SUBSTRING "${AZURE_SDK_GENERATION}" 0 16 AZURE_SDK_GENERATION)

set(AZURE_SDK_ROOT "${ANYBLOB_DEPS_ROOT}/azure-${AZURE_SDK_GENERATION}")
set(AZURE_SDK_SOURCE_DIR "${AZURE_SDK_ROOT}/src")
set(AZURE_SDK_BUILD_DIR "${AZURE_SDK_ROOT}/build")
set(AZURE_SDK_INSTALL_DIR "${AZURE_SDK_ROOT}/prefix")
set(AZURE_SDK_STAMP_DIR "${AZURE_SDK_ROOT}/stamps")

file(MAKE_DIRECTORY
    "${AZURE_SDK_SOURCE_DIR}"
    "${AZURE_SDK_BUILD_DIR}"
    "${AZURE_SDK_INSTALL_DIR}/include"
    "${AZURE_SDK_INSTALL_DIR}/include/libxml2"
    "${AZURE_SDK_INSTALL_DIR}/lib"
    "${AZURE_SDK_STAMP_DIR}")

set(AZURE_SDK_COMMON_ARGS
    -DBUILD_SHARED_LIBS=OFF
    -DBUILD_TESTING=OFF
    -DBUILD_SAMPLES=OFF
    -DBUILD_PERFORMANCE_TESTS=OFF
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_INSTALL_PREFIX=${AZURE_SDK_INSTALL_DIR}
    -DCMAKE_INSTALL_LIBDIR=lib
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
    -DCMAKE_PREFIX_PATH=${AZURE_SDK_INSTALL_DIR}
    -DOPENSSL_ROOT_DIR=${ANYBLOB_DEPS_INSTALL_DIR}
    -DOPENSSL_INCLUDE_DIR=${ANYBLOB_DEPS_INSTALL_DIR}/include
    -DOPENSSL_SSL_LIBRARY=${ANYBLOB_DEPS_INSTALL_DIR}/lib/libssl.a
    -DOPENSSL_CRYPTO_LIBRARY=${ANYBLOB_DEPS_INSTALL_DIR}/lib/libcrypto.a
    -DOPENSSL_USE_STATIC_LIBS=TRUE
    -DWARNINGS_AS_ERRORS=OFF)

ExternalProject_Add(azure-libxml2
    GIT_REPOSITORY https://github.com/GNOME/libxml2.git
    GIT_TAG ${LIBXML2_GIT_TAG}
    SOURCE_DIR "${AZURE_SDK_SOURCE_DIR}/libxml2"
    BINARY_DIR "${AZURE_SDK_BUILD_DIR}/libxml2"
    STAMP_DIR "${AZURE_SDK_STAMP_DIR}/libxml2"
    CMAKE_ARGS
        ${AZURE_SDK_COMMON_ARGS}
        -DLIBXML2_WITH_CATALOG=OFF
        -DLIBXML2_WITH_DEBUG=OFF
        -DLIBXML2_WITH_FTP=OFF
        -DLIBXML2_WITH_HISTORY=OFF
        -DLIBXML2_WITH_HTML=OFF
        -DLIBXML2_WITH_HTTP=OFF
        -DLIBXML2_WITH_ICONV=OFF
        -DLIBXML2_WITH_ICU=OFF
        -DLIBXML2_WITH_LEGACY=OFF
        -DLIBXML2_WITH_LZMA=OFF
        -DLIBXML2_WITH_MODULES=OFF
        -DLIBXML2_WITH_PROGRAMS=OFF
        -DLIBXML2_WITH_PYTHON=OFF
        -DLIBXML2_WITH_TESTS=OFF
        -DLIBXML2_WITH_ZLIB=OFF
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --parallel ${ANYBLOB_JOBS}
    INSTALL_COMMAND ${CMAKE_COMMAND} --install <BINARY_DIR>
    UPDATE_DISCONNECTED ${ANYBLOB_DEPS_UPDATE_DISCONNECTED}
    BUILD_BYPRODUCTS "${AZURE_SDK_INSTALL_DIR}/lib/libxml2.a")

ExternalProject_Add(azure-core
    GIT_REPOSITORY https://github.com/Azure/azure-sdk-for-cpp.git
    GIT_TAG ${AZURE_CORE_GIT_TAG}
    SOURCE_DIR "${AZURE_SDK_SOURCE_DIR}/azure-core"
    BINARY_DIR "${AZURE_SDK_BUILD_DIR}/azure-core"
    STAMP_DIR "${AZURE_SDK_STAMP_DIR}/azure-core"
    DEPENDS curl aws-lc
    PATCH_COMMAND
        ${CMAKE_COMMAND}
        -DSOURCE_DIR=<SOURCE_DIR>
        -DPATCH_FILE=${CMAKE_CURRENT_LIST_DIR}/patches/azure-core-awslc.patch
        -P ${CMAKE_CURRENT_LIST_DIR}/apply-patch.cmake
        COMMAND
        ${CMAKE_COMMAND}
        -DSOURCE_DIR=<SOURCE_DIR>
        -DPATCH_FILE=${CMAKE_CURRENT_LIST_DIR}/patches/azure-core-no-transport-retry.patch
        -P ${CMAKE_CURRENT_LIST_DIR}/apply-patch.cmake
    CONFIGURE_COMMAND
        ${CMAKE_COMMAND} -E env AZURE_SDK_DISABLE_AUTO_VCPKG=1
        ${CMAKE_COMMAND}
        -S <SOURCE_DIR>/sdk/core/azure-core
        -B <BINARY_DIR>
        ${AZURE_SDK_COMMON_ARGS}
        -DBUILD_TRANSPORT_CURL=ON
        -DNO_AUTOMATIC_TRANSPORT_BUILD=ON
        -DCURL_DIR=${ANYBLOB_DEPS_INSTALL_DIR}/lib/cmake/CURL
        -DCURL_INCLUDE_DIR=${ANYBLOB_DEPS_INSTALL_DIR}/include
        -DCURL_LIBRARY=${ANYBLOB_DEPS_INSTALL_DIR}/lib/libcurl.a
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --parallel ${ANYBLOB_JOBS}
    INSTALL_COMMAND ${CMAKE_COMMAND} --install <BINARY_DIR>
    UPDATE_DISCONNECTED ${ANYBLOB_DEPS_UPDATE_DISCONNECTED}
    BUILD_BYPRODUCTS "${AZURE_SDK_INSTALL_DIR}/lib/libazure-core.a")

ExternalProject_Add(azure-storage-common
    GIT_REPOSITORY https://github.com/Azure/azure-sdk-for-cpp.git
    GIT_TAG ${AZURE_STORAGE_COMMON_GIT_TAG}
    SOURCE_DIR "${AZURE_SDK_SOURCE_DIR}/azure-storage-common"
    BINARY_DIR "${AZURE_SDK_BUILD_DIR}/azure-storage-common"
    STAMP_DIR "${AZURE_SDK_STAMP_DIR}/azure-storage-common"
    DEPENDS azure-core azure-libxml2
    PATCH_COMMAND
        ${CMAKE_COMMAND}
        -DSOURCE_DIR=<SOURCE_DIR>
        -DPATCH_FILE=${CMAKE_CURRENT_LIST_DIR}/patches/azure-storage-no-reliable-stream-retry.patch
        -P ${CMAKE_CURRENT_LIST_DIR}/apply-patch.cmake
    CONFIGURE_COMMAND
        ${CMAKE_COMMAND} -E env AZURE_SDK_DISABLE_AUTO_VCPKG=1
        ${CMAKE_COMMAND}
        -S <SOURCE_DIR>/sdk/storage/azure-storage-common
        -B <BINARY_DIR>
        ${AZURE_SDK_COMMON_ARGS}
        -DANYBLOB_SHARED_PREFIX=${ANYBLOB_DEPS_INSTALL_DIR}
        -DCMAKE_PROJECT_INCLUDE=${CMAKE_CURRENT_LIST_DIR}/azure-sdk-dependency-bootstrap.cmake
        -DLIBXML2_INCLUDE_DIR=${AZURE_SDK_INSTALL_DIR}/include/libxml2
        -DLIBXML2_LIBRARY=${AZURE_SDK_INSTALL_DIR}/lib/libxml2.a
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --parallel ${ANYBLOB_JOBS}
    INSTALL_COMMAND ${CMAKE_COMMAND} --install <BINARY_DIR>
    UPDATE_DISCONNECTED ${ANYBLOB_DEPS_UPDATE_DISCONNECTED}
    BUILD_BYPRODUCTS "${AZURE_SDK_INSTALL_DIR}/lib/libazure-storage-common.a")

ExternalProject_Add(azure-storage-blobs
    GIT_REPOSITORY https://github.com/Azure/azure-sdk-for-cpp.git
    GIT_TAG ${AZURE_STORAGE_BLOBS_GIT_TAG}
    SOURCE_DIR "${AZURE_SDK_SOURCE_DIR}/azure-storage-blobs"
    BINARY_DIR "${AZURE_SDK_BUILD_DIR}/azure-storage-blobs"
    STAMP_DIR "${AZURE_SDK_STAMP_DIR}/azure-storage-blobs"
    DEPENDS azure-storage-common
    CONFIGURE_COMMAND
        ${CMAKE_COMMAND} -E env AZURE_SDK_DISABLE_AUTO_VCPKG=1
        ${CMAKE_COMMAND}
        -S <SOURCE_DIR>/sdk/storage/azure-storage-blobs
        -B <BINARY_DIR>
        ${AZURE_SDK_COMMON_ARGS}
        -DANYBLOB_SHARED_PREFIX=${ANYBLOB_DEPS_INSTALL_DIR}
        -DCMAKE_PROJECT_INCLUDE=${CMAKE_CURRENT_LIST_DIR}/azure-sdk-dependency-bootstrap.cmake
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --parallel ${ANYBLOB_JOBS}
    INSTALL_COMMAND ${CMAKE_COMMAND} --install <BINARY_DIR>
    UPDATE_DISCONNECTED ${ANYBLOB_DEPS_UPDATE_DISCONNECTED}
    BUILD_BYPRODUCTS "${AZURE_SDK_INSTALL_DIR}/lib/libazure-storage-blobs.a")

set(AZURE_SDK_MANIFEST "${AZURE_SDK_ROOT}/manifest.txt")
set(AZURE_SDK_COMPLETE "${AZURE_SDK_ROOT}/complete")
file(GENERATE OUTPUT "${AZURE_SDK_MANIFEST}" CONTENT
"azure-core=${AZURE_CORE_GIT_TAG}
azure-storage-common=${AZURE_STORAGE_COMMON_GIT_TAG}
azure-storage-blobs=${AZURE_STORAGE_BLOBS_GIT_TAG}
libxml2=${LIBXML2_GIT_TAG}
generation=${AZURE_SDK_GENERATION}
azure-policy-max-retries=0
azure-curl-transport-attempts=1
azure-reliable-stream-max-attempts=1
patch-driver-sha256=${AZURE_PATCH_DRIVER_SHA256}
azure-core-awslc-patch-sha256=${AZURE_CORE_AWSLC_PATCH_SHA256}
azure-core-retry-patch-sha256=${AZURE_CORE_RETRY_PATCH_SHA256}
azure-storage-retry-patch-sha256=${AZURE_STORAGE_RETRY_PATCH_SHA256}
c-compiler=${CMAKE_C_COMPILER_ID}-${CMAKE_C_COMPILER_VERSION}
cxx-compiler=${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}
")
add_custom_command(
    OUTPUT "${AZURE_SDK_COMPLETE}"
    COMMAND ${CMAKE_COMMAND} -E touch "${AZURE_SDK_COMPLETE}"
    DEPENDS azure-storage-blobs)
add_custom_target(azure-sdk-dependencies DEPENDS "${AZURE_SDK_COMPLETE}")

add_library(LibXml2::LibXml2 STATIC IMPORTED GLOBAL)
set_target_properties(LibXml2::LibXml2 PROPERTIES
    IMPORTED_LOCATION "${AZURE_SDK_INSTALL_DIR}/lib/libxml2.a"
    INTERFACE_INCLUDE_DIRECTORIES "${AZURE_SDK_INSTALL_DIR}/include/libxml2"
    INTERFACE_LINK_LIBRARIES "Threads::Threads;m")
add_dependencies(LibXml2::LibXml2 azure-sdk-dependencies)

add_library(Azure::azure-core STATIC IMPORTED GLOBAL)
set_target_properties(Azure::azure-core PROPERTIES
    IMPORTED_LOCATION "${AZURE_SDK_INSTALL_DIR}/lib/libazure-core.a"
    INTERFACE_INCLUDE_DIRECTORIES "${AZURE_SDK_INSTALL_DIR}/include"
    INTERFACE_LINK_LIBRARIES "CURL::libcurl;OpenSSL::SSL;OpenSSL::Crypto;Threads::Threads;dl")
add_dependencies(Azure::azure-core azure-sdk-dependencies)

add_library(Azure::azure-storage-common STATIC IMPORTED GLOBAL)
set_target_properties(Azure::azure-storage-common PROPERTIES
    IMPORTED_LOCATION "${AZURE_SDK_INSTALL_DIR}/lib/libazure-storage-common.a"
    INTERFACE_INCLUDE_DIRECTORIES "${AZURE_SDK_INSTALL_DIR}/include"
    INTERFACE_LINK_LIBRARIES "Azure::azure-core;LibXml2::LibXml2;OpenSSL::SSL;OpenSSL::Crypto")
add_dependencies(Azure::azure-storage-common azure-sdk-dependencies)

add_library(Azure::azure-storage-blobs STATIC IMPORTED GLOBAL)
set_target_properties(Azure::azure-storage-blobs PROPERTIES
    IMPORTED_LOCATION "${AZURE_SDK_INSTALL_DIR}/lib/libazure-storage-blobs.a"
    INTERFACE_INCLUDE_DIRECTORIES "${AZURE_SDK_INSTALL_DIR}/include"
    INTERFACE_LINK_LIBRARIES "Azure::azure-storage-common")
add_dependencies(Azure::azure-storage-blobs azure-sdk-dependencies)
