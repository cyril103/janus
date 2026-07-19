if(NOT DEFINED BUILD_DIR OR NOT DEFINED SOURCE_DIR OR NOT DEFINED JANUS
   OR NOT DEFINED JANUSUP
   OR NOT DEFINED PACKAGE_PLATFORM OR NOT DEFINED PACKAGE_ARCH)
    message(FATAL_ERROR "missing janusup test configuration")
endif()

set(TEST_ROOT "${BUILD_DIR}/janusup-test")
if(WIN32)
    set(EXECUTABLE_SUFFIX ".exe")
else()
    set(EXECUTABLE_SUFFIX "")
endif()
file(REMOVE_RECURSE "${TEST_ROOT}")
if(WIN32)
    # The package smoke test exercises the complete Windows archive. Keep this
    # janusup lifecycle test small so Defender does not rescan thousands of
    # Clang resource files during a second recursive copy.
    file(MAKE_DIRECTORY "${TEST_ROOT}/package/bin")
    file(COPY_FILE "${JANUS}" "${TEST_ROOT}/package/bin/janus.exe")
    file(COPY_FILE "${JANUSUP}" "${TEST_ROOT}/package/bin/janusup.exe")
    file(COPY_FILE "${JANUS}" "${TEST_ROOT}/package/bin/clang.exe")
    file(COPY_FILE "${JANUS}" "${TEST_ROOT}/package/bin/lld-link.exe")
else()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}"
                --prefix "${TEST_ROOT}/package"
        RESULT_VARIABLE INSTALL_STATUS
        OUTPUT_QUIET
    )
    if(NOT INSTALL_STATUS EQUAL 0)
        message(FATAL_ERROR "could not stage the Janus package")
    endif()
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
if(NOT EXISTS
   "${TEST_ROOT}/home/toolchains/test/bin/janus${EXECUTABLE_SUFFIX}")
    message(FATAL_ERROR "janusup did not install the compiler")
endif()
if(NOT EXISTS "${TEST_ROOT}/home/bin/janus${EXECUTABLE_SUFFIX}")
    message(FATAL_ERROR "janusup did not create the compiler shim")
endif()
if(NOT EXISTS
   "${TEST_ROOT}/home/toolchains/test/bin/clang${EXECUTABLE_SUFFIX}")
    message(FATAL_ERROR "the toolchain does not contain Clang")
endif()
if(NOT EXISTS "${TEST_ROOT}/home/toolchains/test/bin/ld.lld"
   AND NOT EXISTS "${TEST_ROOT}/home/toolchains/test/bin/lld-link.exe"
   AND NOT EXISTS "${TEST_ROOT}/home/toolchains/test/bin/ld64.lld")
    message(FATAL_ERROR "the toolchain does not contain LLD")
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

if(NOT WIN32)
    execute_process(
        COMMAND "${TEST_ROOT}/home/bin/janus${EXECUTABLE_SUFFIX}" run
                "${SOURCE_DIR}/examples/output.janus"
        RESULT_VARIABLE RUN_STATUS
        OUTPUT_VARIABLE RUN_OUTPUT
        ERROR_VARIABLE RUN_ERROR
    )
    if(NOT RUN_STATUS EQUAL 0)
        message(FATAL_ERROR
                "installed compiler could not run a program: ${RUN_ERROR}")
    endif()
    if(NOT RUN_OUTPUT MATCHES "Janus")
        message(FATAL_ERROR
                "installed program produced unexpected output: ${RUN_OUTPUT}")
    endif()
endif()

set(REMOTE_VERSION "0.1.1")
if(PACKAGE_ARCH MATCHES "^(aarch64|ARM64)$")
    set(PACKAGE_ARCH "arm64")
endif()
set(REMOTE_BASENAME
    "janus-${REMOTE_VERSION}-${PACKAGE_PLATFORM}-${PACKAGE_ARCH}")
set(REMOTE_SOURCE "${TEST_ROOT}/remote-source/${REMOTE_BASENAME}")
set(REMOTE_DIST "${TEST_ROOT}/dist/v${REMOTE_VERSION}")
file(MAKE_DIRECTORY "${REMOTE_SOURCE}/bin" "${REMOTE_DIST}")
if(WIN32)
    file(WRITE "${REMOTE_SOURCE}/bin/janus.exe" "")
    set(REMOTE_ARCHIVE "${REMOTE_DIST}/${REMOTE_BASENAME}.zip")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E tar cf "${REMOTE_ARCHIVE}" --format=zip
                "${REMOTE_BASENAME}"
        WORKING_DIRECTORY "${TEST_ROOT}/remote-source"
        RESULT_VARIABLE ARCHIVE_STATUS
    )
else()
    file(WRITE "${REMOTE_SOURCE}/bin/janus" "")
    set(REMOTE_ARCHIVE "${REMOTE_DIST}/${REMOTE_BASENAME}.tar.gz")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E tar czf "${REMOTE_ARCHIVE}"
                "${REMOTE_BASENAME}"
        WORKING_DIRECTORY "${TEST_ROOT}/remote-source"
        RESULT_VARIABLE ARCHIVE_STATUS
    )
endif()
if(NOT ARCHIVE_STATUS EQUAL 0)
    message(FATAL_ERROR "could not create the fake remote package")
endif()
file(SHA256 "${REMOTE_ARCHIVE}" REMOTE_DIGEST)
get_filename_component(REMOTE_ARCHIVE_NAME "${REMOTE_ARCHIVE}" NAME)
file(WRITE "${REMOTE_ARCHIVE}.sha256"
     "${REMOTE_DIGEST}  ${REMOTE_ARCHIVE_NAME}\n")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
            "JANUSUP_HOME=${TEST_ROOT}/home"
            "JANUS_DIST_SERVER=${TEST_ROOT}/dist"
            "${JANUSUP}" install "${REMOTE_VERSION}"
    RESULT_VARIABLE REMOTE_INSTALL_STATUS
    ERROR_VARIABLE REMOTE_INSTALL_ERROR
)
if(NOT REMOTE_INSTALL_STATUS EQUAL 0)
    message(FATAL_ERROR "remote install failed: ${REMOTE_INSTALL_ERROR}")
endif()
if(NOT EXISTS "${TEST_ROOT}/home/toolchains/${REMOTE_VERSION}/bin")
    message(FATAL_ERROR "remote toolchain was not installed")
endif()

foreach(CHANNEL stable beta nightly)
    file(MAKE_DIRECTORY "${TEST_ROOT}/dist/channel-${CHANNEL}")
    file(WRITE "${TEST_ROOT}/dist/channel-${CHANNEL}/version"
         "${REMOTE_VERSION} v${REMOTE_VERSION}\n")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
                "JANUSUP_HOME=${TEST_ROOT}/home"
                "JANUS_DIST_SERVER=${TEST_ROOT}/dist"
                "${JANUSUP}" install "${CHANNEL}"
        RESULT_VARIABLE CHANNEL_STATUS
        ERROR_VARIABLE CHANNEL_ERROR
    )
    if(NOT CHANNEL_STATUS EQUAL 0
       OR NOT EXISTS "${TEST_ROOT}/home/toolchains/${CHANNEL}/bin")
        message(FATAL_ERROR "${CHANNEL} install failed: ${CHANNEL_ERROR}")
    endif()
endforeach()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
            "JANUSUP_HOME=${TEST_ROOT}/home"
            "JANUS_DIST_SERVER=${TEST_ROOT}/dist"
            "${JANUSUP}" update
    RESULT_VARIABLE CHANNEL_UPDATE_STATUS
    ERROR_VARIABLE CHANNEL_UPDATE_ERROR
)
if(NOT CHANNEL_UPDATE_STATUS EQUAL 0)
    message(FATAL_ERROR "active channel update failed: ${CHANNEL_UPDATE_ERROR}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
            "JANUSUP_HOME=${TEST_ROOT}/home"
            "${JANUSUP}" uninstall test
    RESULT_VARIABLE UNINSTALL_STATUS
)
if(NOT UNINSTALL_STATUS EQUAL 0
   OR EXISTS "${TEST_ROOT}/home/toolchains/test")
    message(FATAL_ERROR "inactive toolchain was not uninstalled")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
            "JANUSUP_HOME=${TEST_ROOT}/home"
            "${JANUSUP}" uninstall nightly
    RESULT_VARIABLE ACTIVE_UNINSTALL_STATUS
    ERROR_QUIET
)
if(ACTIVE_UNINSTALL_STATUS EQUAL 0)
    message(FATAL_ERROR "janusup uninstalled the active toolchain")
endif()
