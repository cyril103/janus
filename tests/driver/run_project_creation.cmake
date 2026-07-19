if(NOT DEFINED BUILD_DIR OR NOT DEFINED JANUS)
    message(FATAL_ERROR "BUILD_DIR and JANUS are required")
endif()

set(TEST_ROOT "${BUILD_DIR}/project-creation-test")
file(REMOVE_RECURSE "${TEST_ROOT}")
execute_process(
    COMMAND "${JANUS}" new "${TEST_ROOT}/hello"
    RESULT_VARIABLE NEW_STATUS
    ERROR_VARIABLE NEW_ERROR
)
if(NOT NEW_STATUS EQUAL 0)
    message(FATAL_ERROR "janus new failed: ${NEW_ERROR}")
endif()
foreach(FILE janus.toml src/main.janus .gitignore)
    if(NOT EXISTS "${TEST_ROOT}/hello/${FILE}")
        message(FATAL_ERROR "janus new did not create ${FILE}")
    endif()
endforeach()
execute_process(
    COMMAND "${JANUS}" check
    WORKING_DIRECTORY "${TEST_ROOT}/hello"
    RESULT_VARIABLE CHECK_STATUS
    ERROR_VARIABLE CHECK_ERROR
)
if(NOT CHECK_STATUS EQUAL 0)
    message(FATAL_ERROR "generated project is invalid: ${CHECK_ERROR}")
endif()

execute_process(
    COMMAND "${JANUS}" build
    WORKING_DIRECTORY "${TEST_ROOT}/hello"
    RESULT_VARIABLE BUILD_STATUS
    ERROR_VARIABLE BUILD_ERROR
)
if(NOT BUILD_STATUS EQUAL 0
   OR NOT EXISTS "${TEST_ROOT}/hello/target/debug/hello")
    message(FATAL_ERROR "project build failed: ${BUILD_ERROR}")
endif()
execute_process(
    COMMAND "${JANUS}" run
    WORKING_DIRECTORY "${TEST_ROOT}/hello"
    RESULT_VARIABLE RUN_STATUS
    OUTPUT_VARIABLE RUN_OUTPUT
    ERROR_VARIABLE RUN_ERROR
)
if(NOT RUN_STATUS EQUAL 0 OR NOT RUN_OUTPUT MATCHES "Hello from Janus")
    message(FATAL_ERROR "project run failed: ${RUN_ERROR}")
endif()
execute_process(
    COMMAND "${JANUS}" build --release
    WORKING_DIRECTORY "${TEST_ROOT}/hello"
    RESULT_VARIABLE RELEASE_STATUS
    ERROR_VARIABLE RELEASE_ERROR
)
if(NOT RELEASE_STATUS EQUAL 0
   OR NOT EXISTS "${TEST_ROOT}/hello/target/release/hello")
    message(FATAL_ERROR "release project build failed: ${RELEASE_ERROR}")
endif()

file(MAKE_DIRECTORY "${TEST_ROOT}/existing/src")
file(WRITE "${TEST_ROOT}/existing/src/main.janus"
     "def main() : int {\n    return 7\n}\n")
execute_process(
    COMMAND "${JANUS}" init "${TEST_ROOT}/existing" --name existing_project
    RESULT_VARIABLE INIT_STATUS
    ERROR_VARIABLE INIT_ERROR
)
if(NOT INIT_STATUS EQUAL 0)
    message(FATAL_ERROR "janus init failed: ${INIT_ERROR}")
endif()
file(READ "${TEST_ROOT}/existing/src/main.janus" EXISTING_SOURCE)
if(NOT EXISTING_SOURCE MATCHES "return 7")
    message(FATAL_ERROR "janus init overwrote an existing source")
endif()
