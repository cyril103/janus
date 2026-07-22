if(NOT DEFINED JANUS)
    message(FATAL_ERROR "JANUS is required")
endif()
if(NOT DEFINED SOURCE)
    message(FATAL_ERROR "SOURCE is required")
endif()
if(NOT DEFINED KEYWORD)
    message(FATAL_ERROR "KEYWORD is required")
endif()

execute_process(
    COMMAND "${JANUS}" check "${SOURCE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)

if(result EQUAL 0)
    message(FATAL_ERROR "janus check unexpectedly accepted ${SOURCE}")
endif()

string(CONCAT diagnostic "${output}" "${error}")

if(NOT diagnostic MATCHES ":1:1: error:")
    message(FATAL_ERROR "diagnostic does not point at line 1, column 1:\n${diagnostic}")
endif()
if(NOT diagnostic MATCHES "top-level val/var declarations are not supported")
    message(FATAL_ERROR "diagnostic does not explain unsupported top-level declarations:\n${diagnostic}")
endif()
if(NOT diagnostic MATCHES "found '${KEYWORD}'")
    message(FATAL_ERROR "diagnostic does not name the offending keyword:\n${diagnostic}")
endif()
if(NOT diagnostic MATCHES "move the declaration into a function")
    message(FATAL_ERROR "diagnostic does not recommend moving into a function:\n${diagnostic}")
endif()
if(NOT diagnostic MATCHES "expose it through a function")
    message(FATAL_ERROR "diagnostic does not recommend exposing through a function:\n${diagnostic}")
endif()
if(diagnostic MATCHES "expected 'def', found '${KEYWORD}'")
    message(FATAL_ERROR "diagnostic still uses the old generic parser error:\n${diagnostic}")
endif()
