# ---------------------------------------------------------------------------
# Files
# ---------------------------------------------------------------------------

file(GLOB_RECURSE INTEGRATION_CPP test/integration/*.cpp)
list(REMOVE_ITEM INTEGRATION_CPP ${CMAKE_CURRENT_SOURCE_DIR}/test/integration/test.cpp)
