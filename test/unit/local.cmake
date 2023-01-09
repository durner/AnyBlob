# ---------------------------------------------------------------------------
# Files
# ---------------------------------------------------------------------------

file(GLOB_RECURSE TEST_CPP test/unit/*.cpp)
list(REMOVE_ITEM TEST_CPP ${CMAKE_CURRENT_SOURCE_DIR}/test/unit/test.cpp)
