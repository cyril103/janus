foreach(required JANUSC CLANG RUNTIME SOURCE_DIR EXPECTED_DIR OUTPUT_ROOT CASE)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

include("${CMAKE_CURRENT_LIST_DIR}/../runtime/compare_janus_output.cmake")

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef nonce)
set(work_dir "${OUTPUT_ROOT}/${CASE}-${nonce}")
file(MAKE_DIRECTORY "${work_dir}")

if(CASE STREQUAL "safe_file")
    set(input_file "${work_dir}/entrée sûre.txt")
    file(WRITE "${input_file}" "alpha\nbeta\n")
    set(program_arguments "${input_file}")
elseif(CASE STREQUAL "cli")
    set(program_arguments "greet" "Ada Lovelace")
else()
    set(program_arguments)
endif()

foreach(style explicit idiomatic)
    set(source "${SOURCE_DIR}/${CASE}_${style}.janus")
    set(llvm_ir "${work_dir}/${style}.ll")
    # Script mode does not initialize CMAKE_EXECUTABLE_SUFFIX from a project.
    if(WIN32)
        set(executable "${work_dir}/${style}.exe")
    else()
        set(executable "${work_dir}/${style}")
    endif()
    set(actual "${work_dir}/${style}.stdout.txt")

    execute_process(
        COMMAND "${JANUSC}" "${source}"
        OUTPUT_FILE "${llvm_ir}"
        ERROR_VARIABLE compile_error
        RESULT_VARIABLE compile_result
        TIMEOUT 30
    )
    if(NOT compile_result EQUAL 0)
        message(FATAL_ERROR
                "compilation failed for ${source} (${compile_result}):\n${compile_error}")
    endif()

    set(link_options)
    if(UNIX)
        list(APPEND link_options -pthread)
    endif()
    if(UNIX AND NOT APPLE)
        list(APPEND link_options -lm)
    endif()
    execute_process(
        COMMAND "${CLANG}" -fsanitize=address -fno-omit-frame-pointer
                "${llvm_ir}" "${RUNTIME}" ${link_options} -o "${executable}"
        ERROR_VARIABLE link_error
        RESULT_VARIABLE link_result
        TIMEOUT 30
    )
    if(NOT link_result EQUAL 0)
        message(FATAL_ERROR
                "link failed for ${source} (${link_result}):\n${link_error}")
    endif()

    set(asan_options "detect_leaks=1:halt_on_error=1")
    if(WIN32 OR APPLE)
        set(asan_options "detect_leaks=0:halt_on_error=1")
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "ASAN_OPTIONS=${asan_options}"
                "${executable}" ${program_arguments}
        OUTPUT_FILE "${actual}"
        ERROR_VARIABLE program_error
        RESULT_VARIABLE program_result
        TIMEOUT 15
    )
    if(NOT program_result EQUAL 0)
        message(FATAL_ERROR
                "execution failed for ${source} (${program_result}):\n${program_error}")
    endif()
    compare_janus_output("${EXPECTED_DIR}/${CASE}.txt" "${actual}")
endforeach()

file(REMOVE_RECURSE "${work_dir}")
