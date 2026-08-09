cmake_minimum_required(VERSION 3.21)

project(janus_toolchain_wrapper_install_test LANGUAGES NONE)

foreach(required_variable
        RESOLVER_MODULE CLANG_WRAPPER LLD_WRAPPER LLD_PROGRAM_NAME)
    if(NOT DEFINED ${required_variable}
       OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR "missing ${required_variable}")
    endif()
endforeach()

include("${RESOLVER_MODULE}")
janus_resolve_toolchain_executable(
    TOOL Clang
    DISCOVERED "${CLANG_WRAPPER}"
    OVERRIDE ""
    OVERRIDE_VARIABLE JANUS_CLANG_REAL_EXECUTABLE
    QUERY_EXECUTABLE "${CLANG_WRAPPER}"
    QUERY_PROGRAM clang
    OUTPUT RESOLVED_CLANG
)
janus_resolve_toolchain_executable(
    TOOL LLD
    DISCOVERED "${LLD_WRAPPER}"
    OVERRIDE ""
    OVERRIDE_VARIABLE JANUS_LLD_REAL_EXECUTABLE
    QUERY_EXECUTABLE "${RESOLVED_CLANG}"
    QUERY_PROGRAM "${LLD_PROGRAM_NAME}"
    OUTPUT RESOLVED_LLD
)

add_executable(test_clang IMPORTED GLOBAL)
set_target_properties(test_clang PROPERTIES IMPORTED_LOCATION "${RESOLVED_CLANG}")
add_executable(test_lld IMPORTED GLOBAL)
set_target_properties(test_lld PROPERTIES IMPORTED_LOCATION "${RESOLVED_LLD}")

install(
    IMPORTED_RUNTIME_ARTIFACTS test_clang test_lld
    RUNTIME_DEPENDENCY_SET test_toolchain_dependencies
    RUNTIME DESTINATION bin
)
install(
    RUNTIME_DEPENDENCY_SET test_toolchain_dependencies
    POST_EXCLUDE_REGEXES ".*"
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    FRAMEWORK DESTINATION lib
)
