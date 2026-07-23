foreach(required JANUSC CLANG SOURCE RUNTIME OUTPUT_DIR EXPECTED_OUTPUT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

include("${CMAKE_CURRENT_LIST_DIR}/compare_janus_output.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
set(LLVM_IR "${OUTPUT_DIR}/program.ll")
set(EXECUTABLE "${OUTPUT_DIR}/program")
set(ACTUAL_OUTPUT "${OUTPUT_DIR}/stdout.txt")

execute_process(
    COMMAND "${JANUSC}" "${SOURCE}"
    OUTPUT_FILE "${LLVM_IR}"
    ERROR_VARIABLE JANUSC_ERROR
    RESULT_VARIABLE JANUSC_RESULT
)
if(NOT JANUSC_RESULT EQUAL 0)
    message(FATAL_ERROR "janusc failed:\n${JANUSC_ERROR}")
endif()

execute_process(
    COMMAND "${CLANG}" "${LLVM_IR}" "${RUNTIME}" -o "${EXECUTABLE}"
    ERROR_VARIABLE CLANG_ERROR
    RESULT_VARIABLE CLANG_RESULT
)
if(NOT CLANG_RESULT EQUAL 0)
    message(FATAL_ERROR "native link failed:\n${CLANG_ERROR}")
endif()

execute_process(
    COMMAND "${EXECUTABLE}"
    OUTPUT_FILE "${ACTUAL_OUTPUT}"
    ERROR_VARIABLE PROGRAM_ERROR
    RESULT_VARIABLE PROGRAM_RESULT
)
if(PROGRAM_RESULT EQUAL 0)
    message(FATAL_ERROR "panic program unexpectedly succeeded")
endif()
if(NOT PROGRAM_ERROR MATCHES "global initialization failed")
    message(FATAL_ERROR "panic diagnostic is missing:\n${PROGRAM_ERROR}")
endif()

compare_janus_output("${EXPECTED_OUTPUT}" "${ACTUAL_OUTPUT}")
