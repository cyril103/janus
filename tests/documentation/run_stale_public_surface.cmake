if(NOT DEFINED PYTHON OR NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "PYTHON and SOURCE_DIR are required")
endif()

execute_process(
    COMMAND
        "${PYTHON}"
        "${SOURCE_DIR}/scripts/check_public_surface.py"
        --root "${SOURCE_DIR}"
        --inventory
        "${SOURCE_DIR}/tests/fixtures/documentation/stale-public-surface.json"
        --allow-partial
    RESULT_VARIABLE CHECK_STATUS
    OUTPUT_VARIABLE CHECK_OUTPUT
    ERROR_VARIABLE CHECK_ERROR
)

if(CHECK_STATUS EQUAL 0)
    message(FATAL_ERROR "the intentionally stale inventory unexpectedly passed")
endif()

set(EXPECTED_DIAGNOSTIC
    "tests/fixtures/documentation/stale-guide.md: surface std.array.Array.removed")
string(FIND
    "${CHECK_OUTPUT}${CHECK_ERROR}"
    "${EXPECTED_DIAGNOSTIC}"
    DIAGNOSTIC_POSITION
)
if(DIAGNOSTIC_POSITION EQUAL -1)
    message(FATAL_ERROR
        "the stale inventory did not produce its targeted diagnostic:\n"
        "${CHECK_OUTPUT}${CHECK_ERROR}")
endif()

message(STATUS "intentionally stale documentation fixture rejected")
