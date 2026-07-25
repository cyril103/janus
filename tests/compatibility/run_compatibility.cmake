foreach(required
        PREVIOUS_JANUS
        CURRENT_JANUS
        FIXTURE_DIR
        OUTPUT_DIR
        EXECUTABLE_SUFFIX)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

include("${FIXTURE_DIR}/../runtime/compare_janus_output.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIR}")

foreach(fixture language ownership stdlib)
    set(source "${FIXTURE_DIR}/${fixture}.janus")
    set(expected "${FIXTURE_DIR}/${fixture}.expected.txt")
    if(NOT EXISTS "${source}" OR NOT EXISTS "${expected}")
        message(FATAL_ERROR "compatibility fixture is incomplete: ${fixture}")
    endif()

    foreach(toolchain previous current)
        if(toolchain STREQUAL "previous")
            set(janus "${PREVIOUS_JANUS}")
        else()
            set(janus "${CURRENT_JANUS}")
        endif()
        set(executable
            "${OUTPUT_DIR}/${fixture}-${toolchain}${EXECUTABLE_SUFFIX}")
        set(actual "${OUTPUT_DIR}/${fixture}-${toolchain}.txt")

        execute_process(
            COMMAND "${janus}" build "${source}" -o "${executable}"
            OUTPUT_VARIABLE build_output
            ERROR_VARIABLE build_error
            RESULT_VARIABLE build_result
        )
        if(NOT build_result EQUAL 0)
            message(FATAL_ERROR
                "${toolchain} Janus failed to build ${fixture}:\n"
                "${build_output}${build_error}")
        endif()

        execute_process(
            COMMAND "${executable}"
            OUTPUT_FILE "${actual}"
            ERROR_VARIABLE run_error
            RESULT_VARIABLE run_result
        )
        if(NOT run_result EQUAL 0)
            message(FATAL_ERROR
                "${fixture} built by ${toolchain} Janus failed "
                "(${run_result}):\n${run_error}")
        endif()
        compare_janus_output("${expected}" "${actual}")
    endforeach()
endforeach()

message(STATUS "Janus N/N+1 compatibility fixtures passed")

