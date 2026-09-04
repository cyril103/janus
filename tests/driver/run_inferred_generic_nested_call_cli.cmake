foreach(required JANUS SOURCE EXPECTED_OUTPUT OUTPUT_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

file(MAKE_DIRECTORY "${OUTPUT_DIR}")
set(PROGRAM "${OUTPUT_DIR}/program")
if(WIN32)
    string(APPEND PROGRAM ".exe")
endif()

execute_process(
    COMMAND "${JANUS}" check "${SOURCE}" --deny-warnings
    RESULT_VARIABLE CHECK_RESULT
    ERROR_VARIABLE CHECK_ERROR
)
if(NOT CHECK_RESULT EQUAL 0)
    message(FATAL_ERROR "nested generic check failed (${CHECK_RESULT}):\n${CHECK_ERROR}")
endif()

execute_process(
    COMMAND "${JANUS}" build "${SOURCE}" --no-cache -o "${PROGRAM}"
    RESULT_VARIABLE BUILD_RESULT
    ERROR_VARIABLE BUILD_ERROR
)
if(NOT BUILD_RESULT EQUAL 0)
    message(FATAL_ERROR "nested generic build failed (${BUILD_RESULT}):\n${BUILD_ERROR}")
endif()

execute_process(
    COMMAND "${JANUS}" run "${SOURCE}"
    RESULT_VARIABLE RUN_RESULT
    OUTPUT_VARIABLE RUN_OUTPUT
    ERROR_VARIABLE RUN_ERROR
)
if(NOT RUN_RESULT EQUAL 0)
    message(FATAL_ERROR "nested generic run failed (${RUN_RESULT}):\n${RUN_ERROR}")
endif()

file(READ "${EXPECTED_OUTPUT}" EXPECTED)
if(NOT RUN_OUTPUT STREQUAL EXPECTED)
    message(FATAL_ERROR
        "nested generic output differs\nexpected:\n${EXPECTED}\nactual:\n${RUN_OUTPUT}")
endif()
