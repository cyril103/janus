foreach(required JANUSC SOURCE EXPECTED_ERROR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

execute_process(
    COMMAND "${JANUSC}" "${SOURCE}"
    OUTPUT_VARIABLE JANUSC_OUTPUT
    ERROR_VARIABLE JANUSC_ERROR
    RESULT_VARIABLE JANUSC_RESULT
)

if(JANUSC_RESULT EQUAL 0)
    message(FATAL_ERROR "janusc unexpectedly accepted ${SOURCE}")
endif()
if(NOT JANUSC_ERROR MATCHES "${EXPECTED_ERROR}")
    message(FATAL_ERROR
            "janusc did not report the expected diagnostic\nexpected pattern:\n${EXPECTED_ERROR}\nstderr:\n${JANUSC_ERROR}")
endif()
if(DEFINED EXPECT_NO_OUTPUT AND EXPECT_NO_OUTPUT AND NOT JANUSC_OUTPUT STREQUAL "")
    message(FATAL_ERROR
            "janusc emitted output after a frontend diagnostic:\n${JANUSC_OUTPUT}")
endif()
