if(NOT DEFINED JANUS OR NOT DEFINED BUILD_DIR)
    message(FATAL_ERROR "JANUS and BUILD_DIR are required")
endif()

set(TEST_ROOT "${BUILD_DIR}/cli-execution-contract")
file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}/empty" "${TEST_ROOT}/project/src"
     "${TEST_ROOT}/project/tests")

set(TOP_LEVEL_USAGE
"usage:
  janus new <directory> [--name <name>]
  janus init [directory] [--name <name>]
  janus add <name>[@<version>] [--path <path> | --git <url> --rev <commit>]
  janus remove <name>
  janus publish
  janus check [source.janus]
  janus build [source.janus] [-o output] [--release] [--emit llvm-ir|object]
  janus run [source.janus] [--release]
  janus test [filter] [--release]
  janus fmt [source.janus] [--check]
  diagnostics: --warn-high-growth-loops for check, build, run
  dependency options: --locked --offline
  janus --help
  janus --version
")
set(CHECK_USAGE
"usage: janus check [source.janus] [--locked] [--offline] [--warn-high-growth-loops]
")
set(BUILD_USAGE
"usage: janus build [source.janus] [-o output] [--release] [--emit llvm-ir|object] [--locked] [--offline] [--warn-high-growth-loops]
")
set(RUN_USAGE
"usage: janus run [source.janus] [--release] [--locked] [--offline] [--warn-high-growth-loops]
")
set(TEST_USAGE
"usage: janus test [filter] [--release] [--locked] [--offline]
")

function(assert_result NAME EXPECTED_STATUS EXPECTED_OUT EXPECTED_ERR)
    execute_process(
        COMMAND "${JANUS}" ${ARGN}
        WORKING_DIRECTORY "${TEST_ROOT}/empty"
        RESULT_VARIABLE STATUS
        OUTPUT_VARIABLE OUT
        ERROR_VARIABLE ERR
    )
    string(REPLACE "\r\n" "\n" OUT "${OUT}")
    string(REPLACE "\r\n" "\n" ERR "${ERR}")
    if(NOT STATUS EQUAL EXPECTED_STATUS)
        message(FATAL_ERROR
            "${NAME}: expected status ${EXPECTED_STATUS}, got ${STATUS}\nstdout=[${OUT}]\nstderr=[${ERR}]")
    endif()
    if(NOT OUT STREQUAL EXPECTED_OUT)
        message(FATAL_ERROR
            "${NAME}: unexpected stdout\nexpected=[${EXPECTED_OUT}]\nactual=[${OUT}]")
    endif()
    if(NOT ERR STREQUAL EXPECTED_ERR)
        message(FATAL_ERROR
            "${NAME}: unexpected stderr\nexpected=[${EXPECTED_ERR}]\nactual=[${ERR}]")
    endif()
endfunction()

# Help is successful, is written only to stdout, and must not require a project.
assert_result("top-level help" 0 "${TOP_LEVEL_USAGE}" "" --help)
assert_result("check help" 0 "${CHECK_USAGE}" "" check --help)
assert_result("build help" 0 "${BUILD_USAGE}" "" build --help)
assert_result("run help" 0 "${RUN_USAGE}" "" run --help)
assert_result("test help" 0 "${TEST_USAGE}" "" test --help)

# Invocation mistakes use status 2, a command-qualified diagnostic, and only
# the usage relevant to that command.
assert_result(
    "missing command" 2 ""
    "janus: error: missing command\n${TOP_LEVEL_USAGE}"
)
assert_result(
    "unknown command" 2 ""
    "janus: error: unknown command 'unknown'\n${TOP_LEVEL_USAGE}"
    unknown
)
assert_result(
    "check invocation error" 2 ""
    "janus check: error: unknown option '--bogus'\n${CHECK_USAGE}"
    check --bogus
)
assert_result(
    "build invocation error" 2 ""
    "janus build: error: -o requires an output path\n${BUILD_USAGE}"
    build -o
)
assert_result(
    "run invocation error" 2 ""
    "janus run: error: run does not accept -o or --emit\n${RUN_USAGE}"
    run -o ignored
)
assert_result(
    "test invocation error" 2 ""
    "janus test: error: test accepts at most one filter\n${TEST_USAGE}"
    test first second
)
assert_result(
    "fmt invocation remains coherent" 2 ""
    "janus fmt: error: unknown option '--bogus'\nusage: janus fmt [source.janus] [--check]\n"
    fmt --bogus
)
assert_result(
    "conflicting emit modes" 2 ""
    "janus build: error: --emit may be specified only once\n${BUILD_USAGE}"
    build --emit llvm-ir --emit object
)

# Operational failures use status 1 and never append usage.
execute_process(
    COMMAND "${JANUS}" check missing.janus
    WORKING_DIRECTORY "${TEST_ROOT}/empty"
    RESULT_VARIABLE OPERATION_STATUS
    OUTPUT_VARIABLE OPERATION_OUT
    ERROR_VARIABLE OPERATION_ERR
)
string(REPLACE "\r\n" "\n" OPERATION_OUT "${OPERATION_OUT}")
string(REPLACE "\r\n" "\n" OPERATION_ERR "${OPERATION_ERR}")
if(NOT OPERATION_STATUS EQUAL 1 OR NOT OPERATION_OUT STREQUAL ""
   OR NOT OPERATION_ERR MATCHES "janus: error: .*missing\\.janus"
   OR OPERATION_ERR MATCHES "usage:")
    message(FATAL_ERROR
        "operational failure contract violated: status=${OPERATION_STATUS}\nstdout=[${OPERATION_OUT}]\nstderr=[${OPERATION_ERR}]")
endif()

# Standalone run returns the program's status unchanged.
file(WRITE "${TEST_ROOT}/exit7.janus"
     "def main() : int {\n    return 7\n}\n")
execute_process(
    COMMAND "${JANUS}" run "${TEST_ROOT}/exit7.janus"
    WORKING_DIRECTORY "${TEST_ROOT}/empty"
    RESULT_VARIABLE CHILD_STATUS
    OUTPUT_VARIABLE CHILD_OUT
    ERROR_VARIABLE CHILD_ERR
)
if(NOT CHILD_STATUS EQUAL 7)
    message(FATAL_ERROR
        "run did not preserve child status: ${CHILD_STATUS}\nstdout=[${CHILD_OUT}]\nstderr=[${CHILD_ERR}]")
endif()

# Project test filtering is preserved, and malformed test diagnostics retain
# the conventional path:line:column: error shape.
file(WRITE "${TEST_ROOT}/project/janus.toml"
     "[package]\nname = \"contract\"\nversion = \"0.1.0\"\nentry = \"src/main.janus\"\n")
file(WRITE "${TEST_ROOT}/project/src/main.janus"
     "def main() : int { return 0 }\n")
file(WRITE "${TEST_ROOT}/project/tests/pass.janus"
     "def main() : int { return 0 }\n")
file(WRITE "${TEST_ROOT}/project/tests/skip.janus"
     "def main() : int { return 1 }\n")
execute_process(
    COMMAND "${JANUS}" test pass
    WORKING_DIRECTORY "${TEST_ROOT}/project"
    RESULT_VARIABLE FILTER_STATUS
    OUTPUT_VARIABLE FILTER_OUT
    ERROR_VARIABLE FILTER_ERR
)
if(NOT FILTER_STATUS EQUAL 0 OR NOT FILTER_OUT MATCHES "1 passed; 0 failed"
   OR FILTER_OUT MATCHES "skip")
    message(FATAL_ERROR
        "test filter contract violated: status=${FILTER_STATUS}\nstdout=[${FILTER_OUT}]\nstderr=[${FILTER_ERR}]")
endif()
file(WRITE "${TEST_ROOT}/project/tests/malformed.janus"
     "def main() : int {\n    return nope\n}\n")
execute_process(
    COMMAND "${JANUS}" test malformed
    WORKING_DIRECTORY "${TEST_ROOT}/project"
    RESULT_VARIABLE MALFORMED_STATUS
    OUTPUT_VARIABLE MALFORMED_OUT
    ERROR_VARIABLE MALFORMED_ERR
)
string(REPLACE "\\" "/" MALFORMED_ERR "${MALFORMED_ERR}")
if(NOT MALFORMED_STATUS EQUAL 1
   OR NOT MALFORMED_ERR MATCHES "tests/malformed\\.janus:[0-9]+:[0-9]+: error: ")
    message(FATAL_ERROR
        "malformed test diagnostic contract violated: status=${MALFORMED_STATUS}\nstdout=[${MALFORMED_OUT}]\nstderr=[${MALFORMED_ERR}]")
endif()
