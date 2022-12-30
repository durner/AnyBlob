# ---------------------------------------------------------------------------
# Files
# ---------------------------------------------------------------------------

file(GLOB_RECURSE SRC_CPP src/*.cpp)
list(REMOVE_ITEM SRC_CPP ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)
