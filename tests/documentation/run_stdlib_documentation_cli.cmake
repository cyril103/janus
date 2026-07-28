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
if(NOT FIRST_RESULT EQUAL 0)
    message(FATAL_ERROR
            "stdlib documentation generation failed (${FIRST_RESULT}):\n"
            "${FIRST_ERROR}")
endif()
if(NOT EXISTS "${FIRST_INDEX}" OR NOT EXISTS "${FIRST_HTML}")
    message(FATAL_ERROR "stdlib documentation output is incomplete")
endif()
foreach(reference api-index.json index.html)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E compare_files
                "${OUTPUT_DIR}/${reference}"
                "${SOURCE_DIR}/website/docs/reference/stdlib/${reference}"
        RESULT_VARIABLE REFERENCE_RESULT
    )
    if(NOT REFERENCE_RESULT EQUAL 0)
        message(FATAL_ERROR
                "committed stdlib reference is stale: ${reference}")
    endif()
endforeach()

file(READ "${FIRST_INDEX}" INDEX_CONTENT)
string(REGEX MATCHALL "\"kind\":\"module\"" MODULES "${INDEX_CONTENT}")
list(LENGTH MODULES MODULE_COUNT)
if(NOT MODULE_COUNT EQUAL 28)
    message(FATAL_ERROR "expected 28 documented stdlib modules, got ${MODULE_COUNT}")
endif()
string(REGEX MATCHALL "\"signature\":\"[^\"]*\"" SYMBOLS "${INDEX_CONTENT}")
list(LENGTH SYMBOLS SYMBOL_COUNT)
if(NOT SYMBOL_COUNT EQUAL 637)
    message(FATAL_ERROR "expected 637 documented stdlib symbols, got ${SYMBOL_COUNT}")
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
    OUTPUT_VARIABLE SECOND_OUTPUT
    ERROR_VARIABLE SECOND_ERROR
    RESULT_VARIABLE SECOND_RESULT
)
if(NOT SECOND_RESULT EQUAL 0)
    message(FATAL_ERROR
            "second stdlib documentation generation failed (${SECOND_RESULT}):\n"
            "${SECOND_ERROR}")
endif()
file(SHA256 "${FIRST_INDEX}" SECOND_INDEX_DIGEST)
file(SHA256 "${FIRST_HTML}" SECOND_HTML_DIGEST)
if(NOT FIRST_INDEX_DIGEST STREQUAL SECOND_INDEX_DIGEST
   OR NOT FIRST_HTML_DIGEST STREQUAL SECOND_HTML_DIGEST)
    message(FATAL_ERROR "stdlib documentation output is not deterministic")
endif()

message(STATUS
        "stdlib documentation covers ${MODULE_COUNT} modules and "
        "${SYMBOL_COUNT} symbols deterministically")
