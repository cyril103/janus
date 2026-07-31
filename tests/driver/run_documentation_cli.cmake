foreach(required JANUS BUILD_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

set(PROJECT_DIR "${BUILD_DIR}/documentation-cli-fixture")
set(OUTPUT_DIR "${PROJECT_DIR}/generated docs")
file(REMOVE_RECURSE "${PROJECT_DIR}")
file(MAKE_DIRECTORY "${PROJECT_DIR}/src")
file(WRITE "${PROJECT_DIR}/janus.toml"
"[package]\n"
"name = \"documented-package\"\n"
"version = \"1.4.0\"\n"
"entry = \"src/main.janus\"\n")
file(WRITE "${PROJECT_DIR}/src/main.janus"
"/// Main module for [[Greeting]] and [[Unknown]].\n"
"module documented\n"
"/// A public greeting.\n"
"class Greeting() {\n"
"    /// Returns a greeting.\n"
"    /// @return The greeting text.\n"
"    def text() : string { return \"hello\" }\n"
"    /// Must stay private.\n"
"    private val secret : int = 7\n"
"}\n"
"/// Builds a [[Greeting]].\n"
"/// @return A new greeting.\n"
"def greeting() : Greeting { return new Greeting() }\n"
"/// Entry point.\n"
"/// @return The process status.\n"
"def main() : int { return 0 }\n"
"/// Demonstrates permissive package diagnostics.\n"
"def incomplete(value : int) : int { return value }\n")

foreach(COMMAND check build run)
    execute_process(
        COMMAND "${JANUS}" "${COMMAND}"
        WORKING_DIRECTORY "${PROJECT_DIR}"
        RESULT_VARIABLE COMMAND_RESULT
        OUTPUT_VARIABLE COMMAND_OUTPUT
        ERROR_VARIABLE COMMAND_ERROR
    )
    if(NOT COMMAND_RESULT EQUAL 0)
        message(FATAL_ERROR
                "janus ${COMMAND} failed with documentation comments "
                "(${COMMAND_RESULT}):\n${COMMAND_OUTPUT}\n${COMMAND_ERROR}")
    endif()
endforeach()

execute_process(
    COMMAND "${JANUS}" doc --offline -o "${OUTPUT_DIR}"
    WORKING_DIRECTORY "${PROJECT_DIR}"
    RESULT_VARIABLE FIRST_RESULT
    OUTPUT_VARIABLE FIRST_OUTPUT
    ERROR_VARIABLE FIRST_ERROR
)
if(NOT FIRST_RESULT EQUAL 0)
    message(FATAL_ERROR
            "janus doc failed (${FIRST_RESULT}):\n${FIRST_ERROR}")
endif()
if(NOT FIRST_OUTPUT MATCHES "generated [0-9]+ public symbols")
    message(FATAL_ERROR "janus doc did not report generated symbols")
endif()
if(NOT FIRST_ERROR MATCHES
   "unresolved documentation link '\\[\\[Unknown\\]\\]' in documented")
    message(FATAL_ERROR "janus doc did not signal the unresolved link")
endif()
if(NOT FIRST_ERROR MATCHES
   "warning: documented.incomplete: public parameter 'value' is undocumented"
   OR NOT FIRST_ERROR MATCHES
   "warning: documented.incomplete: non-unit return type 'int' requires an @return")
    message(FATAL_ERROR
            "janus doc did not keep package contract diagnostics permissive")
endif()

set(HTML "${OUTPUT_DIR}/index.html")
set(API_INDEX "${OUTPUT_DIR}/api-index.json")
if(NOT EXISTS "${HTML}" OR NOT EXISTS "${API_INDEX}")
    message(FATAL_ERROR "janus doc did not create its offline artifacts")
endif()
file(READ "${HTML}" HTML_CONTENT)
file(READ "${API_INDEX}" API_INDEX_CONTENT)
if(NOT HTML_CONTENT MATCHES "A public greeting\\.")
    message(FATAL_ERROR "public type documentation is missing")
endif()
if(NOT HTML_CONTENT MATCHES "Returns a greeting\\.")
    message(FATAL_ERROR "public member documentation is missing")
endif()
if(HTML_CONTENT MATCHES "Must stay private\\.")
    message(FATAL_ERROR "private members must not appear in documentation")
endif()
if(NOT HTML_CONTENT MATCHES "href=\"#documented-greeting\"")
    message(FATAL_ERROR "known symbol links were not resolved")
endif()
foreach(FIELD summary details parameters returns examples)
    if(NOT API_INDEX_CONTENT MATCHES "\"${FIELD}\":")
        message(FATAL_ERROR "api-index.json is missing '${FIELD}'")
    endif()
endforeach()
file(SHA256 "${HTML}" FIRST_HASH)

execute_process(
    COMMAND "${JANUS}" doc --offline -o "${OUTPUT_DIR}"
    WORKING_DIRECTORY "${PROJECT_DIR}"
    RESULT_VARIABLE SECOND_RESULT
    ERROR_VARIABLE SECOND_ERROR
)
if(NOT SECOND_RESULT EQUAL 0)
    message(FATAL_ERROR
            "second janus doc failed (${SECOND_RESULT}):\n${SECOND_ERROR}")
endif()
file(SHA256 "${HTML}" SECOND_HASH)
if(NOT FIRST_HASH STREQUAL SECOND_HASH)
    message(FATAL_ERROR "janus doc output is not deterministic")
endif()

execute_process(
    COMMAND "${JANUS}" doc --help
    RESULT_VARIABLE HELP_RESULT
    OUTPUT_VARIABLE HELP_OUTPUT
)
if(NOT HELP_RESULT EQUAL 0 OR
   NOT HELP_OUTPUT MATCHES "janus doc \\[--stdlib\\] \\[-o directory\\] \\[--open\\]")
    message(FATAL_ERROR "janus doc --help is not available")
endif()

if(UNIX AND NOT APPLE)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "BROWSER=/bin/true"
                "${JANUS}" doc --open -o "${OUTPUT_DIR}"
        WORKING_DIRECTORY "${PROJECT_DIR}"
        RESULT_VARIABLE OPEN_RESULT
        ERROR_VARIABLE OPEN_ERROR
    )
    if(NOT OPEN_RESULT EQUAL 0)
        message(FATAL_ERROR
                "janus doc --open failed (${OPEN_RESULT}):\n${OPEN_ERROR}")
    endif()
endif()

file(REMOVE_RECURSE "${PROJECT_DIR}")
