cmake_minimum_required(VERSION 3.20)

project(
    janus
    VERSION 0.1.0
    DESCRIPTION "A compiler with an LLVM backend"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(LLVM REQUIRED CONFIG)

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
)
target_compile_options(janus_semantic
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_library(janus_llvm_backend
    src/backend/llvm/type_lowering.cpp
    src/backend/llvm/ir_generator.cpp
)
target_include_directories(janus_llvm_backend
    SYSTEM PUBLIC
        ${LLVM_INCLUDE_DIRS}
)
target_link_libraries(janus_llvm_backend
    PUBLIC
        janus_types
        janus_frontend
)
llvm_map_components_to_libnames(JANUS_LLVM_LIBS Core Support)
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

include(CTest)
if(BUILD_TESTING)
    add_executable(int_type_test
        tests/types/int_type_test.cpp
    )
    target_link_libraries(int_type_test
        PRIVATE
            janus_llvm_backend
    )
    target_include_directories(int_type_test
        SYSTEM PRIVATE
            ${LLVM_INCLUDE_DIRS}
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
    )
    target_compile_options(extern_function_test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(NAME language.extern_function COMMAND extern_function_test)

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
endif()
