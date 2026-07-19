if(NOT DEFINED BUILD_DIR OR NOT DEFINED SOURCE_DIR OR NOT DEFINED JANUSUP)
    message(FATAL_ERROR "BUILD_DIR, SOURCE_DIR and JANUSUP are required")
endif()

set(TEST_ROOT "${BUILD_DIR}/janusup-test")
file(REMOVE_RECURSE "${TEST_ROOT}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}"
            --prefix "${TEST_ROOT}/package"
    RESULT_VARIABLE INSTALL_STATUS
    OUTPUT_QUIET
)
if(NOT INSTALL_STATUS EQUAL 0)
    message(FATAL_ERROR "could not stage the Janus package")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
            "JANUSUP_HOME=${TEST_ROOT}/home"
            "${JANUSUP}" install "${TEST_ROOT}/package" test
    RESULT_VARIABLE JANUSUP_STATUS
    OUTPUT_VARIABLE JANUSUP_OUTPUT
    ERROR_VARIABLE JANUSUP_ERROR
)
if(NOT JANUSUP_STATUS EQUAL 0)
    message(FATAL_ERROR "janusup install failed: ${JANUSUP_ERROR}")
endif()
if(NOT EXISTS "${TEST_ROOT}/home/toolchains/test/bin/janus")
    message(FATAL_ERROR "janusup did not install the compiler")
endif()
if(NOT EXISTS "${TEST_ROOT}/home/bin/janus")
    message(FATAL_ERROR "janusup did not create the compiler shim")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
            "JANUSUP_HOME=${TEST_ROOT}/home"
            "${JANUSUP}" list
    OUTPUT_VARIABLE LIST_OUTPUT
)
if(NOT LIST_OUTPUT MATCHES "\\* test")
    message(FATAL_ERROR "installed toolchain is not active: ${LIST_OUTPUT}")
endif()

execute_process(
    COMMAND "${TEST_ROOT}/home/bin/janus" run
            "${SOURCE_DIR}/examples/output.janus"
    RESULT_VARIABLE RUN_STATUS
    OUTPUT_VARIABLE RUN_OUTPUT
    ERROR_VARIABLE RUN_ERROR
)
if(NOT RUN_STATUS EQUAL 0)
    message(FATAL_ERROR "installed compiler could not run a program: ${RUN_ERROR}")
endif()
if(NOT RUN_OUTPUT MATCHES "Janus")
    message(FATAL_ERROR "installed program produced unexpected output: ${RUN_OUTPUT}")
endif()
