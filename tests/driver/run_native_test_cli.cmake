if(NOT DEFINED JANUS OR NOT DEFINED BUILD_DIR)
    message(FATAL_ERROR "JANUS and BUILD_DIR are required")
endif()

set(PROJECT_DIR "${BUILD_DIR}/native-test-cli-fixture")
file(REMOVE_RECURSE "${PROJECT_DIR}")
file(MAKE_DIRECTORY "${PROJECT_DIR}/src" "${PROJECT_DIR}/tests")
file(WRITE "${PROJECT_DIR}/janus.toml"
     "[package]\nname = \"native-tests\"\nversion = \"0.1.0\"\nentry = \"src/native_tests.janus\"\n")
file(WRITE "${PROJECT_DIR}/src/native_tests.janus"
     "module native_tests\ndef answer() : int { return 42 }\n")
file(WRITE "${PROJECT_DIR}/tests/suite.janus"
     "import native_tests\nimport std.testing\nimport std.result\nimport std.system\n"
     "def consumeTemporary(directory : TestTemporaryDirectory) : int {\n"
     "    assertTrue(stringLength(directory.path()) > usize(0))\n"
     "    delete directory\n    return 0\n}\n"
     "def temporaryFailure(error : SystemError) : int { println(error.context) return 1 }\n"
     "/// @test\ndef passes() : Unit { assertEqual[int](answer(), 42) }\n"
     "/// @test\ndef temporaryDirectory() : Unit {\n"
     "    val created : Result[TestTemporaryDirectory, SystemError] = testTemporaryDirectory(false)\n"
     "    val status : int = match move created {\n"
     "        Ok(directory) => consumeTemporary(move directory),\n"
     "        Error(error) => temporaryFailure(error)\n"
     "    }\n    assertEqual[int](status, 0)\n}\n"
     "/// @test\n/// @serial\ndef serialPasses() : Unit { assertTrue(true) }\n"
     "/// @test\n/// @shouldPanic expected panic\ndef expectedPanic() : Unit { panic(\"expected panic\\n\") }\n"
     "/// @test\n/// @ignore\ndef ignoredFailure() : Unit { println(\"captured marker\") assertFalse(true) }\n"
     "/// @test\n/// @ignore\ndef timesOut() : Unit { while true {} }\n")

execute_process(
    COMMAND "${JANUS}" test --jobs 2
    WORKING_DIRECTORY "${PROJECT_DIR}"
    RESULT_VARIABLE HUMAN_STATUS OUTPUT_VARIABLE HUMAN_OUT ERROR_VARIABLE HUMAN_ERR
)
if(NOT HUMAN_STATUS EQUAL 0 OR NOT HUMAN_OUT MATCHES "4 passed; 0 failed; 2 ignored"
   OR NOT HUMAN_OUT MATCHES "suite.expectedPanic .* ok")
    message(FATAL_ERROR "native human report failed (${HUMAN_STATUS}):\n${HUMAN_OUT}\n${HUMAN_ERR}")
endif()

execute_process(
    COMMAND "${JANUS}" test suite.ignoredFailure --exact --ignored
    WORKING_DIRECTORY "${PROJECT_DIR}"
    RESULT_VARIABLE CAPTURE_STATUS OUTPUT_VARIABLE CAPTURE_OUT ERROR_VARIABLE CAPTURE_ERR
)
if(NOT CAPTURE_STATUS EQUAL 1 OR NOT CAPTURE_ERR MATCHES "captured marker"
   OR NOT CAPTURE_ERR MATCHES "assertFalse failed")
    message(FATAL_ERROR "captured failure failed (${CAPTURE_STATUS}):\n${CAPTURE_OUT}\n${CAPTURE_ERR}")
endif()

execute_process(
    COMMAND "${JANUS}" test suite.timesOut --exact --include-ignored --timeout 20ms --format json
    WORKING_DIRECTORY "${PROJECT_DIR}"
    RESULT_VARIABLE TIMEOUT_STATUS OUTPUT_VARIABLE TIMEOUT_OUT ERROR_VARIABLE TIMEOUT_ERR
)
if(NOT TIMEOUT_STATUS EQUAL 1 OR NOT TIMEOUT_OUT MATCHES "\"status\":\"timed_out\"")
    message(FATAL_ERROR "timeout failed (${TIMEOUT_STATUS}):\n${TIMEOUT_OUT}\n${TIMEOUT_ERR}")
endif()

execute_process(
    COMMAND "${JANUS}" test --list
    WORKING_DIRECTORY "${PROJECT_DIR}"
    RESULT_VARIABLE LIST_STATUS OUTPUT_VARIABLE LIST_OUT ERROR_VARIABLE LIST_ERR
)
if(NOT LIST_STATUS EQUAL 0 OR NOT LIST_OUT MATCHES "suite.passes: unit"
   OR NOT LIST_OUT MATCHES "suite.ignoredFailure: unit [(]ignored[)]")
    message(FATAL_ERROR "native list failed (${LIST_STATUS}):\n${LIST_OUT}\n${LIST_ERR}")
endif()

execute_process(
    COMMAND "${JANUS}" test suite.passes --exact --format json
    WORKING_DIRECTORY "${PROJECT_DIR}"
    RESULT_VARIABLE JSON_STATUS OUTPUT_VARIABLE JSON_OUT ERROR_VARIABLE JSON_ERR
)
if(NOT JSON_STATUS EQUAL 0 OR NOT JSON_OUT MATCHES "\"schema_version\":1"
   OR NOT JSON_OUT MATCHES "\"id\":\"suite.passes\""
   OR NOT JSON_OUT MATCHES "\"status\":\"passed\"")
    message(FATAL_ERROR "native JSON report failed (${JSON_STATUS}):\n${JSON_OUT}\n${JSON_ERR}")
endif()

execute_process(
    COMMAND "${JANUS}" test suite.passes --exact --format junit
    WORKING_DIRECTORY "${PROJECT_DIR}"
    RESULT_VARIABLE JUNIT_STATUS OUTPUT_VARIABLE JUNIT_OUT ERROR_VARIABLE JUNIT_ERR
)
if(NOT JUNIT_STATUS EQUAL 0 OR NOT JUNIT_OUT MATCHES "<testsuite"
   OR NOT JUNIT_OUT MATCHES "name=\"suite.passes\"")
    message(FATAL_ERROR "native JUnit report failed (${JUNIT_STATUS}):\n${JUNIT_OUT}\n${JUNIT_ERR}")
endif()

file(REMOVE_RECURSE "${PROJECT_DIR}/tests")
execute_process(
    COMMAND "${JANUS}" test --fail-if-empty --doc-path missing
    WORKING_DIRECTORY "${PROJECT_DIR}"
    RESULT_VARIABLE EMPTY_STATUS OUTPUT_VARIABLE EMPTY_OUT ERROR_VARIABLE EMPTY_ERR
)
if(NOT EMPTY_STATUS EQUAL 4 OR NOT EMPTY_ERR MATCHES "no tests discovered")
    message(FATAL_ERROR "empty test policy failed (${EMPTY_STATUS}):\n${EMPTY_OUT}\n${EMPTY_ERR}")
endif()
