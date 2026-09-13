foreach(required JANUS SOURCE_DIR BUILD_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
set(PROJECT_DIR "${BUILD_DIR}/map-literal-doctests")
file(MAKE_DIRECTORY "${PROJECT_DIR}/src" "${PROJECT_DIR}/docs")
file(WRITE "${PROJECT_DIR}/janus.toml"
    "[package]\nname = \"map-literal-doctests\"\nversion = \"0.1.0\"\nentry = \"src/main.janus\"\n")
file(WRITE "${PROJECT_DIR}/src/main.janus" "def main() : int { return 0 }\n")
configure_file("${SOURCE_DIR}/docs/stdlib-reference.md"
    "${PROJECT_DIR}/docs/stdlib-reference.md" COPYONLY)
execute_process(
    COMMAND "${JANUS}" test map-literal --doc --fail-if-empty
    WORKING_DIRECTORY "${PROJECT_DIR}"
    RESULT_VARIABLE STATUS
    OUTPUT_VARIABLE OUTPUT
    ERROR_VARIABLE ERROR
)
if(NOT STATUS EQUAL 0 OR NOT OUTPUT MATCHES "3 passed; 0 failed")
    message(FATAL_ERROR "map literal doctests failed:\n${OUTPUT}\n${ERROR}")
endif()
