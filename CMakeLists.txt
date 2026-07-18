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
endif()
