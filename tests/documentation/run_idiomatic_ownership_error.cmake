foreach(required JANUSC SOURCE EXPECTED_ERROR OUTPUT_ROOT CASE)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef nonce)
set(work_dir "${OUTPUT_ROOT}/${CASE}-${nonce}")
file(MAKE_DIRECTORY "${work_dir}")
set(unexpected_ir "${work_dir}/unexpected.ll")

execute_process(
    COMMAND "${JANUSC}" "${SOURCE}"
    OUTPUT_FILE "${unexpected_ir}"
    ERROR_VARIABLE compile_error
    RESULT_VARIABLE compile_result
    TIMEOUT 30
)
if(NOT "${compile_result}" STREQUAL "1")
    message(FATAL_ERROR
            "expected semantic rejection (exit 1) for ${SOURCE}, got ${compile_result}:\n${compile_error}")
endif()
if(NOT compile_error MATCHES "${EXPECTED_ERROR}")
    message(FATAL_ERROR
            "wrong ownership diagnostic for ${SOURCE}\nexpected: ${EXPECTED_ERROR}\nstderr:\n${compile_error}")
endif()

file(REMOVE_RECURSE "${work_dir}")
