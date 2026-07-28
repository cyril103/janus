if(NOT DEFINED JANUS OR NOT DEFINED BUILD_DIR)
    message(FATAL_ERROR "JANUS and BUILD_DIR are required")
endif()

set(PROJECT_DIR "${BUILD_DIR}/doctest-cli-fixture")
file(REMOVE_RECURSE "${PROJECT_DIR}")
file(MAKE_DIRECTORY "${PROJECT_DIR}/src" "${PROJECT_DIR}/tests"
     "${PROJECT_DIR}/docs" "${PROJECT_DIR}/site")
file(WRITE "${PROJECT_DIR}/janus.toml"
     "[package]\nname = \"doctest-fixture\"\nversion = \"0.1.0\"\nentry = \"src/main.janus\"\n")
file(WRITE "${PROJECT_DIR}/src/main.janus"
     "def main() : int { return 0 }\n")
file(WRITE "${PROJECT_DIR}/src/api.janus"
     "module api\n\ndef package_value() : int { return 42 }\n")
file(WRITE "${PROJECT_DIR}/tests/unit.janus"
     "def main() : int { return 0 }\n")
file(WRITE "${PROJECT_DIR}/README.md"
"# Package examples

```janus doctest name=package-api
import api
def main() : int { return package_value() - 42 }
```

```janus compile_fail=JANA0001 name=unknown-value
def main() : int { return missing }
```

```janus title=\"legacy-illustration.janus\"
def main() : int { return definitely_not_compilable }
```
")
file(WRITE "${PROJECT_DIR}/docs/incomplete.md"
"```janus incomplete
def main() : int { return deliberately_incomplete }
```
")
file(WRITE "${PROJECT_DIR}/site/example.md"
"```janus doctest name=site-example
def main() : int { return 0 }
```
")

execute_process(
    COMMAND "${JANUS}" test
    WORKING_DIRECTORY "${PROJECT_DIR}"
    RESULT_VARIABLE DEFAULT_STATUS
    OUTPUT_VARIABLE DEFAULT_OUT
    ERROR_VARIABLE DEFAULT_ERR
)
if(NOT DEFAULT_STATUS EQUAL 0
   OR NOT DEFAULT_OUT MATCHES "3 passed; 0 failed"
   OR NOT DEFAULT_OUT MATCHES "README.md:4 \\(package-api\\)"
   OR NOT DEFAULT_OUT MATCHES "README.md:9 \\(unknown-value\\)"
   OR DEFAULT_OUT MATCHES "legacy-illustration|deliberately-incomplete")
    message(FATAL_ERROR
        "default doctest contract failed: status=${DEFAULT_STATUS}\nstdout=[${DEFAULT_OUT}]\nstderr=[${DEFAULT_ERR}]")
endif()

execute_process(
    COMMAND "${JANUS}" test package-api
    WORKING_DIRECTORY "${PROJECT_DIR}"
    RESULT_VARIABLE FILTER_STATUS
    OUTPUT_VARIABLE FILTER_OUT
    ERROR_VARIABLE FILTER_ERR
)
if(NOT FILTER_STATUS EQUAL 0 OR NOT FILTER_OUT MATCHES "1 passed; 0 failed"
   OR FILTER_OUT MATCHES "unknown-value|unit")
    message(FATAL_ERROR
        "doctest filter contract failed: status=${FILTER_STATUS}\nstdout=[${FILTER_OUT}]\nstderr=[${FILTER_ERR}]")
endif()

execute_process(
    COMMAND "${JANUS}" test --doc --doc-path site
    WORKING_DIRECTORY "${PROJECT_DIR}"
    RESULT_VARIABLE SITE_STATUS
    OUTPUT_VARIABLE SITE_OUT
    ERROR_VARIABLE SITE_ERR
)
if(NOT SITE_STATUS EQUAL 0 OR NOT SITE_OUT MATCHES "site/example.md:2"
   OR NOT SITE_OUT MATCHES "1 passed; 0 failed"
   OR SITE_OUT MATCHES "package-api|test unit")
    message(FATAL_ERROR
        "custom documentation path failed: status=${SITE_STATUS}\nstdout=[${SITE_OUT}]\nstderr=[${SITE_ERR}]")
endif()

# Renaming a package API must break the documentation example that imports it.
file(WRITE "${PROJECT_DIR}/src/api.janus"
     "module api\n\ndef renamed_value() : int { return 42 }\n")
execute_process(
    COMMAND "${JANUS}" test package-api
    WORKING_DIRECTORY "${PROJECT_DIR}"
    RESULT_VARIABLE RENAME_STATUS
    OUTPUT_VARIABLE RENAME_OUT
    ERROR_VARIABLE RENAME_ERR
)
string(REPLACE "\\" "/" RENAME_ERR "${RENAME_ERR}")
if(NOT RENAME_STATUS EQUAL 1
   OR NOT RENAME_ERR MATCHES "README.md:4: error: doctest compilation failed")
    message(FATAL_ERROR
        "API rename did not break its doctest: status=${RENAME_STATUS}\nstdout=[${RENAME_OUT}]\nstderr=[${RENAME_ERR}]")
endif()

# Compile-fail examples compare stable codes, not diagnostic wording.
file(WRITE "${PROJECT_DIR}/docs/wrong-code.md"
"```janus compile_fail=JPAR0001 name=wrong-code
def main() : int { return missing }
```
")
execute_process(
    COMMAND "${JANUS}" test wrong-code
    WORKING_DIRECTORY "${PROJECT_DIR}"
    RESULT_VARIABLE CODE_STATUS
    OUTPUT_VARIABLE CODE_OUT
    ERROR_VARIABLE CODE_ERR
)
string(REPLACE "\\" "/" CODE_ERR "${CODE_ERR}")
if(NOT CODE_STATUS EQUAL 1
   OR NOT CODE_ERR MATCHES
      "docs/wrong-code.md:2: error: expected diagnostic JPAR0001, got JANA0001")
    message(FATAL_ERROR
        "compile-fail code contract failed: status=${CODE_STATUS}\nstdout=[${CODE_OUT}]\nstderr=[${CODE_ERR}]")
endif()

file(REMOVE_RECURSE "${PROJECT_DIR}")
