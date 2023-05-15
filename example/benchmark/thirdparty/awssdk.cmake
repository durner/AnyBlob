# ---------------------------------------------------------------------------
# AWS SDK
# ---------------------------------------------------------------------------
Include(ExternalProject)

set(AWS_REQUIRED_LIBS
  aws-cpp-sdk-s3-crt
  aws-cpp-sdk-s3
  aws-cpp-sdk-core
  aws-cpp-sdk-kms
  aws-cpp-sdk-s3-encryption
  aws-crt-cpp
  aws-c-s3
  aws-c-auth
  aws-c-http
  aws-c-io
  aws-c-event-stream
  aws-c-cal
  aws-c-compression
  aws-c-mqtt
  aws-c-sdkutils
  s2n
  aws-checksums
  aws-c-common
)

set(AWS_INSTALL_DIR "thirdparty/awssdk/install")
set(AWS_BUILD_BYPRODUCTS "")
foreach(LIBNAME ${AWS_REQUIRED_LIBS})
  list(APPEND AWS_BUILD_BYPRODUCTS ${AWS_INSTALL_DIR}/lib/lib${LIBNAME}.a)
endforeach()
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-error=deprecated-declarations -Wno-stringop-truncation  -Wno-nonnull")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-error=deprecated-declarations -Wno-stringop-truncation  -Wno-nonnull")

ExternalProject_Add(awssdk
  GIT_REPOSITORY      https://github.com/aws/aws-sdk-cpp.git
  GIT_TAG             "1.11.31"
  PREFIX              "thirdparty/awssdk"
  INSTALL_DIR         ${AWS_INSTALL_DIR}
  CMAKE_ARGS
    -DBUILD_ONLY=core\\$<SEMICOLON>s3\\$<SEMICOLON>s3-crt\\$<SEMICOLON>s3-encryption
    -DBUILD_SHARED_LIBS:BOOL=OFF
    -DBUILD_STATIC_LIBS:BOOL=ON
    -DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=ON
    -DENABLE_TESTING:BOOL=OFF
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/${AWS_INSTALL_DIR}
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
    -DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}
  BUILD_BYPRODUCTS ${AWS_BUILD_BYPRODUCTS}
)

ExternalProject_Get_Property(awssdk INSTALL_DIR)
set(AWS_INCLUDE_DIR ${INSTALL_DIR}/include)
file(MAKE_DIRECTORY ${AWS_INCLUDE_DIR})


foreach(LIBNAME ${AWS_REQUIRED_LIBS})
  add_library(${LIBNAME} STATIC IMPORTED)
  set_property(TARGET ${LIBNAME} PROPERTY IMPORTED_LOCATION ${INSTALL_DIR}/lib/lib${LIBNAME}.a)
  set_property(TARGET ${LIBNAME} APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${AWS_INCLUDE_DIR})
  add_dependencies(${LIBNAME} awssdk)
endforeach()

# Load OpenSSL as a static lib
# set(OPENSSL_USE_STATIC_LIBS TRUE)
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

set(AWS_LINK_LIBRARIES ${AWS_REQUIRED_LIBS} OpenSSL::Crypto ${CURL_LIBRARY} dl)
