cmake_minimum_required(VERSION 3.21)

project(
    janus
    VERSION 0.13.0
    DESCRIPTION "A compiler with an LLVM backend"
    LANGUAGES C CXX
)

set(JANUS_SOURCE_SHA "" CACHE STRING "Full source commit SHA embedded in packages")
string(LENGTH "${JANUS_SOURCE_SHA}" JANUS_SOURCE_SHA_LENGTH)
if(JANUS_SOURCE_SHA AND
   (NOT JANUS_SOURCE_SHA_LENGTH EQUAL 40 OR
    NOT JANUS_SOURCE_SHA MATCHES "^[0-9a-f]+$"))
    message(FATAL_ERROR
            "JANUS_SOURCE_SHA must be a full lowercase 40-character SHA")
endif()
set(JANUS_PACKAGE_VERSION "${PROJECT_VERSION}" CACHE STRING
    "Unique package version (nightly workflows override this)")
find_program(JANUS_PYTHON_EXECUTABLE NAMES python3 python REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)
include(cmake/resolve_toolchain_executable.cmake)

find_package(LLVM REQUIRED CONFIG)
include(cmake/validate_llvm_version.cmake)
file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/generated")
execute_process(
    COMMAND "${JANUS_PYTHON_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/scripts/build_identity.py"
            --version "${PROJECT_VERSION}"
            --source "${PROJECT_SOURCE_DIR}"
            --source-sha "${JANUS_SOURCE_SHA}"
            --target "${CMAKE_SYSTEM_PROCESSOR}-${CMAKE_SYSTEM_NAME}"
            --llvm "${LLVM_PACKAGE_VERSION}"
            --json-output "${PROJECT_BINARY_DIR}/build-identity.json"
            --header-output "${PROJECT_BINARY_DIR}/generated/janus-build-identity.hpp"
    COMMAND_ERROR_IS_FATAL ANY
)
add_custom_target(janus_build_identity ALL
    COMMAND "${JANUS_PYTHON_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/scripts/build_identity.py"
            --version "${PROJECT_VERSION}"
            --source "${PROJECT_SOURCE_DIR}"
            --source-sha "${JANUS_SOURCE_SHA}"
            --target "${CMAKE_SYSTEM_PROCESSOR}-${CMAKE_SYSTEM_NAME}"
            --llvm "${LLVM_PACKAGE_VERSION}"
            --json-output "${PROJECT_BINARY_DIR}/build-identity.json"
            --header-output "${PROJECT_BINARY_DIR}/generated/janus-build-identity.hpp"
    BYPRODUCTS "${PROJECT_BINARY_DIR}/build-identity.json"
               "${PROJECT_BINARY_DIR}/generated/janus-build-identity.hpp"
    VERBATIM
)
include_directories("${PROJECT_BINARY_DIR}/generated")
list(GET LLVM_INCLUDE_DIRS 0 JANUS_LLVM_INCLUDE_DIR)
find_program(JANUS_CLANG_EXECUTABLE clang REQUIRED)
find_program(JANUS_LLD_EXECUTABLE NAMES ld.lld lld-link ld64.lld REQUIRED)
set(JANUS_CLANG_REAL_EXECUTABLE "" CACHE FILEPATH
    "Native Clang executable to use when JANUS_CLANG_EXECUTABLE is a wrapper")
set(JANUS_LLD_REAL_EXECUTABLE "" CACHE FILEPATH
    "Native LLD executable to use when JANUS_LLD_EXECUTABLE is a wrapper")
janus_resolve_toolchain_executable(
    TOOL Clang
    DISCOVERED "${JANUS_CLANG_EXECUTABLE}"
    OVERRIDE "${JANUS_CLANG_REAL_EXECUTABLE}"
    OVERRIDE_VARIABLE JANUS_CLANG_REAL_EXECUTABLE
    QUERY_EXECUTABLE "${JANUS_CLANG_EXECUTABLE}"
    QUERY_PROGRAM clang
    OUTPUT JANUS_RESOLVED_CLANG_EXECUTABLE
)
get_filename_component(JANUS_LLD_PROGRAM_NAME "${JANUS_LLD_EXECUTABLE}" NAME)
janus_resolve_toolchain_executable(
    TOOL LLD
    DISCOVERED "${JANUS_LLD_EXECUTABLE}"
    OVERRIDE "${JANUS_LLD_REAL_EXECUTABLE}"
    OVERRIDE_VARIABLE JANUS_LLD_REAL_EXECUTABLE
    QUERY_EXECUTABLE "${JANUS_RESOLVED_CLANG_EXECUTABLE}"
    QUERY_PROGRAM "${JANUS_LLD_PROGRAM_NAME}"
    OUTPUT JANUS_RESOLVED_LLD_EXECUTABLE
)
set(JANUS_CLANG_REAL_EXECUTABLE "${JANUS_RESOLVED_CLANG_EXECUTABLE}")
set(JANUS_LLD_REAL_EXECUTABLE "${JANUS_RESOLVED_LLD_EXECUTABLE}")
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
    COMMAND "${JANUS_CLANG_EXECUTABLE}" -print-resource-dir
    OUTPUT_VARIABLE JANUS_CLANG_RESOURCE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)

add_library(janus_runtime STATIC
    runtime/janus_fs.c
    runtime/janus_graphics.c
    runtime/janus_io.c
    runtime/janus_math.c
    runtime/janus_process.c
    runtime/janus_runtime.c
    runtime/janus_system.c
    runtime/janus_time_random.c
)
target_compile_options(janus_runtime
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)
if(UNIX AND NOT APPLE)
    target_link_libraries(janus_runtime PUBLIC m)
endif()

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
llvm_map_components_to_libnames(JANUS_CONSTANT_LLVM_LIBS Support)
target_include_directories(janus_constant
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
    SYSTEM PRIVATE
        ${JANUS_LLVM_INCLUDE_DIR}
)
target_link_libraries(janus_constant
    PUBLIC
        janus_types
    PRIVATE
        ${JANUS_CONSTANT_LLVM_LIBS}
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
    src/diagnostics/renderer.cpp
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
    BitReader
    BitWriter
    Linker
    Support
    native
    nativecodegen
)
target_link_libraries(janus_llvm_backend
    PRIVATE
        janus_semantic
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
        janus_diagnostics
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
    src/driver/api_index.cpp
    src/driver/documentation.cpp
    src/driver/doctest.cpp
    src/driver/formatter.cpp
    src/driver/incremental_cache.cpp
    src/driver/output_publication_lock.cpp
    src/driver/native_linker.cpp
    src/driver/manifest.cpp
    src/driver/native_test.cpp
    src/driver/project.cpp
    src/driver/dependency.cpp
    src/driver/remote_registry.cpp
    src/driver/semver.cpp
    src/driver/registry.cpp
    src/driver/temporary_directory.cpp
)
target_link_libraries(janus_driver
    PUBLIC
        janus_frontend
    PRIVATE
        janus_constant
        janus_semantic
)
llvm_map_components_to_libnames(JANUS_DRIVER_LLVM_LIBS Support)
target_link_libraries(janus_driver PRIVATE ${JANUS_DRIVER_LLVM_LIBS})
target_include_directories(janus_driver SYSTEM PRIVATE ${JANUS_LLVM_INCLUDE_DIR})
if(WIN32)
    target_link_libraries(janus_driver PRIVATE shell32)
endif()
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
add_dependencies(janus janus_runtime janus_build_identity)
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
function(configure_janusup_target target)
    add_dependencies(${target} janus_build_identity)
    target_include_directories(${target}
        SYSTEM PRIVATE
            ${JANUS_LLVM_INCLUDE_DIR}
        PRIVATE
            ${PROJECT_SOURCE_DIR}/include
    )
    target_link_libraries(${target}
        PRIVATE
            ${JANUS_LLVM_LIBS}
    )
    target_compile_options(${target}
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    target_compile_definitions(${target}
        PRIVATE
            JANUS_VERSION="${PROJECT_VERSION}"
    )
endfunction()
configure_janusup_target(janusup)

add_executable(janus-lsp
    tools/janus-lsp/main.cpp
)
add_dependencies(janus-lsp janus_build_identity)
add_library(janus_lsp
    src/lsp/server.cpp
)
add_dependencies(janus_lsp janus_build_identity)
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
        JANUS_STDLIB_API_INDEX="${PROJECT_SOURCE_DIR}/website/docs/reference/stdlib/api-index.json"
)

include(CTest)
if(BUILD_TESTING)
    add_executable(janusup-test tools/janusup/main.cpp)
    configure_janusup_target(janusup-test)
    target_compile_definitions(janusup-test PRIVATE JANUSUP_TEST_HOOKS=1)
    set_target_properties(janusup-test PROPERTIES OUTPUT_NAME janusup-test-hooks)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(CLANG_EXECUTABLE "${JANUS_CLANG_EXECUTABLE}")

    add_test(
        NAME language.local_type_inference.cli
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/local_type_inference.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/local_type_inference_cli"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_example.cmake"
    )

    add_test(
        NAME tests.no_standard_assert
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/check-test-asserts.py"
                "${PROJECT_SOURCE_DIR}/tests"
    )
    add_test(
        NAME tests.no_standard_assert.guard_self_test
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/check-test-asserts.py"
                --self-test
    )
    add_test(
        NAME ci.github_actions_pinned
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/check-github-actions-pins.py"
                "${PROJECT_SOURCE_DIR}"
    )
    add_test(
        NAME ci.github_actions_pinned.guard_self_test
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/check-github-actions-pins.py"
                --self-test
    )
    add_test(
        NAME build.llvm_support_policy
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/build/test_llvm_support_policy.py"
                "${PROJECT_SOURCE_DIR}"
    )
    add_test(
        NAME ci.vscode_release_workflow_contract
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/check-vscode-release-workflows.py"
                "${PROJECT_SOURCE_DIR}"
    )
    add_test(
        NAME ci.vscode_release_workflow_contract.guard_self_test
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/check-vscode-release-workflows.py"
                --self-test
                "${PROJECT_SOURCE_DIR}"
    )
    add_test(
        NAME release.nightly_policy
        COMMAND "${Python3_EXECUTABLE}" -m unittest discover
                -s "${PROJECT_SOURCE_DIR}/tests/release"
                -p "test_*.py"
                -v
    )

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

    add_executable(floating_runtime_test
        tests/runtime/floating_runtime_test.c
    )
    target_link_libraries(floating_runtime_test PRIVATE janus_runtime)
    if(UNIX)
        target_link_libraries(floating_runtime_test PRIVATE m)
    endif()
    target_compile_options(floating_runtime_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME runtime.floating COMMAND floating_runtime_test)

    add_test(
        NAME runtime.floating_math
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/floating_math.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/floating_math_runtime"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/floating_math.expected.txt"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_example.cmake"
    )

    add_executable(time_random_runtime_test
        tests/runtime/time_random_runtime_test.c
    )
    target_link_libraries(time_random_runtime_test PRIVATE janus_runtime)
    target_compile_options(time_random_runtime_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME runtime.time_random_native COMMAND time_random_runtime_test)

    add_executable(system_runtime_test
        tests/runtime/system_runtime_test.c
    )
    target_link_libraries(system_runtime_test PRIVATE janus_runtime)
    target_compile_options(system_runtime_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME runtime.system_native COMMAND system_runtime_test)

    add_executable(path_fs_runtime_test
        tests/runtime/path_fs_runtime_test.c
    )
    target_link_libraries(path_fs_runtime_test PRIVATE janus_runtime)
    target_compile_options(path_fs_runtime_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME runtime.path_fs_native COMMAND path_fs_runtime_test)

    add_executable(io_runtime_test
        tests/runtime/io_runtime_test.c
    )
    target_link_libraries(io_runtime_test PRIVATE janus_runtime)
    target_compile_options(io_runtime_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME runtime.io_native COMMAND io_runtime_test)

    add_executable(process_runtime_test
        tests/runtime/process_runtime_test.c
    )
    target_link_libraries(process_runtime_test PRIVATE janus_runtime)
    target_compile_options(process_runtime_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME runtime.process_native COMMAND process_runtime_test)

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
        NAME runtime.graphics_resource_ownership
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/graphics_resource_ownership.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/graphics_resource_ownership_runtime"
            "-DPROGRAM_ENVIRONMENT=JANUS_RAYLIB_PATH=$<TARGET_FILE:fake_raylib>"
            "-DENABLE_UNDEFINED_SANITIZER=ON"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_example.cmake"
    )
    add_test(
        NAME language.graphics_resource_rejects_reuse_after_move
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/graphics_resource_move_error.janus"
            "-DEXPECTED_ERROR=variable 'texture' is used before initialization"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_compile_error.cmake"
    )

    add_test(
        NAME cli.check
        COMMAND janus check "${PROJECT_SOURCE_DIR}/examples/value.janus"
    )
    add_test(
        NAME examples.snake.check
        COMMAND janus check
                "${PROJECT_SOURCE_DIR}/examples/snake/main.janus"
    )
    add_test(
        NAME examples.warning_free
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUS=$<TARGET_FILE:janus>"
            "-DEXAMPLES_DIR=${PROJECT_SOURCE_DIR}/examples"
            -P "${PROJECT_SOURCE_DIR}/tests/driver/check_examples_warning_free.cmake"
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
    add_executable(module_loader_test
        tests/frontend/module_loader_test.cpp
    )
    target_link_libraries(module_loader_test PRIVATE janus_frontend)
    target_compile_options(module_loader_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME frontend.module_loader COMMAND module_loader_test)
    add_executable(compile_time_constant_test
        tests/language/compile_time_constant_test.cpp
    )
    target_link_libraries(compile_time_constant_test
        PRIVATE
            janus_semantic
            janus_llvm_backend
    )
    target_include_directories(compile_time_constant_test SYSTEM PRIVATE
        ${JANUS_LLVM_INCLUDE_DIR}
    )
    target_compile_options(compile_time_constant_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME language.compile_time_constants COMMAND compile_time_constant_test)
    add_test(
        NAME cli.compile_time_constant_probes
        COMMAND "${CMAKE_COMMAND}"
                "-DJANUSC=$<TARGET_FILE:janusc>"
                "-DWORK_DIR=${PROJECT_BINARY_DIR}/compile-time-constant-probes"
                -P "${PROJECT_SOURCE_DIR}/tests/driver/run_compile_time_constant_probes_cli.cmake"
    )
    add_executable(documentation_comment_test
        tests/frontend/documentation_comment_test.cpp
    )
    target_link_libraries(documentation_comment_test PRIVATE janus_frontend)
    target_compile_options(documentation_comment_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(
        NAME parser.documentation_comments
        COMMAND documentation_comment_test
    )
    add_executable(derivation_syntax_test
        tests/frontend/derivation_syntax_test.cpp
    )
    target_link_libraries(derivation_syntax_test PRIVATE janus_frontend)
    target_compile_options(derivation_syntax_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME parser.derivation_syntax COMMAND derivation_syntax_test)
    add_executable(integer_literal_test
        tests/frontend/integer_literal_test.cpp
    )
    target_link_libraries(integer_literal_test PRIVATE janus_frontend)
    target_compile_options(integer_literal_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME parser.integer_literals COMMAND integer_literal_test)
    add_executable(diagnostic_model_test
        tests/diagnostics/diagnostic_model_test.cpp
    )
    target_link_libraries(diagnostic_model_test PRIVATE janus_frontend)
    target_compile_options(diagnostic_model_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME diagnostics.model COMMAND diagnostic_model_test)
    add_test(
        NAME diagnostics.invalid_corpus
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/diagnostics/run_invalid_corpus.py"
                --janus "$<TARGET_FILE:janus>"
                --corpus
                "${PROJECT_SOURCE_DIR}/tests/diagnostics/invalid"
    )
    add_test(
        NAME diagnostics.rendering
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/diagnostics/run_rendering_snapshots.py"
                --janus "$<TARGET_FILE:janus>"
                --fixtures
                "${PROJECT_SOURCE_DIR}/tests/diagnostics/rendering"
                --schema
                "${PROJECT_SOURCE_DIR}/docs/schemas/diagnostic-0.5.2.schema.json"
    )
    add_test(
        NAME diagnostics.project
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/diagnostics/run_project_diagnostics.py"
                --janus "$<TARGET_FILE:janus>"
                --fixture
                "${PROJECT_SOURCE_DIR}/tests/fixtures/project-diagnostics"
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
    foreach(ownership_kind class struct enum)
        add_test(
            NAME design.container_owned_${ownership_kind}
            COMMAND janus check
                    "${PROJECT_SOURCE_DIR}/tests/fixtures/design/container_owned_${ownership_kind}.janus"
        )
    endforeach()
    add_executable(lsp_server_test
        tests/lsp/server_test.cpp
    )
    target_link_libraries(lsp_server_test PRIVATE janus_lsp)
    target_include_directories(lsp_server_test SYSTEM PRIVATE
        ${JANUS_LLVM_INCLUDE_DIR}
    )
    target_compile_definitions(lsp_server_test
        PRIVATE
            JANUS_STDLIB_DIR="${PROJECT_SOURCE_DIR}/stdlib"
            JANUS_STDLIB_API_INDEX="${PROJECT_SOURCE_DIR}/website/docs/reference/stdlib/api-index.json"
    )
    add_test(NAME lsp.server COMMAND lsp_server_test)
    add_executable(lsp_workspace_index_test
        tests/lsp/workspace_index_test.cpp
    )
    target_link_libraries(lsp_workspace_index_test PRIVATE janus_lsp)
    target_compile_definitions(lsp_workspace_index_test
        PRIVATE
            JANUS_STDLIB_DIR="${PROJECT_SOURCE_DIR}/stdlib"
            JANUS_LSP_WORKSPACE_FIXTURE="${PROJECT_SOURCE_DIR}/tests/fixtures/lsp-workspace"
    )
    target_compile_options(lsp_workspace_index_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME lsp.workspace_index COMMAND lsp_workspace_index_test)
    add_test(
        NAME diagnostics.model.require_failure
        COMMAND diagnostic_model_test --verify-require-failure
    )
    set_tests_properties(
        diagnostics.model.require_failure
        PROPERTIES WILL_FAIL TRUE
    )
    add_test(
        NAME lsp.server.require_failure
        COMMAND lsp_server_test --verify-require-failure
    )
    set_tests_properties(
        lsp.server.require_failure
        PROPERTIES WILL_FAIL TRUE
    )
    add_test(
        NAME lsp.workspace_index.require_failure
        COMMAND lsp_workspace_index_test --verify-require-failure
    )
    set_tests_properties(
        lsp.workspace_index.require_failure
        PROPERTIES WILL_FAIL TRUE
    )
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
        NAME cli.compilation_timings
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/driver/run_compilation_timings_cli.py"
                --janus "$<TARGET_FILE:janus>"
                --source "${PROJECT_SOURCE_DIR}/examples/value.janus"
                --work-dir "${PROJECT_BINARY_DIR}/compilation-timings"
    )
    add_test(
        NAME performance.compilation_trend
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/performance/test_compilation_trend.py"
    )
    add_test(
        NAME performance.compilation_benchmark_schema
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/performance/test_benchmark_compilation.py"
    )
    add_test(
        NAME performance.compilation_benchmarks
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/benchmark-compilation.py"
                --janus "$<TARGET_FILE:janus>"
                --benchmarks "${PROJECT_SOURCE_DIR}/benchmarks/compilation"
                --samples 1
                --output "${PROJECT_BINARY_DIR}/compiler-performance-smoke.json"
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
                -DCLANG_EXECUTABLE=${JANUS_CLANG_EXECUTABLE}
                -DLLD_EXECUTABLE=${JANUS_LLD_EXECUTABLE}
                -DLLVM_DIR=${LLVM_DIR}
                -DPACKAGE_PLATFORM=${CMAKE_SYSTEM_NAME}
                -DPACKAGE_ARCH=${CMAKE_SYSTEM_PROCESSOR}
                -DJANUS=$<TARGET_FILE:janus>
                -DJANUSUP=$<TARGET_FILE:janusup>
                -P "${PROJECT_SOURCE_DIR}/tests/driver/run_janusup.cmake"
    )
    add_test(
        NAME toolchain.archive_validation
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/driver/toolchain_archive_validation_test.py"
                --source "${PROJECT_SOURCE_DIR}"
                --janusup "$<TARGET_FILE:janusup>"
    )
    # PowerShell process startup dominates the shared malicious-archive matrix
    # on Windows runners; keep the repository-wide default for other tests.
    set_tests_properties(toolchain.archive_validation PROPERTIES TIMEOUT 180)
    add_test(
        NAME toolchain.concurrent_install
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/driver/janusup_concurrent_install_test.py"
                --janusup "$<TARGET_FILE:janusup-test>"
                --production-janusup "$<TARGET_FILE:janusup>"
    )
    set_tests_properties(toolchain.concurrent_install PROPERTIES TIMEOUT 90)
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
    find_program(POWERSHELL_EXECUTABLE NAMES pwsh powershell)
    if(POWERSHELL_EXECUTABLE)
        add_test(
            NAME toolchain.install_script_powershell
            COMMAND "${POWERSHELL_EXECUTABLE}" -NoProfile -File
                    "${PROJECT_SOURCE_DIR}/tests/driver/install_script_test.ps1"
                    "${PROJECT_SOURCE_DIR}"
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
    add_executable(documentation_test
        tests/driver/documentation_test.cpp
    )
    target_link_libraries(documentation_test
        PRIVATE
            janus_driver
            janus_frontend
    )
    target_compile_options(documentation_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME driver.documentation COMMAND documentation_test)

    add_executable(api_index_test tests/driver/api_index_test.cpp)
    target_link_libraries(api_index_test PRIVATE janus_driver)
    target_compile_definitions(api_index_test PRIVATE
        JANUS_INSTALLED_API_INDEX=\"${PROJECT_SOURCE_DIR}/website/docs/reference/stdlib/api-index.json\"
    )
    target_compile_options(api_index_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME driver.api_index COMMAND api_index_test)
    add_executable(doctest_test
        tests/driver/doctest_test.cpp
    )
    target_link_libraries(doctest_test PRIVATE janus_driver)
    target_compile_options(doctest_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME driver.doctest COMMAND doctest_test)
    add_executable(native_test_test
        tests/driver/native_test_test.cpp
    )
    target_link_libraries(native_test_test PRIVATE janus_driver)
    target_compile_options(native_test_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME driver.native_test COMMAND native_test_test)
    find_package(Threads REQUIRED)
    add_executable(temporary_directory_test
        tests/driver/temporary_directory_test.cpp
    )
    target_link_libraries(temporary_directory_test
        PRIVATE
            janus_driver
            Threads::Threads
    )
    target_compile_options(temporary_directory_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME driver.temporary_directory COMMAND temporary_directory_test)
    add_executable(incremental_cache_test
        tests/driver/incremental_cache_test.cpp
    )
    target_link_libraries(incremental_cache_test
        PRIVATE
            janus_driver
            Threads::Threads
    )
    target_compile_options(incremental_cache_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME driver.incremental_cache COMMAND incremental_cache_test)
    add_test(
        NAME cli.incremental_cache
        COMMAND "${CMAKE_COMMAND}"
                -DBUILD_DIR=${PROJECT_BINARY_DIR}
                -DJANUS=$<TARGET_FILE:janus>
                -P "${PROJECT_SOURCE_DIR}/tests/driver/run_incremental_cache_cli.cmake"
    )
    add_test(
        NAME cli.incremental_cache_concurrent_no_cache
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/driver/run_incremental_cache_concurrent.py"
                --janus "$<TARGET_FILE:janus>"
                --work-dir "${PROJECT_BINARY_DIR}/incremental-cache-concurrent-no-cache"
                --no-cache
    )
    add_test(
        NAME cli.incremental_cache_concurrent
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/driver/run_incremental_cache_concurrent.py"
                --janus "$<TARGET_FILE:janus>"
                --work-dir "${PROJECT_BINARY_DIR}/incremental-cache-concurrent"
    )
    foreach(INDEX RANGE 1 8)
        add_test(
            NAME cli.concurrent_build_${INDEX}
            COMMAND "${CMAKE_COMMAND}"
                    -DBUILD_DIR=${PROJECT_BINARY_DIR}
                    -DJANUS=$<TARGET_FILE:janus>
                    -DID=${INDEX}
                    -P "${PROJECT_SOURCE_DIR}/tests/driver/run_concurrent_build.cmake"
        )
    endforeach()
    add_test(
        NAME cli.temporary_cleanup
        COMMAND "${CMAKE_COMMAND}"
                -DBUILD_DIR=${PROJECT_BINARY_DIR}
                -DJANUS=$<TARGET_FILE:janus>
                -P "${PROJECT_SOURCE_DIR}/tests/driver/run_temporary_cleanup.cmake"
    )
    add_test(
        NAME cli.project_creation
        COMMAND "${CMAKE_COMMAND}"
                -DBUILD_DIR=${PROJECT_BINARY_DIR}
                -DJANUS=$<TARGET_FILE:janus>
                -P "${PROJECT_SOURCE_DIR}/tests/driver/run_project_creation.cmake"
    )
    add_test(
        NAME cli.remote_registry
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/driver/run_remote_registry.py"
                --janus $<TARGET_FILE:janus>
                --work-dir "${PROJECT_BINARY_DIR}/remote-registry-test"
    )
    set_tests_properties(cli.remote_registry PROPERTIES TIMEOUT 120)
    add_test(
        NAME registry.reference
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/registry/run_reference_registry.py"
                --janus $<TARGET_FILE:janus>
                --source-root "${PROJECT_SOURCE_DIR}"
                --work-dir "${PROJECT_BINARY_DIR}/reference-registry-test"
    )
    set_tests_properties(registry.reference PROPERTIES TIMEOUT 180)
    add_test(
        NAME cli.documentation
        COMMAND "${CMAKE_COMMAND}"
                -DBUILD_DIR=${PROJECT_BINARY_DIR}
                -DJANUS=$<TARGET_FILE:janus>
                -P "${PROJECT_SOURCE_DIR}/tests/driver/run_documentation_cli.cmake"
    )
    add_test(
        NAME docs.stdlib_reference
        COMMAND "${CMAKE_COMMAND}"
                -DBUILD_DIR=${PROJECT_BINARY_DIR}
                -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
                -DJANUS=$<TARGET_FILE:janus>
                -P "${PROJECT_SOURCE_DIR}/tests/documentation/run_stdlib_documentation_cli.cmake"
    )
    add_test(
        NAME cli.doctest
        COMMAND "${CMAKE_COMMAND}"
                -DBUILD_DIR=${PROJECT_BINARY_DIR}
                -DJANUS=$<TARGET_FILE:janus>
                -P "${PROJECT_SOURCE_DIR}/tests/driver/run_doctest_cli.cmake"
    )
    add_test(
        NAME cli.native_test
        COMMAND "${CMAKE_COMMAND}"
                -DBUILD_DIR=${PROJECT_BINARY_DIR}
                -DJANUS=$<TARGET_FILE:janus>
                -P "${PROJECT_SOURCE_DIR}/tests/driver/run_native_test_cli.cmake"
    )
    add_test(
        NAME cli.execution_contract
        COMMAND "${CMAKE_COMMAND}"
                -DBUILD_DIR=${PROJECT_BINARY_DIR}
                -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
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
        NAME docs.version_consistency
        COMMAND
            ${CMAKE_COMMAND}
            "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
            -P "${PROJECT_SOURCE_DIR}/cmake/verify_documentation.cmake"
    )
    add_test(
        NAME docs.public_surface
        COMMAND
            "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/scripts/check_public_surface.py"
            --root "${PROJECT_SOURCE_DIR}"
            --janus $<TARGET_FILE:janus>
    )
    add_test(
        NAME docs.stability_inventory_0_8
        COMMAND
            "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/scripts/check_stability_inventory.py"
            --root "${PROJECT_SOURCE_DIR}"
    )
    foreach(FUZZ_MODE lexer parser manifest resolver)
        add_test(
            NAME fuzz.smoke.${FUZZ_MODE}
            COMMAND
                "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/fuzz_frontend.py"
                --janus $<TARGET_FILE:janus>
                --mode "${FUZZ_MODE}"
                --duration 1
        )
        set_tests_properties(fuzz.smoke.${FUZZ_MODE} PROPERTIES TIMEOUT 10)
    endforeach()
    add_test(
        NAME docs.public_surface_stale_fixture
        COMMAND
            "${CMAKE_COMMAND}"
            "-DPYTHON=${Python3_EXECUTABLE}"
            "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
            -P
            "${PROJECT_SOURCE_DIR}/tests/documentation/run_stale_public_surface.cmake"
    )
    add_test(
        NAME docs.stdlib_audit
        COMMAND
            "${Python3_EXECUTABLE}"
            -m unittest
            discover
            -s "${PROJECT_SOURCE_DIR}/tests/documentation"
            -p "test_stdlib_audit.py"
            -v
    )
    add_test(
        NAME docs.registry_protocol_v1
        COMMAND
            "${Python3_EXECUTABLE}"
            -m unittest
            discover
            -s "${PROJECT_SOURCE_DIR}/tests/documentation"
            -p "test_registry_protocol.py"
            -v
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
        NAME compatibility.current
        COMMAND
            ${CMAKE_COMMAND}
            "-DPREVIOUS_JANUS=$<TARGET_FILE:janus>"
            "-DCURRENT_JANUS=$<TARGET_FILE:janus>"
            "-DFIXTURE_DIR=${PROJECT_SOURCE_DIR}/tests/compatibility"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/compatibility-current"
            "-DEXECUTABLE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}"
            -P
            "${PROJECT_SOURCE_DIR}/tests/compatibility/run_compatibility.cmake"
    )
    add_test(
        NAME language.text_api_runtime
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/text_api.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/text_api_runtime"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/text_api.expected.txt"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_example.cmake"
    )
    add_test(
        NAME language.debug_struct_bool_runtime
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/debug_struct_bool.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/debug_struct_bool_runtime"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/debug_struct_bool.expected.txt"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_example.cmake"
    )
    add_test(
        NAME compatibility.system_result
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/system_result.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/system_result_runtime"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/system_result.expected.txt"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_example.cmake"
    )
    add_test(
        NAME compatibility.path_fs_result
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/path_fs_result.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/path_fs_result_runtime"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/path_fs_result.expected.txt"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_example.cmake"
    )
    add_test(
        NAME compatibility.io_streams
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/io_streams.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/io_streams_runtime"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/io_streams.expected.txt"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_example.cmake"
    )
    add_test(
        NAME compatibility.processes
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/processes.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/processes_runtime"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/processes.expected.txt"
            "-DPROGRAM_ENVIRONMENT=JANUS_PROCESS_TEST=valeur-béta"
            -DDISABLE_LEAK_SANITIZER=ON
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_example.cmake"
    )
    add_test(
        NAME runtime.stdout_normalization
        COMMAND
            ${CMAKE_COMMAND}
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/verify_stdout_normalization.cmake"
    )

    add_executable(backend_link_test
        tests/language/backend_link_test.cpp
    )
    target_link_libraries(backend_link_test
        PRIVATE
            janus_llvm_backend
    )
    target_compile_options(backend_link_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME backend.link_contract COMMAND backend_link_test)

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
            JANUS_GLOBALS_UNIMPORTED_MUTABLE_READ="${PROJECT_SOURCE_DIR}/tests/fixtures/modules/globals_unimported_mutable_read.janus"
            JANUS_GLOBALS_UNIMPORTED_MUTABLE_WRITE="${PROJECT_SOURCE_DIR}/tests/fixtures/modules/globals_unimported_mutable_write.janus"
            JANUS_GLOBALS_UNIMPORTED_IMMUTABLE_READ="${PROJECT_SOURCE_DIR}/tests/fixtures/modules/globals_unimported_immutable_read.janus"
            JANUS_GLOBALS_UNIMPORTED_IMMUTABLE_INITIALIZER="${PROJECT_SOURCE_DIR}/tests/fixtures/modules/globals_unimported_immutable_initializer.janus"
            JANUS_GLOBALS_EXPLICIT_IMPORT="${PROJECT_SOURCE_DIR}/tests/fixtures/modules/globals_explicit_import.janus"
            JANUS_GLOBALS_ALIASED_IMPORT="${PROJECT_SOURCE_DIR}/tests/fixtures/modules/globals_aliased_import.janus"
            JANUS_GLOBALS_SELECTIVE_IMPORT_BYPASS="${PROJECT_SOURCE_DIR}/tests/fixtures/modules/globals_selective_import_bypass.janus"
            JANUS_GLOBALS_ALIAS_CANONICAL_BYPASS="${PROJECT_SOURCE_DIR}/tests/fixtures/modules/globals_alias_canonical_bypass.janus"
            JANUS_CONSTANTS_SIMPLE_IMPORT="${PROJECT_SOURCE_DIR}/tests/fixtures/modules/constants_simple_import.janus"
            JANUS_CONSTANTS_SELECTIVE_IMPORT="${PROJECT_SOURCE_DIR}/tests/fixtures/modules/constants_selective_import.janus"
            JANUS_CONSTANTS_RENAMED_IMPORT="${PROJECT_SOURCE_DIR}/tests/fixtures/modules/constants_renamed_import.janus"
            JANUS_CONSTANTS_MODULE_ALIAS="${PROJECT_SOURCE_DIR}/tests/fixtures/modules/constants_module_alias.janus"
            JANUS_CONSTANTS_PRIVATE_IMPORT="${PROJECT_SOURCE_DIR}/tests/fixtures/modules/constants_private_import.janus"
            JANUS_CONSTANTS_INTERNAL_IMPORT="${PROJECT_SOURCE_DIR}/tests/fixtures/modules/constants_internal_import.janus"
            JANUS_CONSTANTS_LOADED_NOT_IMPORTED="${PROJECT_SOURCE_DIR}/tests/fixtures/modules/constants_loaded_not_imported.janus"
    )
    target_compile_options(global_variable_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME language.global_variable COMMAND global_variable_test)
    add_test(
        NAME runtime.const_def_parity
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/const_def_parity.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/const_def_parity_runtime"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/const_def_parity.expected.txt"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_example.cmake"
    )
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
    add_test(
        NAME runtime.panic_context_cleanup
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/panic_context_cleanup.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/panic_context_cleanup_runtime"
            "-DEXPECTED_ERROR=explicit failure"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/panic_context_cleanup.expected.txt"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_trap.cmake"
    )
    add_test(
        NAME runtime.array_out_of_bounds
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/array_out_of_bounds.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/array_out_of_bounds_runtime"
            "-DEXPECTED_ERROR=Array index out of bounds"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_trap.cmake"
    )
    add_test(
        NAME runtime.owned_array
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/owned_array.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/owned_array_runtime"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/owned_array.expected.txt"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_example.cmake"
    )
    add_test(
        NAME runtime.array_factories
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/array_factories.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/array_factories_runtime"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/array_factories.expected.txt"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_example.cmake"
    )
    add_test(
        NAME runtime.array_factory_panic_cleanup
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/array_factory_panic_cleanup.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/array_factory_panic_cleanup_runtime"
            "-DEXPECTED_ERROR=array factory failure"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/array_factory_panic_cleanup.expected.txt"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_trap.cmake"
    )
    add_test(
        NAME runtime.nested_callback_panic_cleanup
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/nested_callback_panic_cleanup.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/nested_callback_panic_cleanup_runtime"
            "-DEXPECTED_ERROR=nested callback failure"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/nested_callback_panic_cleanup.expected.txt"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_trap.cmake"
    )
    add_test(
        NAME runtime.callback_panic_cleanup_pop
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/callback_panic_cleanup_pop.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/callback_panic_cleanup_pop_runtime"
            "-DEXPECTED_ERROR=after callback return"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/callback_panic_cleanup_pop.expected.txt"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_trap.cmake"
    )
    add_test(
        NAME runtime.callback_argument_panic_cleanup
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/callback_argument_panic_cleanup.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/callback_argument_panic_cleanup_runtime"
            "-DEXPECTED_ERROR=callback argument failure"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/callback_argument_panic_cleanup.expected.txt"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_trap.cmake"
    )
    add_test(
        NAME runtime.array_factory_capacity_overflow
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/array_factory_capacity_overflow.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/array_factory_capacity_overflow_runtime"
            "-DEXPECTED_ERROR=Array capacity overflow"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_trap.cmake"
    )
    add_test(
        NAME runtime.array_literals
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/array_literals.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/array_literals_runtime"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/array_literals.expected.txt"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_example.cmake"
    )
    add_test(
        NAME runtime.array_literal_panic_cleanup
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/array_literal_panic_cleanup.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/array_literal_panic_cleanup_runtime"
            "-DEXPECTED_ERROR=integer division by zero"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/array_literal_panic_cleanup.expected.txt"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_trap.cmake"
    )
    add_test(
        NAME runtime.array_sort
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/array_sort.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/array_sort_runtime"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/array_sort.expected.txt"
            "-DENABLE_UNDEFINED_SANITIZER=ON"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_example.cmake"
    )
    add_test(
        NAME runtime.owned_array_panic
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/owned_array_panic.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/owned_array_panic_runtime"
            "-DEXPECTED_ERROR=Array index out of bounds"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/owned_array_panic.expected.txt"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_trap.cmake"
    )
    add_test(
        NAME language.owned_array_rejects_copy
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/owned_array_copy_error.janus"
            "-DEXPECTED_ERROR=Array.get requires a Copy element type"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_compile_error.cmake"
    )
    add_test(
        NAME language.filled_array_rejects_non_copy
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/filled_array_non_copy_error.janus"
            "-DEXPECTED_ERROR=does not satisfy constraint 'Copy'"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_compile_error.cmake"
    )
    add_test(
        NAME language.owned_array_rejects_sort
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/owned_array_sort_error.janus"
            "-DEXPECTED_ERROR=Array.sortWith requires a Copy element type"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_compile_error.cmake"
    )
    add_test(
        NAME language.owned_array_requires_move
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/owned_array_move_error.janus"
            "-DEXPECTED_ERROR=owned Array element requires an explicit move"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_compile_error.cmake"
    )
    add_test(
        NAME runtime.owned_hash_collections
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/owned_hash_collections.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/owned_hash_collections_runtime"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/owned_hash_collections.expected.txt"
            "-DENABLE_UNDEFINED_SANITIZER=ON"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_example.cmake"
    )
    add_test(
        NAME runtime.hash_table_invariants
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/hash_table_invariants.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/hash_table_invariants_runtime"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/hash_table_invariants.expected.txt"
            "-DENABLE_UNDEFINED_SANITIZER=ON"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_example.cmake"
    )
    add_test(
        NAME runtime.owned_hash_panic
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/owned_hash_panic.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/owned_hash_panic_runtime"
            "-DEXPECTED_ERROR=owned hash failure"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/owned_hash_panic.expected.txt"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_trap.cmake"
    )
    add_test(
        NAME language.owned_hash_requires_move
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/owned_hash_move_error.janus"
            "-DEXPECTED_ERROR=owned collection element requires an explicit move"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_compile_error.cmake"
    )
    add_test(
        NAME language.owned_hash_rejects_copy
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/owned_hash_copy_error.janus"
            "-DEXPECTED_ERROR=HashMap.getOption requires Copy"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_compile_error.cmake"
    )
    add_test(
        NAME language.hashing_borrow_rejects_delete
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/hashing_borrow_error.janus"
            "-DEXPECTED_ERROR=borrowed value 'value' cannot be deleted"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_compile_error.cmake"
    )
    add_test(
        NAME runtime.owned_iterators
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/owned_iterators.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/owned_iterators_runtime"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/owned_iterators.expected.txt"
            "-DENABLE_UNDEFINED_SANITIZER=ON"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_example.cmake"
    )
    add_test(
        NAME runtime.sequence_pipeline_stress
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/sequence_pipeline_stress.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/sequence_pipeline_stress_runtime"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/sequence_pipeline_stress.expected.txt"
            "-DENABLE_UNDEFINED_SANITIZER=ON"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_example.cmake"
    )
    add_test(
        NAME runtime.functional_sequence_cleanup
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/functional_sequence_cleanup.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/functional_sequence_cleanup_runtime"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/functional_sequence_cleanup.expected.txt"
            "-DENABLE_UNDEFINED_SANITIZER=ON"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_example.cmake"
    )
    add_test(
        NAME language.owned_iterator_borrow_rejects_escape
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/owned_iterator_borrow_error.janus"
            "-DEXPECTED_ERROR=borrowed value 'item' cannot be passed to an owning parameter"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_compile_error.cmake"
    )
    add_test(
        NAME language.owned_iterator_rejects_implicit_copy
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/owned_iterator_for_copy_error.janus"
            "-DEXPECTED_ERROR=for cannot copy an owned Iterable element"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_compile_error.cmake"
    )
    add_test(
        NAME runtime.owned_iterator_panic
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/owned_iterator_panic.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/owned_iterator_panic_runtime"
            "-DEXPECTED_ERROR=owned iterator failure"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/owned_iterator_panic.expected.txt"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_trap.cmake"
    )
    add_test(
        NAME runtime.option_combinators
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/option_combinators.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/option_combinators_runtime"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/option_combinators.expected.txt"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_example.cmake"
    )
    add_test(
        NAME runtime.owned_option_combinators
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/owned_option_combinators.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/owned_option_combinators_runtime"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/owned_option_combinators.expected.txt"
            "-DENABLE_UNDEFINED_SANITIZER=ON"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_example.cmake"
    )
    add_test(
        NAME language.owned_option_requires_move
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/owned_option_move_error.janus"
            "-DEXPECTED_ERROR=transferring owning aggregate 'Option.*Resource.*' requires an explicit move"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_compile_error.cmake"
    )
    add_test(
        NAME language.owned_option_observation_requires_local
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/owned_option_observe_temporary_error.janus"
            "-DEXPECTED_ERROR=observing an owning Option requires a local value"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_compile_error.cmake"
    )
    add_test(
        NAME runtime.result_combinators
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/result_combinators.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/result_combinators_runtime"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/result_combinators.expected.txt"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_example.cmake"
    )
    add_test(
        NAME runtime.owned_result_combinators
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/owned_result_combinators.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/owned_result_combinators_runtime"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/owned_result_combinators.expected.txt"
            "-DENABLE_UNDEFINED_SANITIZER=ON"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_example.cmake"
    )
    add_test(
        NAME language.owned_result_requires_move
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/owned_result_move_error.janus"
            "-DEXPECTED_ERROR=transferring owning aggregate 'Result.*Resource.*Resource.*' requires an explicit move"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_compile_error.cmake"
    )
    add_test(
        NAME language.owned_result_observation_requires_local
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/owned_result_observe_temporary_error.janus"
            "-DEXPECTED_ERROR=observing an owning Result requires a local value"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_compile_error.cmake"
    )
    add_test(
        NAME language.owned_result_try_requires_move
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/owned_result_try_move_error.janus"
            "-DEXPECTED_ERROR=propagating owning aggregate 'Result.*Resource.*Resource.*' requires an explicit move"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_compile_error.cmake"
    )
    add_test(
        NAME runtime.owned_result_try
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/owned_result_try.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/owned_result_try_runtime"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/owned_result_try.expected.txt"
            "-DENABLE_UNDEFINED_SANITIZER=ON"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_example.cmake"
    )
    add_test(
        NAME language.owned_result_try_rejects_reuse
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/owned_result_try_reuse_error.janus"
            "-DEXPECTED_ERROR=variable 'value' is used before initialization"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_compile_error.cmake"
    )
    add_test(
        NAME runtime.global_aggregate_cleanup
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/global_aggregate_cleanup.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/global_aggregate_cleanup_runtime"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/global_aggregate_cleanup.expected.txt"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_example.cmake"
    )
    add_test(
        NAME runtime.aggregate_control_flow_cleanup
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/aggregate_control_flow_cleanup.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/aggregate_control_flow_cleanup_runtime"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/aggregate_control_flow_cleanup.expected.txt"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_example.cmake"
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
        NAME runtime.time_random
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/time_random.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/time_random_runtime"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/time_random.expected.txt"
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
    add_test(
        NAME benchmarks.sequence_pipeline_smoke
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/benchmarks/sequence_pipeline.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/sequence_pipeline_benchmark_smoke"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/benchmarks/sequence_pipeline.expected.txt"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_example.cmake"
    )
    add_test(
        NAME benchmarks.array_sort_smoke
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/benchmarks/array_sort.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/array_sort_benchmark_smoke"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/benchmarks/array_sort.expected.txt"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_example.cmake"
    )
    add_test(
        NAME benchmarks.hash_collections_smoke
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/benchmarks/hash_collections.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/hash_collections_benchmark_smoke"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/benchmarks/hash_collections.expected.txt"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_example.cmake"
    )
    add_test(
        NAME benchmarks.stdlib_services_smoke
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/benchmarks/stdlib_services.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/stdlib_services_benchmark_smoke"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/benchmarks/stdlib_services.expected.txt"
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
    foreach(trap_case random_zero_bound duration_overflow)
        if(trap_case STREQUAL "random_zero_bound")
            set(trap_error "random upper bound must be positive")
        else()
            set(trap_error "duration overflow")
        endif()
        add_test(
            NAME runtime.${trap_case}
            COMMAND
                ${CMAKE_COMMAND}
                "-DJANUSC=$<TARGET_FILE:janusc>"
                "-DCLANG=${CLANG_EXECUTABLE}"
                "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/${trap_case}.janus"
                "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
                "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/${trap_case}"
                "-DEXPECTED_ERROR=${trap_error}"
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

    add_executable(ownership_leak_warning_test
        tests/language/ownership_leak_warning_test.cpp
    )
    target_link_libraries(ownership_leak_warning_test
        PRIVATE
            janus_semantic
    )
    target_compile_options(ownership_leak_warning_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME language.ownership_leak_warning
        COMMAND ownership_leak_warning_test)

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
    add_test(
        NAME runtime.match_literals_guards
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/match_literals_guards.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/match_literals_guards_runtime"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/match_literals_guards.expected.txt"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_example.cmake"
    )

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

    add_executable(numeric_conversion_test
        tests/language/numeric_conversion_test.cpp
    )
    target_link_libraries(numeric_conversion_test
        PRIVATE
            janus_semantic
            janus_llvm_backend
    )
    target_compile_options(numeric_conversion_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME language.numeric_conversion COMMAND numeric_conversion_test)

    add_test(
        NAME runtime.numeric_conversions
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/numeric_conversions.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/numeric_conversions_runtime"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/numeric_conversions.expected.txt"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_example.cmake"
    )
    add_test(
        NAME runtime.numeric_conversions_reordered_enums
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/numeric_conversions_reordered_enums.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/numeric_conversions_reordered_enums_runtime"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/numeric_conversions_reordered_enums.expected.txt"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_example.cmake"
    )
    add_test(
        NAME runtime.numeric_conversions_aliased_imports
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/numeric_conversions_aliased_imports.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/numeric_conversions_aliased_imports_runtime"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/numeric_conversions_aliased_imports.expected.txt"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_example.cmake"
    )
    add_test(
        NAME runtime.numeric_conversion_matrix
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/numeric_conversion_matrix.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/numeric_conversion_matrix_runtime"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/numeric_conversion_matrix.expected.txt"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_example.cmake"
    )

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
            JANUS_VISIBILITY_ENTRY="${PROJECT_SOURCE_DIR}/tests/fixtures/modules/visibility_entry.janus"
            JANUS_VISIBILITY_PRIVATE_FUNCTION_ENTRY="${PROJECT_SOURCE_DIR}/tests/fixtures/modules/visibility_private_function_entry.janus"
            JANUS_VISIBILITY_PRIVATE_TYPE_ENTRY="${PROJECT_SOURCE_DIR}/tests/fixtures/modules/visibility_private_type_entry.janus"
            JANUS_VISIBILITY_INTERNAL_METHOD_ENTRY="${PROJECT_SOURCE_DIR}/tests/fixtures/modules/visibility_internal_method_entry.janus"
            JANUS_VISIBILITY_INTERNAL_FIELD_ENTRY="${PROJECT_SOURCE_DIR}/tests/fixtures/modules/visibility_internal_field_entry.janus"
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

    add_executable(derivation_semantics_test
        tests/language/derivation_semantics_test.cpp
    )
    target_link_libraries(derivation_semantics_test
        PRIVATE
            janus_semantic
            janus_llvm_backend
    )
    target_compile_options(derivation_semantics_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME language.derivation_semantics COMMAND derivation_semantics_test)

    add_test(
        NAME compatibility.derivations
        COMMAND
            ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/derived_capabilities.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/derived_capabilities_runtime"
            "-DEXPECTED_OUTPUT=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/derived_capabilities.expected.txt"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_example.cmake"
    )
    add_test(
        NAME language.bitwise_shifts_runtime
        COMMAND ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/bitwise_shifts.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/bitwise_shifts_runtime"
            "-DENABLE_UNDEFINED_SANITIZER=ON"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_example.cmake"
    )
    add_test(
        NAME language.shift_count_out_of_range_runtime
        COMMAND ${CMAKE_COMMAND}
            "-DJANUSC=$<TARGET_FILE:janusc>"
            "-DCLANG=${CLANG_EXECUTABLE}"
            "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/fixtures/runtime/shift_count_out_of_range.janus"
            "-DRUNTIME=$<TARGET_FILE:janus_runtime>"
            "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/shift_count_out_of_range_runtime"
            "-DEXPECTED_ERROR=shift count exceeds operand width"
            -P "${PROJECT_SOURCE_DIR}/tests/runtime/run_janus_trap.cmake"
    )
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
    RUNTIME_DEPENDENCY_SET janus_application_dependencies
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
install(
    RUNTIME_DEPENDENCY_SET janus_application_dependencies
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
    DIRECTORY website/docs/reference/stdlib/
    DESTINATION ${CMAKE_INSTALL_DOCDIR}/stdlib-reference
)
install(
    FILES README.md CHANGELOG.md LICENSE NOTICE
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
)
install(FILES "${PROJECT_BINARY_DIR}/build-identity.json"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/janus)
if(JANUS_SOURCE_SHA)
    file(WRITE "${PROJECT_BINARY_DIR}/janus-source-sha" "${JANUS_SOURCE_SHA}\n")
    install(FILES "${PROJECT_BINARY_DIR}/janus-source-sha"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/janus)
endif()
if(UNIX)
    # Toolchain archives are consumed by strict extractors which reject every
    # link entry. Materialize dependency SONAME links in the staging tree so
    # the official package contains only directories and regular files.
    install(CODE [[
        set(install_root "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}")
        file(GLOB_RECURSE installed_entries
             LIST_DIRECTORIES TRUE "${install_root}/*")
        foreach(installed_entry IN LISTS installed_entries)
            if(IS_SYMLINK "${installed_entry}")
                file(REAL_PATH "${installed_entry}" link_target)
                if(IS_DIRECTORY "${link_target}")
                    message(FATAL_ERROR
                            "cannot materialize directory symlink: ${installed_entry}")
                endif()
                file(REMOVE "${installed_entry}")
                file(COPY_FILE "${link_target}" "${installed_entry}")
            endif()
        endforeach()
    ]])
endif()

set(JANUS_PACKAGE_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
if(JANUS_PACKAGE_ARCH MATCHES "^(aarch64|ARM64)$")
    set(JANUS_PACKAGE_ARCH "arm64")
endif()
set(CPACK_PACKAGE_VENDOR "Janus")
set(CPACK_PACKAGE_CONTACT "Janus contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_FILE_NAME
    "janus-${JANUS_PACKAGE_VERSION}-${CMAKE_SYSTEM_NAME}-${JANUS_PACKAGE_ARCH}")
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
