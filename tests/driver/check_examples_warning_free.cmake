if(NOT DEFINED JANUS OR NOT DEFINED EXAMPLES_DIR)
    message(FATAL_ERROR "JANUS and EXAMPLES_DIR are required")
endif()

file(GLOB_RECURSE EXAMPLES "${EXAMPLES_DIR}/*.janus")
list(SORT EXAMPLES)

foreach(EXAMPLE IN LISTS EXAMPLES)
    execute_process(
        COMMAND "${JANUS}" check "${EXAMPLE}"
        RESULT_VARIABLE CHECK_STATUS
        OUTPUT_VARIABLE CHECK_OUTPUT
        ERROR_VARIABLE CHECK_ERROR
    )
    set(CHECK_DIAGNOSTICS "${CHECK_OUTPUT}${CHECK_ERROR}")
    if(NOT CHECK_STATUS EQUAL 0)
        message(FATAL_ERROR
            "example failed to check: ${EXAMPLE}\n${CHECK_DIAGNOSTICS}")
    endif()
    if(CHECK_DIAGNOSTICS MATCHES "(^|\n)[^\n]*warning:")
        message(FATAL_ERROR
            "example emitted a warning: ${EXAMPLE}\n${CHECK_DIAGNOSTICS}")
    endif()
endforeach()

list(LENGTH EXAMPLES EXAMPLE_COUNT)
message(STATUS "${EXAMPLE_COUNT} Janus examples checked without warnings")
