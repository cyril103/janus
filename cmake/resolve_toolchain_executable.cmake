function(_janus_toolchain_path_is_script executable result)
    file(READ "${executable}" executable_magic OFFSET 0 LIMIT 2 HEX)
    string(TOLOWER "${executable_magic}" executable_magic)
    if(executable_magic STREQUAL "2321")
        set("${result}" TRUE PARENT_SCOPE)
    else()
        set("${result}" FALSE PARENT_SCOPE)
    endif()
endfunction()

function(_janus_toolchain_native_candidate candidate result)
    if(NOT IS_ABSOLUTE "${candidate}" OR NOT EXISTS "${candidate}"
       OR IS_DIRECTORY "${candidate}")
        set("${result}" "" PARENT_SCOPE)
        return()
    endif()

    get_filename_component(candidate_real "${candidate}" REALPATH)
    _janus_toolchain_path_is_script("${candidate_real}" candidate_is_script)
    if(candidate_is_script)
        set("${result}" "" PARENT_SCOPE)
    else()
        set("${result}" "${candidate_real}" PARENT_SCOPE)
    endif()
endfunction()

function(janus_resolve_toolchain_executable)
    cmake_parse_arguments(
        ARG
        ""
        "TOOL;DISCOVERED;OVERRIDE;OVERRIDE_VARIABLE;QUERY_EXECUTABLE;QUERY_PROGRAM;OUTPUT"
        ""
        ${ARGN}
    )
    foreach(required_argument
            TOOL DISCOVERED OVERRIDE_VARIABLE OUTPUT)
        if(NOT DEFINED ARG_${required_argument}
           OR ARG_${required_argument} STREQUAL "")
            message(FATAL_ERROR
                    "janus_resolve_toolchain_executable missing ${required_argument}")
        endif()
    endforeach()

    if(NOT "${ARG_OVERRIDE}" STREQUAL "")
        _janus_toolchain_native_candidate("${ARG_OVERRIDE}" resolved_executable)
        if("${resolved_executable}" STREQUAL "")
            message(FATAL_ERROR
                    "${ARG_OVERRIDE_VARIABLE} must name a native ${ARG_TOOL} executable, not a script: ${ARG_OVERRIDE}")
        endif()
        set("${ARG_OUTPUT}" "${resolved_executable}" PARENT_SCOPE)
        return()
    endif()

    _janus_toolchain_native_candidate("${ARG_DISCOVERED}" resolved_executable)
    if(NOT "${resolved_executable}" STREQUAL "")
        set("${ARG_OUTPUT}" "${resolved_executable}" PARENT_SCOPE)
        return()
    endif()

    file(READ "${ARG_DISCOVERED}" wrapper_contents)
    string(REGEX MATCH
           "exec[ \t]+['\"]?(/[^'\" \t\r\n]+)"
           wrapper_exec_match "${wrapper_contents}")
    if(NOT "${CMAKE_MATCH_1}" STREQUAL "")
        _janus_toolchain_native_candidate(
            "${CMAKE_MATCH_1}" resolved_executable
        )
        if(NOT "${resolved_executable}" STREQUAL "")
            set("${ARG_OUTPUT}" "${resolved_executable}" PARENT_SCOPE)
            return()
        endif()
    endif()

    if(NOT "${ARG_QUERY_EXECUTABLE}" STREQUAL ""
       AND NOT "${ARG_QUERY_PROGRAM}" STREQUAL "")
        execute_process(
            COMMAND "${ARG_QUERY_EXECUTABLE}"
                    "-print-prog-name=${ARG_QUERY_PROGRAM}"
            RESULT_VARIABLE query_status
            OUTPUT_VARIABLE query_candidate
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(query_status EQUAL 0)
            _janus_toolchain_native_candidate(
                "${query_candidate}" resolved_executable
            )
            if(NOT "${resolved_executable}" STREQUAL "")
                set("${ARG_OUTPUT}" "${resolved_executable}" PARENT_SCOPE)
                return()
            endif()
        endif()
    endif()

    message(FATAL_ERROR
            "Could not resolve the native ${ARG_TOOL} executable behind wrapper '${ARG_DISCOVERED}'. Set ${ARG_OVERRIDE_VARIABLE} to the native executable path.")
endfunction()
