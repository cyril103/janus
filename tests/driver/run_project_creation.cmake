if(NOT DEFINED BUILD_DIR OR NOT DEFINED JANUS)
    message(FATAL_ERROR "BUILD_DIR and JANUS are required")
endif()

set(TEST_ROOT "${BUILD_DIR}/project-creation-test")
file(REMOVE_RECURSE "${TEST_ROOT}")
set(ENV{JANUS_CACHE} "${TEST_ROOT}/cache")
set(ENV{JANUS_REGISTRY} "${TEST_ROOT}/registry")
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

file(MAKE_DIRECTORY "${TEST_ROOT}/nestedmath/src")
file(WRITE "${TEST_ROOT}/nestedmath/janus.toml"
     "[package]\nname = \"nestedmath\"\nversion = \"1.0.0\"\n"
     "entry = \"src/nestedmath.janus\"\n")
file(WRITE "${TEST_ROOT}/nestedmath/src/nestedmath.janus"
     "module nestedmath\ndef nested_value() : int { return 2 }\n")

file(MAKE_DIRECTORY "${TEST_ROOT}/localmath/src")
file(WRITE "${TEST_ROOT}/localmath/janus.toml"
     "[package]\nname = \"localmath\"\nversion = \"1.0.0\"\n"
     "entry = \"src/localmath.janus\"\n"
     "\n[dependencies]\n"
     "nestedmath = { path = \"../nestedmath\", version = \"~1.0.0\" }\n")
file(WRITE "${TEST_ROOT}/localmath/src/localmath.janus"
     "module localmath\nimport nestedmath\n"
     "def local_value() : int { return 18 + nested_value() }\n")

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
     "localmath = { path = \"../localmath\", version = \"^1.0.0\" }\n"
     "gitmath = { git = \"${GIT_DEPENDENCY_URL}\", "
     "rev = \"${GIT_REVISION}\", version = \"2.*\" }\n")
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
if(NOT IS_DIRECTORY "${TEST_ROOT}/cache/git/${GIT_REVISION}")
    message(FATAL_ERROR "Git dependency was not cached globally")
endif()

execute_process(
    COMMAND "${JANUS}" check --locked --offline
    WORKING_DIRECTORY "${TEST_ROOT}/hello"
    RESULT_VARIABLE LOCKED_STATUS
    ERROR_VARIABLE LOCKED_ERROR
)
if(NOT LOCKED_STATUS EQUAL 0)
    message(FATAL_ERROR "locked offline build failed: ${LOCKED_ERROR}")
endif()
file(APPEND "${TEST_ROOT}/hello/janus.lock" "# stale\n")
execute_process(
    COMMAND "${JANUS}" check --locked
    WORKING_DIRECTORY "${TEST_ROOT}/hello"
    RESULT_VARIABLE STALE_LOCK_STATUS
    ERROR_VARIABLE STALE_LOCK_ERROR
)
if(STALE_LOCK_STATUS EQUAL 0 OR NOT STALE_LOCK_ERROR MATCHES "out of date")
    message(FATAL_ERROR "stale lockfile was accepted: ${STALE_LOCK_ERROR}")
endif()
execute_process(
    COMMAND "${JANUS}" check
    WORKING_DIRECTORY "${TEST_ROOT}/hello"
    RESULT_VARIABLE LOCK_REFRESH_STATUS
)
if(NOT LOCK_REFRESH_STATUS EQUAL 0)
    message(FATAL_ERROR "janus.lock could not be refreshed")
endif()

file(MAKE_DIRECTORY "${TEST_ROOT}/cacheapp/src")
file(WRITE "${TEST_ROOT}/cacheapp/src/main.janus"
     "import gitmath\ndef main() : int { return git_value() - 22 }\n")
file(WRITE "${TEST_ROOT}/cacheapp/janus.toml"
     "[package]\nname = \"cacheapp\"\nversion = \"1.0.0\"\n"
     "entry = \"src/main.janus\"\n[dependencies]\n"
     "gitmath = { git = \"${GIT_DEPENDENCY_URL}\", "
     "rev = \"${GIT_REVISION}\", version = \"2.*\" }\n")
execute_process(
    COMMAND "${JANUS}" check --offline
    WORKING_DIRECTORY "${TEST_ROOT}/cacheapp"
    RESULT_VARIABLE SHARED_CACHE_STATUS
    ERROR_VARIABLE SHARED_CACHE_ERROR
)
if(NOT SHARED_CACHE_STATUS EQUAL 0)
    message(FATAL_ERROR "global cache was not reused: ${SHARED_CACHE_ERROR}")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}/cache/git/${GIT_REVISION}")
execute_process(
    COMMAND "${JANUS}" check --offline
    WORKING_DIRECTORY "${TEST_ROOT}/hello"
    RESULT_VARIABLE OFFLINE_MISS_STATUS
    ERROR_VARIABLE OFFLINE_MISS_ERROR
)
if(OFFLINE_MISS_STATUS EQUAL 0 OR NOT OFFLINE_MISS_ERROR MATCHES "not cached")
    message(FATAL_ERROR "offline cache miss was accepted: ${OFFLINE_MISS_ERROR}")
endif()
execute_process(
    COMMAND "${JANUS}" check
    WORKING_DIRECTORY "${TEST_ROOT}/hello"
    RESULT_VARIABLE CACHE_RESTORE_STATUS
)
if(NOT CACHE_RESTORE_STATUS EQUAL 0)
    message(FATAL_ERROR "Git dependency cache could not be restored")
endif()

execute_process(
    COMMAND "${JANUS}" publish
    WORKING_DIRECTORY "${TEST_ROOT}/nestedmath"
    RESULT_VARIABLE PUBLISH_STATUS
    ERROR_VARIABLE PUBLISH_ERROR
)
if(NOT PUBLISH_STATUS EQUAL 0
   OR NOT IS_DIRECTORY
      "${TEST_ROOT}/registry/nestedmath/1.0.0/package/src")
    message(FATAL_ERROR "package publish failed: ${PUBLISH_ERROR}")
endif()
execute_process(
    COMMAND "${JANUS}" publish
    WORKING_DIRECTORY "${TEST_ROOT}/nestedmath"
    RESULT_VARIABLE REPUBLISH_STATUS
    ERROR_VARIABLE REPUBLISH_ERROR
)
if(REPUBLISH_STATUS EQUAL 0 OR NOT REPUBLISH_ERROR MATCHES "already published")
    message(FATAL_ERROR "immutable package was overwritten")
endif()

execute_process(
    COMMAND "${JANUS}" new "${TEST_ROOT}/registryapp"
    RESULT_VARIABLE REGISTRY_NEW_STATUS
)
execute_process(
    COMMAND "${JANUS}" add "nestedmath@^1.0.0"
    WORKING_DIRECTORY "${TEST_ROOT}/registryapp"
    RESULT_VARIABLE REGISTRY_ADD_STATUS
    ERROR_VARIABLE REGISTRY_ADD_ERROR
)
file(WRITE "${TEST_ROOT}/registryapp/src/main.janus"
     "import nestedmath\n"
     "def main() : int { return nested_value() - 2 }\n")
execute_process(
    COMMAND "${JANUS}" check
    WORKING_DIRECTORY "${TEST_ROOT}/registryapp"
    RESULT_VARIABLE REGISTRY_CHECK_STATUS
    ERROR_VARIABLE REGISTRY_CHECK_ERROR
)
if(NOT REGISTRY_NEW_STATUS EQUAL 0 OR NOT REGISTRY_ADD_STATUS EQUAL 0
   OR NOT REGISTRY_CHECK_STATUS EQUAL 0)
    message(FATAL_ERROR
        "registry dependency failed: ${REGISTRY_ADD_ERROR}"
        "${REGISTRY_CHECK_ERROR}")
endif()
if(NOT IS_DIRECTORY "${TEST_ROOT}/cache/registry/nestedmath/1.0.0")
    message(FATAL_ERROR "registry package was not cached")
endif()
execute_process(
    COMMAND "${JANUS}" check --locked --offline
    WORKING_DIRECTORY "${TEST_ROOT}/registryapp"
    RESULT_VARIABLE REGISTRY_OFFLINE_STATUS
    ERROR_VARIABLE REGISTRY_OFFLINE_ERROR
)
if(NOT REGISTRY_OFFLINE_STATUS EQUAL 0)
    message(FATAL_ERROR
        "cached registry package failed offline: ${REGISTRY_OFFLINE_ERROR}")
endif()
execute_process(
    COMMAND "${JANUS}" remove nestedmath
    WORKING_DIRECTORY "${TEST_ROOT}/registryapp"
    RESULT_VARIABLE REGISTRY_REMOVE_STATUS
)
execute_process(
    COMMAND "${JANUS}" add localmath --path ../localmath --version "^1.0.0"
    WORKING_DIRECTORY "${TEST_ROOT}/registryapp"
    RESULT_VARIABLE PATH_ADD_STATUS
)
execute_process(
    COMMAND "${JANUS}" remove localmath
    WORKING_DIRECTORY "${TEST_ROOT}/registryapp"
    RESULT_VARIABLE PATH_REMOVE_STATUS
)
if(NOT REGISTRY_REMOVE_STATUS EQUAL 0 OR NOT PATH_ADD_STATUS EQUAL 0
   OR NOT PATH_REMOVE_STATUS EQUAL 0)
    message(FATAL_ERROR "add/remove dependency commands failed")
endif()

foreach(PACKAGE cyclea cycleb)
    file(MAKE_DIRECTORY "${TEST_ROOT}/${PACKAGE}/src")
    file(WRITE "${TEST_ROOT}/${PACKAGE}/src/${PACKAGE}.janus"
         "module ${PACKAGE}\ndef ${PACKAGE}_value() : int { return 0 }\n")
endforeach()
file(WRITE "${TEST_ROOT}/cyclea/janus.toml"
     "[package]\nname = \"cyclea\"\nversion = \"1.0.0\"\n"
     "entry = \"src/cyclea.janus\"\n[dependencies]\n"
     "cycleb = { path = \"../cycleb\" }\n")
file(WRITE "${TEST_ROOT}/cycleb/janus.toml"
     "[package]\nname = \"cycleb\"\nversion = \"1.0.0\"\n"
     "entry = \"src/cycleb.janus\"\n[dependencies]\n"
     "cyclea = { path = \"../cyclea\" }\n")
file(MAKE_DIRECTORY "${TEST_ROOT}/cycleapp/src")
file(WRITE "${TEST_ROOT}/cycleapp/src/main.janus"
     "def main() : int { return 0 }\n")
file(WRITE "${TEST_ROOT}/cycleapp/janus.toml"
     "[package]\nname = \"cycleapp\"\nversion = \"1.0.0\"\n"
     "entry = \"src/main.janus\"\n[dependencies]\n"
     "cyclea = { path = \"../cyclea\" }\n")
execute_process(
    COMMAND "${JANUS}" check
    WORKING_DIRECTORY "${TEST_ROOT}/cycleapp"
    RESULT_VARIABLE CYCLE_STATUS
    ERROR_VARIABLE CYCLE_ERROR
)
if(CYCLE_STATUS EQUAL 0 OR NOT CYCLE_ERROR MATCHES "cyclic dependency")
    message(FATAL_ERROR "dependency cycle was not rejected: ${CYCLE_ERROR}")
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
