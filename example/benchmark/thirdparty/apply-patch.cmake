if(NOT DEFINED SOURCE_DIR OR NOT DEFINED PATCH_FILE)
    message(FATAL_ERROR "SOURCE_DIR and PATCH_FILE are required")
endif()

execute_process(
    COMMAND git apply --check "${PATCH_FILE}"
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE can_apply
    OUTPUT_QUIET
    ERROR_QUIET)

if(can_apply EQUAL 0)
    execute_process(
        COMMAND git apply --whitespace=nowarn "${PATCH_FILE}"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE apply_result)
    if(NOT apply_result EQUAL 0)
        message(FATAL_ERROR "Failed to apply ${PATCH_FILE}")
    endif()
    return()
endif()

execute_process(
    COMMAND git apply --reverse --check "${PATCH_FILE}"
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE already_applied
    OUTPUT_QUIET
    ERROR_QUIET)
if(NOT already_applied EQUAL 0)
    message(FATAL_ERROR "${PATCH_FILE} neither applies nor is already applied")
endif()
