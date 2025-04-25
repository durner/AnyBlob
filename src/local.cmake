# ---------------------------------------------------------------------------
# Files
# ---------------------------------------------------------------------------

file(GLOB_RECURSE SRC_CPP src/*.cpp)
if(NOT LIBURING_FOUND OR ANYBLOB_URING_COMPAT)
    list(REMOVE_ITEM SRC_CPP ${CMAKE_CURRENT_SOURCE_DIR}/src/network/io_uring_socket.cpp)
endif()
if(ANYBLOB_LIBCXX_COMPAT)
    list(REMOVE_ITEM SRC_CPP ${CMAKE_CURRENT_SOURCE_DIR}/src/network/throughput_cache.cpp)
endif()
list(REMOVE_ITEM SRC_CPP ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)
