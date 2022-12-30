# ---------------------------------------------------------------------------
# Files
# ---------------------------------------------------------------------------

file(GLOB_RECURSE TEST_CPP test/*.cpp)
list(REMOVE_ITEM TEST_CPP ${CMAKE_CURRENT_SOURCE_DIR}/test/test.cpp)
