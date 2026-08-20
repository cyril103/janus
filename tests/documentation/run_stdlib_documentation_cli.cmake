foreach(required JANUS SOURCE_DIR BUILD_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

set(OUTPUT_DIR "${BUILD_DIR}/stdlib-documentation")
set(FIRST_INDEX "${OUTPUT_DIR}/api-index.json")
set(FIRST_HTML "${OUTPUT_DIR}/index.html")
file(REMOVE_RECURSE "${OUTPUT_DIR}")

execute_process(
    COMMAND "${JANUS}" doc --stdlib --offline -o "${OUTPUT_DIR}"
    WORKING_DIRECTORY "${SOURCE_DIR}"
    OUTPUT_VARIABLE FIRST_OUTPUT
    ERROR_VARIABLE FIRST_ERROR
    RESULT_VARIABLE FIRST_RESULT
)
if (NOT FIRST_RESULT EQUAL 0)
    message(FATAL_ERROR
            "stdlib documentation generation failed:\n${FIRST_ERROR}")
endif()
if (NOT EXISTS "${FIRST_INDEX}" OR NOT EXISTS "${FIRST_HTML}")
    message(FATAL_ERROR "stdlib documentation output is incomplete")
endif()
if (FIRST_ERROR MATCHES "unresolved documentation link")
    message(FATAL_ERROR
            "stdlib documentation still contains unresolved links:\n${FIRST_ERROR}")
endif()
file(READ "${FIRST_INDEX}" INDEX_CONTENT)
string(REGEX MATCHALL "\"kind\":\"module\"" MODULES "${INDEX_CONTENT}")
list(LENGTH MODULES MODULE_COUNT)
if(NOT MODULE_COUNT EQUAL 30)
    message(FATAL_ERROR "expected 30 documented stdlib modules, got ${MODULE_COUNT}")
endif()
string(REGEX MATCHALL "\"signature\":\"[^\"]*\"" SYMBOLS "${INDEX_CONTENT}")
list(LENGTH SYMBOLS SYMBOL_COUNT)
if(NOT SYMBOL_COUNT EQUAL 964)
    message(FATAL_ERROR "expected 964 documented stdlib symbols, got ${SYMBOL_COUNT}")
endif()
if(INDEX_CONTENT MATCHES "\"documentation\":\"\"")
    message(FATAL_ERROR "stdlib reference contains undocumented public entries")
endif()
if(INDEX_CONTENT MATCHES "\"name\":\"[^\"]*private"
   OR INDEX_CONTENT MATCHES "\"name\":\"[^\"]*internal")
    message(FATAL_ERROR "stdlib reference exposes a private/internal declaration")
endif()

file(SHA256 "${FIRST_INDEX}" FIRST_INDEX_DIGEST)
file(SHA256 "${FIRST_HTML}" FIRST_HTML_DIGEST)
execute_process(
    COMMAND "${JANUS}" doc --stdlib --offline -o "${OUTPUT_DIR}"
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE SECOND_RESULT
    ERROR_VARIABLE SECOND_ERROR
)
if (NOT SECOND_RESULT EQUAL 0)
    message(FATAL_ERROR
            "second stdlib documentation generation failed:\n${SECOND_ERROR}")
endif()
file(SHA256 "${FIRST_INDEX}" SECOND_INDEX_DIGEST)
file(SHA256 "${FIRST_HTML}" SECOND_HTML_DIGEST)
if(NOT FIRST_INDEX_DIGEST STREQUAL SECOND_INDEX_DIGEST
   OR NOT FIRST_HTML_DIGEST STREQUAL SECOND_HTML_DIGEST)
    message(FATAL_ERROR "stdlib documentation output is not deterministic")
endif()

message(STATUS
        "stdlib documentation covers ${MODULE_COUNT} modules and "
        "${SYMBOL_COUNT} symbols deterministically and enforces contracts")
