function(compare_janus_output EXPECTED_OUTPUT ACTUAL_OUTPUT)
    if(NOT EXISTS "${EXPECTED_OUTPUT}")
        message(FATAL_ERROR "expected output not found: ${EXPECTED_OUTPUT}")
    endif()
    if(NOT EXISTS "${ACTUAL_OUTPUT}")
        message(FATAL_ERROR "actual output not found: ${ACTUAL_OUTPUT}")
    endif()

    file(READ "${EXPECTED_OUTPUT}" EXPECTED_TEXT)
    file(READ "${ACTUAL_OUTPUT}" ACTUAL_TEXT)

    string(FIND "${EXPECTED_TEXT}" "\r" EXPECTED_CR)
    if(NOT EXPECTED_CR EQUAL -1)
        message(FATAL_ERROR
                "expected output must use canonical LF line endings: ${EXPECTED_OUTPUT}")
    endif()

    string(REPLACE "\r\n" "\n" NORMALIZED_ACTUAL_TEXT "${ACTUAL_TEXT}")
    string(FIND "${NORMALIZED_ACTUAL_TEXT}" "\r" ACTUAL_LONE_CR)
    if(NOT ACTUAL_LONE_CR EQUAL -1)
        message(FATAL_ERROR
                "actual stdout contains a lone CR after CRLF normalization")
    endif()

    if(NOT NORMALIZED_ACTUAL_TEXT STREQUAL EXPECTED_TEXT)
        message(FATAL_ERROR
                "stdout mismatch\nexpected:\n${EXPECTED_TEXT}\nactual:\n${NORMALIZED_ACTUAL_TEXT}")
    endif()
endfunction()
