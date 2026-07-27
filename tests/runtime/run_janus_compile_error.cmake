foreach(required JANUSC SOURCE EXPECTED_ERROR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

execute_process(
    COMMAND "${JANUSC}" "${SOURCE}"
    OUTPUT_QUIET
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
