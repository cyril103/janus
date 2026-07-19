file(MAKE_DIRECTORY "${OUTPUT_DIR}")
set(LLVM_IR "${OUTPUT_DIR}/c_abi.ll")
set(EXECUTABLE "${OUTPUT_DIR}/c_abi_asan")

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
        "${FIXTURE}"
        "${RUNTIME}"
        -o "${EXECUTABLE}"
    ERROR_VARIABLE CLANG_ERROR
    RESULT_VARIABLE CLANG_RESULT
)
if(NOT CLANG_RESULT EQUAL 0)
    message(FATAL_ERROR "native link failed:\n${CLANG_ERROR}")
endif()

execute_process(
    COMMAND
        "${CMAKE_COMMAND}" -E env
        "ASAN_OPTIONS=detect_leaks=1:halt_on_error=1"
        "${EXECUTABLE}"
    ERROR_VARIABLE PROGRAM_ERROR
    RESULT_VARIABLE PROGRAM_RESULT
)
if(NOT PROGRAM_RESULT EQUAL 0)
    message(FATAL_ERROR
            "C interoperability executable failed (${PROGRAM_RESULT}):\n"
            "${PROGRAM_ERROR}")
endif()
