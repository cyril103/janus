cmake_minimum_required(VERSION 3.21)

project(
    janus
    VERSION 0.3.0
    DESCRIPTION "A compiler with an LLVM backend"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)

find_package(LLVM REQUIRED CONFIG)
list(GET LLVM_INCLUDE_DIRS 0 JANUS_LLVM_INCLUDE_DIR)
find_program(JANUS_CLANG_EXECUTABLE clang REQUIRED)
find_program(JANUS_LLD_EXECUTABLE NAMES ld.lld lld-link ld64.lld REQUIRED)
get_filename_component(
    JANUS_CLANG_REAL_EXECUTABLE "${JANUS_CLANG_EXECUTABLE}" REALPATH
)
get_filename_component(
    JANUS_LLD_REAL_EXECUTABLE "${JANUS_LLD_EXECUTABLE}" REALPATH
)
get_filename_component(
    JANUS_CLANG_RUNTIME_DIRECTORY "${JANUS_CLANG_REAL_EXECUTABLE}" DIRECTORY
)
get_filename_component(
    JANUS_LLD_RUNTIME_DIRECTORY "${JANUS_LLD_REAL_EXECUTABLE}" DIRECTORY
)
get_filename_component(
    JANUS_CLANG_REAL_NAME "${JANUS_CLANG_REAL_EXECUTABLE}" NAME
)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(JANUS_CLANG_INSTALL_NAME "clang.real")
elseif(WIN32)
    set(JANUS_CLANG_INSTALL_NAME "clang.exe")
else()
    set(JANUS_CLANG_INSTALL_NAME "clang")
endif()
get_filename_component(JANUS_LLD_INSTALL_NAME "${JANUS_LLD_EXECUTABLE}" NAME)
execute_process(
    COMMAND "${JANUS_CLANG_REAL_EXECUTABLE}" -print-resource-dir
    OUTPUT_VARIABLE JANUS_CLANG_RESOURCE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)

add_library(janus_runtime STATIC
    runtime/janus_graphics.c
    runtime/janus_runtime.c
)
target_compile_options(janus_runtime
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_library(janus_types
    src/types/type.cpp
)
target_include_directories(janus_types
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)
target_compile_options(janus_types
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_library(janus_frontend
    src/frontend/lexer.cpp
    src/frontend/module_loader.cpp
    src/frontend/parser.cpp
)
target_include_directories(janus_frontend
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(janus_frontend
    PUBLIC
        janus_types
)
target_compile_options(janus_frontend
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_library(janus_constant
    src/constant/evaluator.cpp
)
target_include_directories(janus_constant
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(janus_constant
    PUBLIC
        janus_types
)
target_compile_options(janus_constant
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_library(janus_semantic
    src/semantic/analyzer.cpp
)
target_include_directories(janus_semantic
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(janus_semantic
    PUBLIC
        janus_frontend
        janus_constant
)
target_compile_options(janus_semantic
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_library(janus_diagnostics
    src/diagnostics/high_growth_loop_linter.cpp
)
target_include_directories(janus_diagnostics
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(janus_diagnostics
    PUBLIC
        janus_frontend
)
target_compile_options(janus_diagnostics
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_library(janus_llvm_backend
    src/backend/llvm/type_lowering.cpp
    src/backend/llvm/ir_generator.cpp
    src/backend/llvm/object_emitter.cpp
)
target_include_directories(janus_llvm_backend
    SYSTEM PUBLIC
        ${JANUS_LLVM_INCLUDE_DIR}
)
target_link_libraries(janus_llvm_backend
    PUBLIC
        janus_types
        janus_frontend
        janus_constant
)
llvm_map_components_to_libnames(
    JANUS_LLVM_LIBS
    Core
    Support
    native
    nativecodegen
)
target_link_libraries(janus_llvm_backend
    PRIVATE
        ${JANUS_LLVM_LIBS}
)
target_compile_options(janus_llvm_backend
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_executable(janusc
    tools/janusc/main.cpp
)
target_link_libraries(janusc
    PRIVATE
        janus_semantic
        janus_llvm_backend
)
target_compile_options(janusc
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)
target_compile_definitions(janusc
    PRIVATE
        JANUS_STDLIB_DIR="${PROJECT_SOURCE_DIR}/stdlib"
)

add_library(janus_driver
    src/driver/formatter.cpp
    src/driver/native_linker.cpp
    src/driver/manifest.cpp
    src/driver/project.cpp
    src/driver/dependency.cpp
    src/driver/semver.cpp
    src/driver/registry.cpp
)
target_include_directories(janus_driver
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)
target_compile_options(janus_driver
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)
target_compile_definitions(janus_driver
    PRIVATE
        JANUS_CLANG_PATH="${JANUS_CLANG_EXECUTABLE}"
)

add_executable(janus
    tools/janus/main.cpp
)
add_dependencies(janus janus_runtime)
target_link_libraries(janus
    PRIVATE
        janus_driver
        janus_diagnostics
        janus_semantic
        janus_llvm_backend
)
target_compile_options(janus
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)
target_compile_definitions(janus
    PRIVATE
        JANUS_STDLIB_DIR="${PROJECT_SOURCE_DIR}/stdlib"
        JANUS_VERSION="${PROJECT_VERSION}"
        JANUS_CLANG_PATH="${JANUS_CLANG_EXECUTABLE}"
        JANUS_RUNTIME_LIBRARY="$<TARGET_FILE:janus_runtime>"
)
if(APPLE)
    set(JANUS_INSTALL_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
else()
    set(JANUS_INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
endif()
set_target_properties(janus janusc
    PROPERTIES INSTALL_RPATH "${JANUS_INSTALL_RPATH}"
)

add_executable(janusup
    tools/janusup/main.cpp
)
target_include_directories(janusup
    SYSTEM PRIVATE
        ${JANUS_LLVM_INCLUDE_DIR}
)
target_link_libraries(janusup
    PRIVATE
        ${JANUS_LLVM_LIBS}
)
target_compile_options(janusup
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)
target_compile_definitions(janusup
    PRIVATE
        JANUS_VERSION="${PROJECT_VERSION}"
)

add_executable(janus-lsp
    tools/janus-lsp/main.cpp
)
add_library(janus_lsp
    src/lsp/server.cpp
)
target_include_directories(janus_lsp
    SYSTEM PRIVATE
        ${JANUS_LLVM_INCLUDE_DIR}
)
target_link_libraries(janus_lsp
    PUBLIC
        janus_semantic
    PRIVATE
        janus_driver
        ${JANUS_LLVM_LIBS}
)
target_compile_options(janus_lsp
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)
target_compile_definitions(janus_lsp
    PRIVATE
        JANUS_VERSION="${PROJECT_VERSION}"
)
target_link_libraries(janus-lsp
    PRIVATE
        janus_lsp
)
target_compile_options(janus-lsp
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)
target_compile_definitions(janus-lsp
    PRIVATE
        JANUS_VERSION="${PROJECT_VERSION}"
        JANUS_STDLIB_DIR="${PROJECT_SOURCE_DIR}/stdlib"
)

include(CTest)
if(BUILD_TESTING)
    set(CLANG_EXECUTABLE "${JANUS_CLANG_EXECUTABLE}")

    add_executable(graphics_runtime_test
        tests/runtime/graphics_runtime_test.c
    )
    target_link_libraries(graphics_runtime_test PRIVATE janus_runtime)
    target_compile_options(graphics_runtime_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME runtime.graphics_loader COMMAND graphics_runtime_test)

    add_library(fake_raylib SHARED tests/runtime/fake_raylib.c)
    target_compile_options(fake_raylib
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_executable(graphics_backend_test
        tests/runtime/graphics_backend_test.c
    )
    target_link_libraries(graphics_backend_test PRIVATE janus_runtime)
    target_compile_options(graphics_backend_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME runtime.graphics_backend COMMAND graphics_backend_test)
    set_tests_properties(runtime.graphics_backend
        PROPERTIES
            ENVIRONMENT "JANUS_RAYLIB_PATH=$<TARGET_FILE:fake_raylib>"
    )

    add_test(
        NAME cli.check
        COMMAND janus check "${PROJECT_SOURCE_DIR}/examples/value.janus"
    )
    add_test(NAME lsp.version COMMAND janus-lsp --version)
    add_executable(parser_top_level_declaration_test
        tests/frontend/parser_top_level_declaration_test.cpp
    )
    target_link_libraries(parser_top_level_declaration_test PRIVATE janus_frontend)
    target_compile_options(parser_top_level_declaration_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(
        NAME parser.top_level_declaration
        COMMAND parser_top_level_declaration_test
    )
    add_test(
        NAME cli.check_top_level_val
        COMMAND janus check
                "${PROJECT_SOURCE_DIR}/tests/fixtures/parser/top_level_val.janus"
    )
    add_test(
        NAME cli.check_top_level_var
        COMMAND janus check
                "${PROJECT_SOURCE_DIR}/tests/fixtures/parser/top_level_var.janus"
    )
    add_executable(lsp_server_test
        tests/lsp/server_test.cpp
    )
    target_link_libraries(lsp_server_test PRIVATE janus_lsp)
    target_compile_definitions(lsp_server_test
        PRIVATE
            JANUS_STDLIB_DIR="${PROJECT_SOURCE_DIR}/stdlib"
    )
    add_test(NAME lsp.server COMMAND lsp_server_test)
    add_test(
        NAME cli.emit_llvm
        COMMAND janus build "${PROJECT_SOURCE_DIR}/examples/value.janus"
                --emit llvm-ir
                -o "${PROJECT_BINARY_DIR}/cli_value.ll"
    )
    add_test(
        NAME cli.emit_object
        COMMAND janus build "${PROJECT_SOURCE_DIR}/examples/value.janus"
                --emit object
                -o "${PROJECT_BINARY_DIR}/cli_value.o"
    )
    add_test(
        NAME cli.build
        COMMAND janus build "${PROJECT_SOURCE_DIR}/examples/value.janus"
                -o "${PROJECT_BINARY_DIR}/cli_value"
    )
    add_test(
        NAME cli.run
        COMMAND janus run "${PROJECT_SOURCE_DIR}/examples/loop_jumps.janus"
    )
    add_test(
        NAME cli.high_growth_loop_warnings
        COMMAND "${CMAKE_COMMAND}"
                "-DJANUS=$<TARGET_FILE:janus>"
                "-DWORK_DIR=${PROJECT_BINARY_DIR}/high-growth-loop-cli"
                -P "${PROJECT_SOURCE_DIR}/tests/diagnostics/run_high_growth_loop_cli.cmake"
    )
    add_test(
        NAME toolchain.install
        COMMAND "${CMAKE_COMMAND}"
                -DBUILD_DIR=${PROJECT_BINARY_DIR}
                -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
                -DPACKAGE_PLATFORM=${CMAKE_SYSTEM_NAME}
                -DPACKAGE_ARCH=${CMAKE_SYSTEM_PROCESSOR}
                -DJANUS=$<TARGET_FILE:janus>
                -DJANUSUP=$<TARGET_FILE:janusup>
                -P "${PROJECT_SOURCE_DIR}/tests/driver/run_janusup.cmake"
    )
    if(UNIX)
        add_test(
            NAME toolchain.install_raylib_script
            COMMAND sh
                    "${PROJECT_SOURCE_DIR}/tests/driver/install_raylib_script_test.sh"
                    "${PROJECT_SOURCE_DIR}"
        )
        add_test(
            NAME toolchain.install_script
            COMMAND sh
                    "${PROJECT_SOURCE_DIR}/tests/driver/install_script_test.sh"
                    "${PROJECT_SOURCE_DIR}"
        )
        add_test(
            NAME toolchain.verify_janus_euler_suite
            COMMAND sh
                    "${PROJECT_SOURCE_DIR}/tests/driver/verify_janus_euler_suite_test.sh"
                    "${PROJECT_SOURCE_DIR}"
        )
        add_test(
            NAME project_euler.fixture_structure
            COMMAND sh
                    "${PROJECT_SOURCE_DIR}/tests/driver/project_euler_fixture_structure_test.sh"
                    "${PROJECT_SOURCE_DIR}"
        )
        add_test(
            NAME project_euler.smoke
            COMMAND sh
                    "${PROJECT_SOURCE_DIR}/scripts/verify-janus-euler-suite.sh"
                    --project
                    "${PROJECT_SOURCE_DIR}/tests/fixtures/project-euler"
                    --config
                    "${PROJECT_SOURCE_DIR}/tests/fixtures/project-euler/production.txt"
                    --janus
                    $<TARGET_FILE:janus>
                    --all
                    --timeout
                    5
                    --global-timeout
                    120
                    --artifacts-dir
                    "${PROJECT_BINARY_DIR}/project-euler-smoke-artifacts"
        )
        set_tests_properties(project_euler.smoke
            PROPERTIES
                TIMEOUT 150
                RUN_SERIAL TRUE
                LABELS "project-euler;smoke"
        )
    endif()
    add_executable(manifest_test
        tests/driver/manifest_test.cpp
    )
    target_link_libraries(manifest_test
        PRIVATE
            janus_driver
    )
    target_compile_options(manifest_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME driver.manifest COMMAND manifest_test)
    add_executable(semver_test
        tests/driver/semver_test.cpp
    )
    target_link_libraries(semver_test PRIVATE janus_driver)
    target_compile_options(semver_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME driver.semver COMMAND semver_test)
    add_executable(formatter_test
        tests/driver/formatter_test.cpp
    )
    target_link_libraries(formatter_test PRIVATE janus_driver)
    target_compile_options(formatter_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME driver.formatter COMMAND formatter_test)
    add_test(
        NAME cli.project_creation
        COMMAND "${CMAKE_COMMAND}"
                -DBUILD_DIR=${PROJECT_BINARY_DIR}
                -DJANUS=$<TARGET_FILE:janus>
                -P "${PROJECT_SOURCE_DIR}/tests/driver/run_project_creation.cmake"
    )
    add_test(
        NAME cli.execution_contract
        COMMAND "${CMAKE_COMMAND}"
                -DBUILD_DIR=${PROJECT_BINARY_DIR}
                -DJANUS=$<TARGET_FILE:janus>
                -P "${PROJECT_SOURCE_DIR}/tests/driver/run_cli_execution_contract.cmake"
    )

    add_executable(int_type_test
        tests/types/int_type_test.cpp
    )
    target_link_libraries(int_type_test
        PRIVATE
            janus_llvm_backend
    )
    target_include_directories(int_type_test
        SYSTEM PRIVATE
            ${JANUS_LLVM_INCLUDE_DIR}
    )
    target_compile_options(int_type_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME types.int COMMAND int_type_test)

    add_executable(builtin_types_test
        tests/types/builtin_types_test.cpp
    )
    target_link_libraries(builtin_types_test
        PRIVATE
            janus_llvm_backend
    )
    target_compile_options(builtin_types_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME types.builtins COMMAND builtin_types_test)

    add_executable(val_declaration_test
        tests/language/val_declaration_test.cpp
    )
    target_link_libraries(val_declaration_test
        PRIVATE
            janus_semantic
            janus_llvm_backend
    )
    target_compile_options(val_declaration_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME language.val_declaration COMMAND val_declaration_test)

    add_executable(extern_function_test
        tests/language/extern_function_test.cpp
    )
    target_link_libraries(extern_function_test
        PRIVATE
            janus_semantic
            janus_llvm_backend
    )
    target_compile_options(extern_function_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME language.extern_function COMMAND extern_function_test)

    add_library(janus_c_interop_fixture STATIC
        tests/interop/c_abi_fixture.c
    )
    target_compile_options(janus_c_interop_fixture
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(
        NAME language.c_interop
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/interop/c_abi.janus"
            "-DFIXTURE=$<TARGET_FILE:janus_c_interop_fixture>"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/c_interop"
            -P "${PROJECT_SOURCE_DIR}/tests/interop/run_c_interop.cmake"
    )
    add_test(
        NAME language.loop_jumps_runtime
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/examples/loop_jumps.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/loop_jumps_runtime"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_example.cmake"
    )
    add_test(
        NAME docs.canonical_output
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/examples/canonical_output.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/canonical_output"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/canonical_output.expected.txt"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_example.cmake"
    )
    add_test(
        NAME runtime.stdout_normalization
        COMMAND
            ${CMAKE_COMMAND}
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/verify_stdout_normalization.cmake"
    )

    add_executable(generic_function_test
        tests/language/generic_function_test.cpp
    )
    target_link_libraries(generic_function_test
        PRIVATE
            janus_semantic
            janus_llvm_backend
    )
    target_compile_options(generic_function_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME language.generic_function COMMAND generic_function_test)

    add_executable(first_class_function_test
        tests/language/first_class_function_test.cpp
    )
    target_link_libraries(first_class_function_test
        PRIVATE
            janus_semantic
            janus_llvm_backend
    )
    target_compile_options(first_class_function_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME language.first_class_function COMMAND first_class_function_test)

    add_executable(mutable_variable_test
        tests/language/mutable_variable_test.cpp
    )
    target_link_libraries(mutable_variable_test
        PRIVATE
            janus_semantic
            janus_llvm_backend
    )
    target_compile_options(mutable_variable_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME language.mutable_variable COMMAND mutable_variable_test)

    add_executable(global_variable_test
        tests/language/global_variable_test.cpp
    )
    target_link_libraries(global_variable_test
        PRIVATE
            janus_semantic
            janus_llvm_backend
    )
    target_compile_definitions(global_variable_test
        PRIVATE
            JANUS_GLOBALS_ENTRY="${PROJECT_SOURCE_DIR}/tests/fixtures/modules/globals_entry.janus"
            JANUS_GLOBALS_PRIVATE_ACCESS="${PROJECT_SOURCE_DIR}/tests/fixtures/modules/globals_private_access.janus"
            JANUS_GLOBALS_PUBLIC_COLLISION="${PROJECT_SOURCE_DIR}/tests/fixtures/modules/globals_public_collision.janus"
    )
    target_compile_options(global_variable_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME language.global_variable COMMAND global_variable_test)
    add_test(
        NAME runtime.global_variable
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/examples/global_variables.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/global_variable_runtime"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/global_variables.expected.txt"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_example.cmake"
    )
    add_test(
        NAME runtime.global_panic_cleanup
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/global_panic_cleanup.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/global_panic_cleanup_runtime"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/global_panic_cleanup.expected.txt"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_panic_cleanup.cmake"
    )

    add_executable(class_test
        tests/language/class_test.cpp
    )
    target_link_libraries(class_test
        PRIVATE
            janus_semantic
            janus_llvm_backend
    )
    target_compile_options(class_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME language.class COMMAND class_test)

    add_executable(primitive_operations_test
        tests/language/primitive_operations_test.cpp
    )
    target_link_libraries(primitive_operations_test
        PRIVATE
            janus_semantic
            janus_llvm_backend
    )
    target_compile_options(primitive_operations_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME language.primitive_operations COMMAND primitive_operations_test)

    add_executable(integer_boundary_test
        tests/language/integer_boundary_test.cpp
    )
    target_link_libraries(integer_boundary_test
        PRIVATE
            janus_semantic
            janus_llvm_backend
    )
    target_compile_options(integer_boundary_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME language.integer_boundary COMMAND integer_boundary_test)
    add_test(
        NAME runtime.integer_boundary
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/integer_boundary.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/integer_boundary_runtime"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/integer_boundary.expected.txt"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_example.cmake"
    )
    add_test(
        NAME runtime.math_helpers
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/math_helpers.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/math_helpers_runtime"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/math_helpers.expected.txt"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_example.cmake"
    )
    add_test(
        NAME runtime.prime_factors
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/prime_factors.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/prime_factors_runtime"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/prime_factors.expected.txt"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_example.cmake"
    )
    add_test(
        NAME benchmarks.prime_factors_smoke
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/benchmarks/prime_factors.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/prime_factors_benchmark_smoke"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/benchmarks/prime_factors.expected.txt"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_example.cmake"
    )
    foreach(trap_case lcm_overflow lcm_overflow_reversed)
        add_test(
            NAME runtime.${trap_case}
            COMMAND
                ${CMAKE_COMMAND}
                "-DJANUSC=$<TARGET_FILE:janusc>"
                "-DCLANG=${CLANG_EXECUTABLE}"
                "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/${trap_case}.janus"
                "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
                "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/${trap_case}"
                "-DEXPECTED_ERROR=lcm overflow"
                -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_trap.cmake"
        )
    endforeach()
    foreach(trap_case
            byte_divide_by_zero
            byte_remainder_by_zero
            int_divide_by_zero
            int_remainder_by_zero
            usize_divide_by_zero
            usize_remainder_by_zero)
        add_test(
            NAME runtime.${trap_case}
            COMMAND
                ${CMAKE_COMMAND}
                "-DJANUSC=$<TARGET_FILE:janusc>"
                "-DCLANG=${CLANG_EXECUTABLE}"
                "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/${trap_case}.janus"
                "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
                "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/${trap_case}"
                "-DEXPECTED_ERROR=integer division by zero"
                -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_trap.cmake"
        )
    endforeach()
    foreach(trap_case
            byte_min_divide_overflow
            byte_min_remainder_overflow
            int_min_divide_overflow
            int_min_remainder_overflow)
        add_test(
            NAME runtime.${trap_case}
            COMMAND
                ${CMAKE_COMMAND}
                "-DJANUSC=$<TARGET_FILE:janusc>"
                "-DCLANG=${CLANG_EXECUTABLE}"
                "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/${trap_case}.janus"
                "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
                "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/${trap_case}"
                "-DEXPECTED_ERROR=integer division overflow"
                -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_trap.cmake"
        )
    endforeach()

    add_executable(generic_class_test
        tests/language/generic_class_test.cpp
    )
    target_link_libraries(generic_class_test
        PRIVATE
            janus_semantic
            janus_llvm_backend
    )
    target_compile_options(generic_class_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME language.generic_class COMMAND generic_class_test)

    add_executable(trait_declaration_test
        tests/language/trait_declaration_test.cpp
    )
    target_link_libraries(trait_declaration_test
        PRIVATE
            janus_semantic
            janus_llvm_backend
    )
    target_compile_options(trait_declaration_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME language.trait_declaration COMMAND trait_declaration_test)

    add_executable(generic_method_test
        tests/language/generic_method_test.cpp
    )
    target_link_libraries(generic_method_test
        PRIVATE
            janus_semantic
            janus_llvm_backend
    )
    target_compile_options(generic_method_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME language.generic_method COMMAND generic_method_test)

    add_executable(method_chaining_test
        tests/language/method_chaining_test.cpp
    )
    target_link_libraries(method_chaining_test
        PRIVATE
            janus_semantic
            janus_llvm_backend
    )
    target_compile_options(method_chaining_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME language.method_chaining COMMAND method_chaining_test)

    add_executable(move_semantics_test
        tests/language/move_semantics_test.cpp
    )
    target_link_libraries(move_semantics_test
        PRIVATE
            janus_semantic
            janus_llvm_backend
    )
    target_compile_options(move_semantics_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME language.move_semantics COMMAND move_semantics_test)

    add_executable(control_flow_test
        tests/language/control_flow_test.cpp
    )
    target_link_libraries(control_flow_test
        PRIVATE
            janus_semantic
            janus_llvm_backend
    )
    target_compile_options(control_flow_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME language.control_flow COMMAND control_flow_test)

    add_executable(if_expression_test
        tests/language/if_expression_test.cpp
    )
    target_link_libraries(if_expression_test
        PRIVATE
            janus_semantic
            janus_llvm_backend
    )
    target_compile_options(if_expression_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME language.if_expression COMMAND if_expression_test)

    add_executable(match_expression_test
        tests/language/match_expression_test.cpp
    )
    target_link_libraries(match_expression_test
        PRIVATE
            janus_semantic
            janus_llvm_backend
    )
    target_compile_options(match_expression_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME language.match_expression COMMAND match_expression_test)

    add_executable(try_operator_test
        tests/language/try_operator_test.cpp
    )
    target_link_libraries(try_operator_test
        PRIVATE
            janus_semantic
            janus_llvm_backend
    )
    target_compile_options(try_operator_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME language.try_operator COMMAND try_operator_test)

    add_executable(unit_type_test
        tests/language/unit_type_test.cpp
    )
    target_link_libraries(unit_type_test
        PRIVATE
            janus_semantic
            janus_llvm_backend
    )
    target_compile_options(unit_type_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME language.unit_type COMMAND unit_type_test)

    add_executable(constructor_initialization_test
        tests/language/constructor_initialization_test.cpp
    )
    target_link_libraries(constructor_initialization_test
        PRIVATE
            janus_semantic
            janus_llvm_backend
    )
    target_compile_options(constructor_initialization_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME language.constructor_initialization
        COMMAND constructor_initialization_test)

    add_executable(destructor_body_test
        tests/language/destructor_body_test.cpp
    )
    target_link_libraries(destructor_body_test
        PRIVATE
            janus_semantic
            janus_llvm_backend
    )
    target_compile_options(destructor_body_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME language.destructor_body COMMAND destructor_body_test)

    add_executable(usize_test
        tests/language/usize_test.cpp
    )
    target_link_libraries(usize_test
        PRIVATE
            janus_semantic
            janus_llvm_backend
    )
    target_compile_options(usize_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME language.usize COMMAND usize_test)

    add_executable(explicit_cast_test
        tests/language/explicit_cast_test.cpp
    )
    target_link_libraries(explicit_cast_test
        PRIVATE
            janus_semantic
            janus_llvm_backend
    )
    target_compile_options(explicit_cast_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME language.explicit_cast COMMAND explicit_cast_test)

    add_executable(enum_test
        tests/language/enum_test.cpp
    )
    target_link_libraries(enum_test
        PRIVATE
            janus_semantic
            janus_llvm_backend
    )
    target_compile_options(enum_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME language.enum COMMAND enum_test)

    add_executable(pointer_memory_test
        tests/language/pointer_memory_test.cpp
    )
    target_link_libraries(pointer_memory_test
        PRIVATE
            janus_semantic
            janus_llvm_backend
    )
    target_compile_options(pointer_memory_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME language.pointer_memory COMMAND pointer_memory_test)

    add_executable(panic_test
        tests/language/panic_test.cpp
    )
    target_link_libraries(panic_test
        PRIVATE
            janus_semantic
            janus_llvm_backend
    )
    target_compile_options(panic_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME language.panic COMMAND panic_test)

    add_executable(standard_output_test
        tests/language/standard_output_test.cpp
    )
    target_link_libraries(standard_output_test
        PRIVATE
            janus_semantic
            janus_llvm_backend
    )
    target_compile_options(standard_output_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME language.standard_output COMMAND standard_output_test)

    add_executable(dynamic_array_test
        tests/language/dynamic_array_test.cpp
    )
    target_link_libraries(dynamic_array_test
        PRIVATE
            janus_semantic
            janus_llvm_backend
    )
    target_compile_options(dynamic_array_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME language.dynamic_array COMMAND dynamic_array_test)

    add_executable(module_import_test
        tests/language/module_import_test.cpp
    )
    target_link_libraries(module_import_test
        PRIVATE
            janus_semantic
            janus_llvm_backend
    )
    target_compile_definitions(module_import_test
        PRIVATE
            JANUS_STDLIB_DIR="${PROJECT_SOURCE_DIR}/stdlib"
            JANUS_GLOBALS_ENTRY="${PROJECT_SOURCE_DIR}/tests/fixtures/modules/globals_entry.janus"
            JANUS_QUALIFIED_ENTRY="${PROJECT_SOURCE_DIR}/tests/fixtures/modules/qualified_entry.janus"
            JANUS_IDENTITY_ENTRY="${PROJECT_SOURCE_DIR}/tests/fixtures/modules/identity_entry.janus"
            JANUS_IDENTITY_AMBIGUOUS_ENTRY="${PROJECT_SOURCE_DIR}/tests/fixtures/modules/identity_ambiguous_entry.janus"
            JANUS_IDENTITY_AMBIGUOUS_FUNCTION_ENTRY="${PROJECT_SOURCE_DIR}/tests/fixtures/modules/identity_ambiguous_function_entry.janus"
            JANUS_ARRAY_EXAMPLE="${PROJECT_SOURCE_DIR}/examples/array.janus"
            JANUS_OPTION_RESULT_EXAMPLE="${PROJECT_SOURCE_DIR}/examples/option_result.janus"
            JANUS_RANGE_EXAMPLE="${PROJECT_SOURCE_DIR}/examples/range.janus"
            JANUS_ITERATOR_ADAPTERS_EXAMPLE="${PROJECT_SOURCE_DIR}/examples/iterator_adapters.janus"
    )
    target_compile_options(module_import_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME language.module_import COMMAND module_import_test)

    add_executable(graphics_module_test
        tests/language/graphics_module_test.cpp
    )
    target_link_libraries(graphics_module_test
        PRIVATE
            janus_semantic
            janus_llvm_backend
    )
    target_compile_options(graphics_module_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    target_compile_definitions(graphics_module_test
        PRIVATE
            JANUS_STDLIB_DIR="${PROJECT_SOURCE_DIR}/stdlib"
    )
    add_test(NAME language.graphics_module COMMAND graphics_module_test)

    add_executable(struct_value_test
        tests/language/struct_value_test.cpp
    )
    target_link_libraries(struct_value_test
        PRIVATE
            janus_semantic
            janus_llvm_backend
    )
    target_compile_options(struct_value_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME language.struct_value COMMAND struct_value_test)
endif()

add_executable(janus_bundled_clang IMPORTED GLOBAL)
set_target_properties(janus_bundled_clang
    PROPERTIES IMPORTED_LOCATION "${JANUS_CLANG_REAL_EXECUTABLE}"
)
add_executable(janus_bundled_lld IMPORTED GLOBAL)
set_target_properties(janus_bundled_lld
    PROPERTIES IMPORTED_LOCATION "${JANUS_LLD_REAL_EXECUTABLE}"
)

install(
    TARGETS janus janusc janusup janus-lsp
    RUNTIME_DEPENDENCY_SET janus_toolchain_dependencies
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(
    IMPORTED_RUNTIME_ARTIFACTS janus_bundled_clang janus_bundled_lld
    RUNTIME_DEPENDENCY_SET janus_toolchain_dependencies
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(
    PROGRAMS "${JANUS_LLD_REAL_EXECUTABLE}"
    TYPE BIN
    RENAME "${JANUS_LLD_INSTALL_NAME}"
)
install(
    RUNTIME_DEPENDENCY_SET janus_toolchain_dependencies
    DIRECTORIES
        "${JANUS_CLANG_RUNTIME_DIRECTORY}"
        "${JANUS_LLD_RUNTIME_DIRECTORY}"
    PRE_EXCLUDE_REGEXES
        "^api-ms-"
        "^ext-ms-"
        "^ld-linux"
        "^lib(c|m|pthread|dl|rt|gcc_s|stdc\\+\\+)\\.so"
    POST_EXCLUDE_REGEXES
        "^/System/Library/"
        "^/usr/lib/lib(System|c\\+\\+)"
        ".*[Ww]indows[/\\\\][Ss]ystem32.*"
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FRAMEWORK DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
if(NOT JANUS_CLANG_REAL_NAME STREQUAL JANUS_CLANG_INSTALL_NAME)
    install(CODE "
        file(RENAME
            \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/${JANUS_CLANG_REAL_NAME}\"
            \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/${JANUS_CLANG_INSTALL_NAME}\"
        )
    ")
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    install(
        PROGRAMS cmake/clang-wrapper.sh
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        RENAME clang
    )
endif()
if(UNIX)
    install(
        PROGRAMS scripts/install-raylib.sh
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        RENAME janus-install-raylib
    )
endif()
install(
    TARGETS janus_runtime
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(
    DIRECTORY "${JANUS_CLANG_RESOURCE_DIR}/"
    DESTINATION
        "${CMAKE_INSTALL_LIBDIR}/clang/${LLVM_VERSION_MAJOR}"
)
if(WIN32)
    find_file(
        JANUS_UNWIND_LIBRARY
        NAMES libunwind.a
        HINTS "${JANUS_CLANG_RUNTIME_DIRECTORY}/../lib"
        REQUIRED
    )
    install(
        FILES "${JANUS_UNWIND_LIBRARY}"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
    install(
        FILES "${JANUS_UNWIND_LIBRARY}"
        DESTINATION
            "${CMAKE_INSTALL_LIBDIR}/clang/${LLVM_VERSION_MAJOR}/lib/windows"
    )
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    file(GLOB JANUS_LINUX_SUPPORT_LIBRARIES
        "/lib/${CMAKE_LIBRARY_ARCHITECTURE}/libbsd.so*"
        "/lib/${CMAKE_LIBRARY_ARCHITECTURE}/libicudata.so*"
        "/lib/${CMAKE_LIBRARY_ARCHITECTURE}/libicuuc.so*"
        "/lib/${CMAKE_LIBRARY_ARCHITECTURE}/libmd.so*"
        "/lib/*/libbsd.so*"
        "/lib/*/libicudata.so*"
        "/lib/*/libicuuc.so*"
        "/lib/*/libmd.so*"
        "/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}/libbsd.so*"
        "/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}/libicudata.so*"
        "/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}/libicuuc.so*"
        "/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}/libmd.so*"
        "/usr/lib/*/libbsd.so*"
        "/usr/lib/*/libicudata.so*"
        "/usr/lib/*/libicuuc.so*"
        "/usr/lib/*/libmd.so*"
    )
    install(
        FILES ${JANUS_LINUX_SUPPORT_LIBRARIES}
        DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
    find_file(
        JANUS_ICUUC_SONAME
        NAMES libicuuc.so.74
        PATHS /lib/x86_64-linux-gnu /usr/lib/x86_64-linux-gnu
        NO_DEFAULT_PATH
    )
    find_file(
        JANUS_ICUDATA_SONAME
        NAMES libicudata.so.74
        PATHS /lib/x86_64-linux-gnu /usr/lib/x86_64-linux-gnu
        NO_DEFAULT_PATH
    )
    if(JANUS_ICUUC_SONAME AND JANUS_ICUDATA_SONAME)
        get_filename_component(
            JANUS_ICUUC_LIBRARY "${JANUS_ICUUC_SONAME}" REALPATH
        )
        get_filename_component(
            JANUS_ICUDATA_LIBRARY "${JANUS_ICUDATA_SONAME}" REALPATH
        )
        install(
            FILES
                "${JANUS_ICUUC_SONAME}"
                "${JANUS_ICUUC_LIBRARY}"
                "${JANUS_ICUDATA_SONAME}"
                "${JANUS_ICUDATA_LIBRARY}"
            DESTINATION ${CMAKE_INSTALL_LIBDIR}
        )
    endif()
endif()
install(
    DIRECTORY stdlib/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/janus/stdlib
)
install(
    FILES README.md CHANGELOG.md LICENSE NOTICE
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
)

set(JANUS_PACKAGE_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
if(JANUS_PACKAGE_ARCH MATCHES "^(aarch64|ARM64)$")
    set(JANUS_PACKAGE_ARCH "arm64")
endif()
set(CPACK_PACKAGE_VENDOR "Janus")
set(CPACK_PACKAGE_CONTACT "Janus contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_FILE_NAME
    "janus-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${JANUS_PACKAGE_ARCH}")
set(JANUS_PACKAGE_BASENAME "${CPACK_PACKAGE_FILE_NAME}")
set(CPACK_STRIP_FILES ON)
if(WIN32)
    set(CPACK_GENERATOR ZIP)
else()
    set(CPACK_GENERATOR TGZ)
endif()
set(JANUS_CPACK_GENERATOR "${CPACK_GENERATOR}")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
include(CPack)

if(JANUS_CPACK_GENERATOR STREQUAL "TGZ")
    set(JANUS_PACKAGE_ARCHIVE
        "${PROJECT_BINARY_DIR}/${JANUS_PACKAGE_BASENAME}.tar.gz")
elseif(JANUS_CPACK_GENERATOR STREQUAL "ZIP")
    set(JANUS_PACKAGE_ARCHIVE
        "${PROJECT_BINARY_DIR}/${JANUS_PACKAGE_BASENAME}.zip")
endif()
add_custom_target(dist
    COMMAND "${CMAKE_CPACK_COMMAND}" --config "${PROJECT_BINARY_DIR}/CPackConfig.cmake"
    COMMAND "${CMAKE_COMMAND}"
            -DARCHIVE=${JANUS_PACKAGE_ARCHIVE}
            -P "${PROJECT_SOURCE_DIR}/cmake/write_checksum.cmake"
    DEPENDS janus janusc janusup janus-lsp janus_runtime
    COMMENT "Building the Janus distribution and SHA-256 checksum"
)
