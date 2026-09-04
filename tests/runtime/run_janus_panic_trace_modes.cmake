foreach(required JANUSC CLANG SOURCE RUNTIME OUTPUT_DIR EXPECTED_OUTPUT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

include("${CMAKE_CURRENT_LIST_DIR}/compare_janus_output.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIR}")

foreach(mode full short off)
    set(LLVM_IR "${OUTPUT_DIR}/program-${mode}.ll")
    set(EXECUTABLE "${OUTPUT_DIR}/program-${mode}${CMAKE_EXECUTABLE_SUFFIX}")
    execute_process(
        COMMAND "${JANUSC}" "${SOURCE}" --panic-trace "${mode}"
        OUTPUT_FILE "${LLVM_IR}"
        ERROR_VARIABLE JANUSC_ERROR
        RESULT_VARIABLE JANUSC_RESULT
    )
    if(NOT JANUSC_RESULT EQUAL 0)
        message(FATAL_ERROR "janusc (${mode}) failed:\n${JANUSC_ERROR}")
    endif()

    set(SANITIZERS address,undefined)
    if(WIN32)
        set(SANITIZERS address)
    endif()
    execute_process(
        COMMAND "${CLANG}" "-fsanitize=${SANITIZERS}"
                -fno-omit-frame-pointer "${LLVM_IR}" "${RUNTIME}"
                -o "${EXECUTABLE}"
        ERROR_VARIABLE CLANG_ERROR
        RESULT_VARIABLE CLANG_RESULT
    )
    if(NOT CLANG_RESULT EQUAL 0)
        message(FATAL_ERROR "native link (${mode}) failed:\n${CLANG_ERROR}")
    endif()

    set(ASAN_OPTIONS "detect_leaks=1:halt_on_error=1")
    if(WIN32 OR APPLE)
        set(ASAN_OPTIONS "detect_leaks=0:halt_on_error=1")
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
                "ASAN_OPTIONS=${ASAN_OPTIONS}"
                "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1"
                "${EXECUTABLE}"
        OUTPUT_VARIABLE PROGRAM_OUTPUT
        ERROR_VARIABLE PROGRAM_ERROR
        RESULT_VARIABLE PROGRAM_RESULT
    )
    if(PROGRAM_RESULT EQUAL 0)
        message(FATAL_ERROR "panic fixture (${mode}) unexpectedly succeeded")
    endif()
    if(NOT PROGRAM_ERROR MATCHES "interprocedural panic" OR
       NOT PROGRAM_ERROR MATCHES "panic during destructor")
        message(FATAL_ERROR "panic diagnostics (${mode}) are missing:\n${PROGRAM_ERROR}")
    endif()
    if(mode STREQUAL "full")
        if(NOT PROGRAM_ERROR MATCHES "at .+:[0-9]+ in [A-Za-z0-9_.]+" OR
           NOT PROGRAM_ERROR MATCHES "stack trace \\(experimental\\):")
            message(FATAL_ERROR "full panic trace is incomplete:\n${PROGRAM_ERROR}")
        endif()
    elseif(mode STREQUAL "short")
        if(NOT PROGRAM_ERROR MATCHES "at .+:[0-9]+ in [A-Za-z0-9_.]+" OR
           PROGRAM_ERROR MATCHES "stack trace \\(experimental\\):")
            message(FATAL_ERROR "short panic trace has the wrong shape:\n${PROGRAM_ERROR}")
        endif()
    else()
        if(PROGRAM_ERROR MATCHES "at .+:[0-9]+ in [A-Za-z0-9_.]+" OR
           PROGRAM_ERROR MATCHES "stack trace \\(experimental\\):")
            message(FATAL_ERROR "disabled panic trace emitted context:\n${PROGRAM_ERROR}")
        endif()
    endif()
    set(ACTUAL_OUTPUT "${OUTPUT_DIR}/stdout-${mode}.txt")
    file(WRITE "${ACTUAL_OUTPUT}" "${PROGRAM_OUTPUT}")
    compare_janus_output("${EXPECTED_OUTPUT}" "${ACTUAL_OUTPUT}")
endforeach()
