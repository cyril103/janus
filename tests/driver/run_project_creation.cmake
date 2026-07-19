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
foreach(FILE janus.toml src/main.janus .gitignore tests)
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

file(WRITE "${TEST_ROOT}/hello/src/helper.janus"
     "module helper\ndef helper_value() : int { return 42 }\n")
file(WRITE "${TEST_ROOT}/hello/tests/basic.janus"
     "import helper\n"
     "def main() : int {\n    return helper_value() - 42\n}\n")
execute_process(
    COMMAND "${JANUS}" test
    WORKING_DIRECTORY "${TEST_ROOT}/hello"
    RESULT_VARIABLE TEST_STATUS
    OUTPUT_VARIABLE TEST_OUTPUT
    ERROR_VARIABLE TEST_ERROR
)
if(NOT TEST_STATUS EQUAL 0 OR NOT TEST_OUTPUT MATCHES "1 passed; 0 failed")
    message(FATAL_ERROR "janus test failed: ${TEST_ERROR}\n${TEST_OUTPUT}")
endif()
if(NOT EXISTS "${TEST_ROOT}/hello/target/debug/tests/basic")
    message(FATAL_ERROR "janus test did not create an isolated executable")
endif()
file(WRITE "${TEST_ROOT}/hello/tests/failing.janus"
     "def main() : int {\n    return 1\n}\n")
execute_process(
    COMMAND "${JANUS}" test failing
    WORKING_DIRECTORY "${TEST_ROOT}/hello"
    RESULT_VARIABLE FAILING_TEST_STATUS
    OUTPUT_VARIABLE FAILING_TEST_OUTPUT
    ERROR_VARIABLE FAILING_TEST_ERROR
)
if(FAILING_TEST_STATUS EQUAL 0
   OR NOT FAILING_TEST_OUTPUT MATCHES "0 passed; 1 failed")
    message(FATAL_ERROR
        "janus test did not report failure: ${FAILING_TEST_ERROR}\n"
        "${FAILING_TEST_OUTPUT}")
endif()
file(REMOVE "${TEST_ROOT}/hello/tests/failing.janus")

file(MAKE_DIRECTORY "${TEST_ROOT}/localmath/src")
file(WRITE "${TEST_ROOT}/localmath/janus.toml"
     "[package]\nname = \"localmath\"\nversion = \"1.0.0\"\n"
     "entry = \"src/localmath.janus\"\n")
file(WRITE "${TEST_ROOT}/localmath/src/localmath.janus"
     "module localmath\ndef local_value() : int { return 20 }\n")

file(MAKE_DIRECTORY "${TEST_ROOT}/gitmath/src")
file(WRITE "${TEST_ROOT}/gitmath/janus.toml"
     "[package]\nname = \"gitmath\"\nversion = \"2.0.0\"\n"
     "entry = \"src/gitmath.janus\"\n")
file(WRITE "${TEST_ROOT}/gitmath/src/gitmath.janus"
     "module gitmath\ndef git_value() : int { return 22 }\n")
execute_process(
    COMMAND git init --quiet
    WORKING_DIRECTORY "${TEST_ROOT}/gitmath"
    RESULT_VARIABLE GIT_INIT_STATUS
)
execute_process(
    COMMAND git add .
    WORKING_DIRECTORY "${TEST_ROOT}/gitmath"
    RESULT_VARIABLE GIT_ADD_STATUS
)
execute_process(
    COMMAND git -c user.name=Janus -c user.email=janus@example.invalid
            commit --quiet -m fixture
    WORKING_DIRECTORY "${TEST_ROOT}/gitmath"
    RESULT_VARIABLE GIT_COMMIT_STATUS
)
execute_process(
    COMMAND git rev-parse HEAD
    WORKING_DIRECTORY "${TEST_ROOT}/gitmath"
    OUTPUT_VARIABLE GIT_REVISION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE GIT_REVISION_STATUS
)
if(NOT GIT_INIT_STATUS EQUAL 0 OR NOT GIT_ADD_STATUS EQUAL 0
   OR NOT GIT_COMMIT_STATUS EQUAL 0 OR NOT GIT_REVISION_STATUS EQUAL 0)
    message(FATAL_ERROR "could not prepare the Git dependency fixture")
endif()
file(TO_CMAKE_PATH "${TEST_ROOT}/gitmath" GIT_DEPENDENCY_URL)
file(APPEND "${TEST_ROOT}/hello/janus.toml"
     "\n[dependencies]\n"
     "localmath = { path = \"../localmath\" }\n"
     "gitmath = { git = \"${GIT_DEPENDENCY_URL}\", "
     "rev = \"${GIT_REVISION}\" }\n")
file(WRITE "${TEST_ROOT}/hello/src/main.janus"
     "import localmath\nimport gitmath\n"
     "def main() : int { return local_value() + git_value() - 42 }\n")
execute_process(
    COMMAND "${JANUS}" check
    WORKING_DIRECTORY "${TEST_ROOT}/hello"
    RESULT_VARIABLE DEPENDENCY_STATUS
    ERROR_VARIABLE DEPENDENCY_ERROR
)
if(NOT DEPENDENCY_STATUS EQUAL 0)
    message(FATAL_ERROR "dependency resolution failed: ${DEPENDENCY_ERROR}")
endif()
if(NOT EXISTS "${TEST_ROOT}/hello/janus.lock")
    message(FATAL_ERROR "dependency resolution did not create janus.lock")
endif()
file(READ "${TEST_ROOT}/hello/janus.lock" LOCK_CONTENTS)
if(NOT LOCK_CONTENTS MATCHES "path\\+\\.\\./localmath"
   OR NOT LOCK_CONTENTS MATCHES "${GIT_REVISION}")
    message(FATAL_ERROR "janus.lock does not contain resolved dependencies")
endif()
if(NOT IS_DIRECTORY
   "${TEST_ROOT}/hello/target/dependencies/gitmath-${GIT_REVISION}")
    string(SUBSTRING "${GIT_REVISION}" 0 12 GIT_SHORT_REVISION)
    if(NOT IS_DIRECTORY
       "${TEST_ROOT}/hello/target/dependencies/gitmath-${GIT_SHORT_REVISION}")
        message(FATAL_ERROR "Git dependency was not cached")
    endif()
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
