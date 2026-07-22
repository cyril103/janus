foreach(required JANUSC CLANG SOURCE RUNTIME OUTPUT_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

if(NOT EXISTS "${SOURCE}")
    message(FATAL_ERROR "source example not found: ${SOURCE}")
endif()

if(DEFINED EXPECTED_OUTPUT)
    include("${CMAKE_CURRENT_LIST_DIR}/compare_janus_output.cmake")
endif()

file(MAKE_DIRECTORY "${OUTPUT_DIR}")
set(LLVM_IR "${OUTPUT_DIR}/program.ll")
set(EXECUTABLE "${OUTPUT_DIR}/program_asan")
set(PROGRAM_OUTPUT_ARGS)
if(DEFINED EXPECTED_OUTPUT)
    set(ACTUAL_OUTPUT "${OUTPUT_DIR}/stdout.txt")
    set(PROGRAM_OUTPUT_ARGS OUTPUT_FILE "${ACTUAL_OUTPUT}")
endif()

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
    COMMAND
        "${CLANG}"
        -fsanitize=address
        -fno-omit-frame-pointer
        "${LLVM_IR}"
        "${RUNTIME}"
        -o "${EXECUTABLE}"
    ERROR_VARIABLE CLANG_ERROR
    RESULT_VARIABLE CLANG_RESULT
)
if(NOT CLANG_RESULT EQUAL 0)
    message(FATAL_ERROR "native link failed:\n${CLANG_ERROR}")
endif()

set(ASAN_OPTIONS "detect_leaks=1:halt_on_error=1")
if(WIN32)
    set(ASAN_OPTIONS "detect_leaks=0:halt_on_error=1")
endif()

execute_process(
    COMMAND
        "${CMAKE_COMMAND}" -E env
        "ASAN_OPTIONS=${ASAN_OPTIONS}"
        "${EXECUTABLE}"
    ${PROGRAM_OUTPUT_ARGS}
    ERROR_VARIABLE PROGRAM_ERROR
    RESULT_VARIABLE PROGRAM_RESULT
)
if(NOT PROGRAM_RESULT EQUAL 0)
    message(FATAL_ERROR
            "native example failed (${PROGRAM_RESULT}):\n${PROGRAM_ERROR}")
endif()

if(DEFINED EXPECTED_OUTPUT)
    compare_janus_output("${EXPECTED_OUTPUT}" "${ACTUAL_OUTPUT}")
endif()
