if(NOT DEFINED BUILD_DIR OR NOT DEFINED JANUS OR NOT DEFINED ID)
    message(FATAL_ERROR "BUILD_DIR, JANUS and ID are required")
endif()

set(TEST_ROOT "${BUILD_DIR}/concurrent-build-test")
set(TEMP_ROOT "${TEST_ROOT}/tmp")
file(MAKE_DIRECTORY "${TEST_ROOT}" "${TEMP_ROOT}")
set(ENV{TMPDIR} "${TEMP_ROOT}")
set(ENV{TMP} "${TEMP_ROOT}")
set(ENV{TEMP} "${TEMP_ROOT}")

set(SOURCE "${TEST_ROOT}/source-${ID}.janus")
set(OUTPUT "${TEST_ROOT}/program-${ID}")
if(WIN32)
    string(APPEND OUTPUT ".exe")
endif()
file(WRITE "${SOURCE}" "def main() : int { return ${ID} }\n")
file(REMOVE "${OUTPUT}")

execute_process(
    COMMAND "${JANUS}" build "${SOURCE}" -o "${OUTPUT}"
    RESULT_VARIABLE STATUS
    ERROR_VARIABLE ERROR
)
if(NOT STATUS EQUAL 0 OR NOT EXISTS "${OUTPUT}")
    message(FATAL_ERROR "concurrent build ${ID} failed: ${ERROR}")
endif()

execute_process(
    COMMAND "${OUTPUT}"
    RESULT_VARIABLE RUN_STATUS
    ERROR_VARIABLE RUN_ERROR
)
if(NOT RUN_STATUS EQUAL ID)
    message(FATAL_ERROR
        "concurrent build ${ID} used another build's artifact: "
        "exit=${RUN_STATUS}, error=${RUN_ERROR}")
endif()
