if(NOT DEFINED BUILD_DIR OR NOT DEFINED JANUS)
    message(FATAL_ERROR "BUILD_DIR and JANUS are required")
endif()

set(TEST_ROOT "${BUILD_DIR}/temporary-cleanup-test")
set(TEMP_ROOT "${TEST_ROOT}/tmp")
file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}" "${TEMP_ROOT}")
set(ENV{TMPDIR} "${TEMP_ROOT}")
set(ENV{TMP} "${TEMP_ROOT}")
set(ENV{TEMP} "${TEMP_ROOT}")

function(require_no_janus_temporaries CONTEXT)
    file(GLOB LEFTOVERS "${TEMP_ROOT}/janus-build-*" "${TEMP_ROOT}/janus-run-*")
    if(LEFTOVERS)
        message(FATAL_ERROR "temporary artifacts survived ${CONTEXT}: ${LEFTOVERS}")
    endif()
endfunction()

set(VALID_SOURCE "${TEST_ROOT}/valid.janus")
set(INVALID_SOURCE "${TEST_ROOT}/invalid.janus")
file(WRITE "${VALID_SOURCE}" "def main() : int { return 0 }\n")
file(WRITE "${INVALID_SOURCE}" "def main( {\n")

set(SUCCESS_OUTPUT "${TEST_ROOT}/success")
if(WIN32)
    string(APPEND SUCCESS_OUTPUT ".exe")
endif()
execute_process(
    COMMAND "${JANUS}" build "${VALID_SOURCE}" -o "${SUCCESS_OUTPUT}"
    RESULT_VARIABLE SUCCESS_STATUS
    ERROR_VARIABLE SUCCESS_ERROR
)
if(NOT SUCCESS_STATUS EQUAL 0 OR NOT EXISTS "${SUCCESS_OUTPUT}")
    message(FATAL_ERROR "successful build failed: ${SUCCESS_ERROR}")
endif()
require_no_janus_temporaries("successful build")

execute_process(
    COMMAND "${JANUS}" run "${VALID_SOURCE}"
    RESULT_VARIABLE RUN_STATUS
    ERROR_VARIABLE RUN_ERROR
)
if(NOT RUN_STATUS EQUAL 0)
    message(FATAL_ERROR "successful run failed: ${RUN_ERROR}")
endif()
require_no_janus_temporaries("successful run")

execute_process(
    COMMAND "${JANUS}" run "${INVALID_SOURCE}"
    RESULT_VARIABLE COMPILE_STATUS
    ERROR_VARIABLE COMPILE_ERROR
)
if(COMPILE_STATUS EQUAL 0)
    message(FATAL_ERROR "invalid source compiled successfully")
endif()
require_no_janus_temporaries("compile error")

set(LINK_SOURCE "${TEST_ROOT}/link-error.janus")
file(WRITE "${LINK_SOURCE}"
     "extern def janus_missing_test_symbol() : int\n"
     "def main() : int { return janus_missing_test_symbol() }\n")
set(LINK_OUTPUT "${TEST_ROOT}/link-error")
if(WIN32)
    string(APPEND LINK_OUTPUT ".exe")
endif()
execute_process(
    COMMAND "${JANUS}" build "${LINK_SOURCE}" -o "${LINK_OUTPUT}"
    RESULT_VARIABLE LINK_STATUS
    ERROR_VARIABLE LINK_ERROR
)
if(LINK_STATUS EQUAL 0)
    message(FATAL_ERROR "unresolved external symbol unexpectedly linked")
endif()
require_no_janus_temporaries("link error")
