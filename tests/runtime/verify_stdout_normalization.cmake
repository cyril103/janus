include("${CMAKE_CURRENT_LIST_DIR}/compare_janus_output.cmake")

set(PROBE_DIR "${CMAKE_CURRENT_BINARY_DIR}/stdout_normalization_probe")
file(MAKE_DIRECTORY "${PROBE_DIR}")

set(EXPECTED_OUTPUT "${PROBE_DIR}/expected.txt")
set(ACTUAL_CRLF_OUTPUT "${PROBE_DIR}/actual_crlf.txt")
set(ACTUAL_LONE_CR_OUTPUT "${PROBE_DIR}/actual_lone_cr.txt")

file(WRITE "${EXPECTED_OUTPUT}" "42\n2147483648\n3.5\ndone\n")
file(WRITE "${ACTUAL_CRLF_OUTPUT}" "42\r\n2147483648\r\n3.5\r\ndone\r\n")
compare_janus_output("${EXPECTED_OUTPUT}" "${ACTUAL_CRLF_OUTPUT}")

file(WRITE "${ACTUAL_LONE_CR_OUTPUT}" "42\r2147483648\n3.5\ndone\n")
execute_process(
    COMMAND
        "${CMAKE_COMMAND}"
        "-DEXPECTED_OUTPUT=${EXPECTED_OUTPUT}"
        "-DACTUAL_OUTPUT=${ACTUAL_LONE_CR_OUTPUT}"
        -P "${CMAKE_CURRENT_LIST_DIR}/verify_stdout_normalization_rejects_lone_cr.cmake"
    OUTPUT_QUIET
    ERROR_QUIET
    RESULT_VARIABLE LONE_CR_RESULT
)
if(LONE_CR_RESULT EQUAL 0)
    message(FATAL_ERROR "lone CR output unexpectedly compared equal")
endif()
