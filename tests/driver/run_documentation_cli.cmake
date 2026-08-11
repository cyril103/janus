foreach(required JANUS BUILD_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

set(PROJECT_DIR "${BUILD_DIR}/documentation-cli-fixture")
set(NO_PROJECT_DIR "${BUILD_DIR}/documentation-search-no-project")
set(OUTPUT_DIR "${PROJECT_DIR}/generated docs")
file(REMOVE_RECURSE "${PROJECT_DIR}" "${NO_PROJECT_DIR}")
file(MAKE_DIRECTORY "${PROJECT_DIR}/src" "${NO_PROJECT_DIR}")
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
    COMMAND "${JANUS}" doc --search Array.any --format json
    WORKING_DIRECTORY "${NO_PROJECT_DIR}"
    RESULT_VARIABLE NO_PROJECT_SEARCH_RESULT
    OUTPUT_VARIABLE NO_PROJECT_SEARCH_OUTPUT
    ERROR_VARIABLE NO_PROJECT_SEARCH_ERROR
)
if(NOT NO_PROJECT_SEARCH_RESULT EQUAL 0 OR
   NOT NO_PROJECT_SEARCH_OUTPUT MATCHES "std.array.Array.any")
    message(FATAL_ERROR
            "stdlib API search must work outside a project "
            "(${NO_PROJECT_SEARCH_RESULT}):\n"
            "${NO_PROJECT_SEARCH_OUTPUT}\n${NO_PROJECT_SEARCH_ERROR}")
endif()

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
if(NOT API_INDEX_CONTENT MATCHES
   "\"signature\":\"class Greeting\\(\\)\"")
    message(FATAL_ERROR "class constructor signature is not shared with discovery")
endif()
file(SHA256 "${HTML}" FIRST_HASH)

# Search must consume the generated package index instead of reparsing sources.
file(MAKE_DIRECTORY "${PROJECT_DIR}/target/doc")
file(COPY_FILE "${API_INDEX}" "${PROJECT_DIR}/target/doc/api-index.json")
file(RENAME "${PROJECT_DIR}/src" "${PROJECT_DIR}/src.hidden")

execute_process(
    COMMAND "${JANUS}" doc --search "public greeting" --format human
            --module documented --kind class --package documented-package
    WORKING_DIRECTORY "${PROJECT_DIR}"
    RESULT_VARIABLE SEARCH_RESULT
    OUTPUT_VARIABLE SEARCH_HUMAN
    ERROR_VARIABLE SEARCH_ERROR
)
if(NOT SEARCH_RESULT EQUAL 0 OR
   NOT SEARCH_HUMAN MATCHES "documented.Greeting" OR
   NOT SEARCH_HUMAN MATCHES "import documented" OR
   SEARCH_HUMAN MATCHES "secret")
    message(FATAL_ERROR "human API search failed:\n${SEARCH_HUMAN}\n${SEARCH_ERROR}")
endif()
execute_process(
    COMMAND "${JANUS}" doc --search documented.greeting --format json
    WORKING_DIRECTORY "${PROJECT_DIR}"
    RESULT_VARIABLE JSON_SEARCH_RESULT
    OUTPUT_VARIABLE FIRST_JSON_SEARCH
)
execute_process(
    COMMAND "${JANUS}" doc --search documented.greeting --format json
    WORKING_DIRECTORY "${PROJECT_DIR}"
    OUTPUT_VARIABLE SECOND_JSON_SEARCH
)
if(NOT JSON_SEARCH_RESULT EQUAL 0 OR
   NOT FIRST_JSON_SEARCH STREQUAL SECOND_JSON_SEARCH OR
   NOT FIRST_JSON_SEARCH MATCHES "\"format_version\":1" OR
   NOT FIRST_JSON_SEARCH MATCHES "\"required_import\":\"documented\"")
    message(FATAL_ERROR "JSON API search is missing or non-deterministic")
endif()
file(RENAME "${PROJECT_DIR}/src.hidden" "${PROJECT_DIR}/src")

# A resolved dependency path points at <dependency>/src. Search must locate the
# index from its project root and retain manifest package metadata offline.
set(DEPENDENCY_DIR "${BUILD_DIR}/documentation-cli-dependency")
file(REMOVE_RECURSE "${DEPENDENCY_DIR}")
file(MAKE_DIRECTORY "${DEPENDENCY_DIR}/src")
file(WRITE "${DEPENDENCY_DIR}/janus.toml"
"[package]\nname = \"real-dependency\"\nversion = \"3.2.1\"\nentry = \"src/main.janus\"\n")
file(WRITE "${DEPENDENCY_DIR}/src/main.janus"
"module dependency_api\n/// Found only in the dependency.\ndef offlineNeedle() : int { return 7 }\n")
execute_process(
    COMMAND "${JANUS}" doc --offline
    WORKING_DIRECTORY "${DEPENDENCY_DIR}"
    RESULT_VARIABLE DEPENDENCY_DOC_RESULT
    ERROR_VARIABLE DEPENDENCY_DOC_ERROR
)
if(NOT DEPENDENCY_DOC_RESULT EQUAL 0)
    message(FATAL_ERROR "dependency docs failed: ${DEPENDENCY_DOC_ERROR}")
endif()
file(APPEND "${PROJECT_DIR}/janus.toml"
"\n[dependencies]\nreal-dependency = { path = \"../documentation-cli-dependency\" }\n")
file(RENAME "${DEPENDENCY_DIR}/src/main.janus"
            "${DEPENDENCY_DIR}/main.janus.hidden")
file(REMOVE "${PROJECT_DIR}/janus.lock")
execute_process(
    COMMAND "${JANUS}" doc --search offlineNeedle --offline
            --package real-dependency
    WORKING_DIRECTORY "${PROJECT_DIR}"
    RESULT_VARIABLE DEPENDENCY_SEARCH_RESULT
    OUTPUT_VARIABLE DEPENDENCY_SEARCH_OUTPUT
    ERROR_VARIABLE DEPENDENCY_SEARCH_ERROR
)
if(NOT DEPENDENCY_SEARCH_RESULT EQUAL 0 OR
   NOT DEPENDENCY_SEARCH_OUTPUT MATCHES "dependency_api.offlineNeedle" OR
   NOT DEPENDENCY_SEARCH_OUTPUT MATCHES "real-dependency")
    message(FATAL_ERROR
            "offline dependency index search failed:\n${DEPENDENCY_SEARCH_OUTPUT}\n${DEPENDENCY_SEARCH_ERROR}")
endif()
if(EXISTS "${PROJECT_DIR}/janus.lock")
    message(FATAL_ERROR "read-only API search must not create janus.lock")
endif()
file(RENAME "${DEPENDENCY_DIR}/main.janus.hidden"
            "${DEPENDENCY_DIR}/src/main.janus")

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
   NOT HELP_OUTPUT MATCHES "janus doc \\[--stdlib\\] \\[-o directory\\] \\[--open\\] \\[--offline\\] \\[--search QUERY\\]")
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
file(REMOVE_RECURSE "${DEPENDENCY_DIR}")
