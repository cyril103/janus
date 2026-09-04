if(NOT DEFINED JANUS OR NOT DEFINED BUILD_DIR)
    message(FATAL_ERROR "JANUS and BUILD_DIR are required")
endif()

set(ROOT "${BUILD_DIR}/incremental-cache-cli")
set(PROJECT "${ROOT}/project")
set(CLEAN_OUTPUT "${ROOT}/clean-output")
file(REMOVE_RECURSE "${ROOT}")
file(MAKE_DIRECTORY "${PROJECT}/src/lib")
file(WRITE "${PROJECT}/janus.toml"
     "[package]\nname = \"cached\"\nversion = \"0.1.0\"\nentry = \"src/main.janus\"\n")
file(WRITE "${PROJECT}/src/main.janus"
     "module app.main\nimport lib.middle\nimport lib.generic\nimport lib.box\nimport lib.constant\nimport lib.lifecycle\n\nprivate def entry_seed() : int { return 3 }\nprivate val entry_offset : int = entry_seed()\ndef main() : int { val box : Box = new Box(identity[int](middle()))\nreturn box.get() + entry_offset - 3 + adjustment - 1 + lifecycle_value() }\n")
file(WRITE "${PROJECT}/src/lib/middle.janus"
     "module lib.middle\nimport lib.answer\nimport lib.dynamic\n\ndef middle() : int { return answer() + dynamic_value() - 1 }\n")
file(WRITE "${PROJECT}/src/lib/dynamic.janus"
     "module lib.dynamic\nprivate def seed() : int { return 1 }\nprivate val offset : int = seed()\ndef dynamic_value() : int { return offset }\n")
file(WRITE "${PROJECT}/src/lib/constant.janus"
     "module lib.constant\nval adjustment : int = 1\n")
file(WRITE "${PROJECT}/src/lib/lifecycle.janus"
     "module lib.lifecycle\ndef lifecycle_value() : int { return 0 }\n")
file(WRITE "${PROJECT}/src/lib/box.janus"
     "module lib.box\n\nclass Box(val value : int) { def get() : int { return value } }\n")
file(WRITE "${PROJECT}/src/lib/generic.janus"
     "module lib.generic\n\nprivate def unrelated_generic_helper() : int { return 1 }\ndef identity[T](value : T) : T { return move value }\n")
file(WRITE "${PROJECT}/src/lib/answer.janus"
     "module lib.answer\nprivate val base : int = 1\nprivate def helper() : int { val transform : (int) => int = (value : int) => value + base\nreturn transform(6) }\ndef answer() : int { return helper() }\n")

function(run_build NAME)
    execute_process(
        COMMAND "${JANUS}" build ${ARGN}
        WORKING_DIRECTORY "${PROJECT}"
        RESULT_VARIABLE STATUS
        OUTPUT_VARIABLE OUT
        ERROR_VARIABLE ERR
    )
    if(NOT STATUS EQUAL 0)
        message(FATAL_ERROR "${NAME} failed: ${STATUS}\nstdout=${OUT}\nstderr=${ERR}")
    endif()
endfunction()

function(run_traced_build NAME EXPECTED)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env JANUS_INCREMENTAL_TRACE=1
                "${JANUS}" build ${ARGN}
        WORKING_DIRECTORY "${PROJECT}"
        RESULT_VARIABLE STATUS
        OUTPUT_VARIABLE OUT
        ERROR_VARIABLE ERR
    )
    if(NOT STATUS EQUAL 0 OR NOT ERR MATCHES "incremental: consumer ${EXPECTED}")
        message(FATAL_ERROR "${NAME} did not ${EXPECTED} the consumer: ${STATUS}\nstdout=${OUT}\nstderr=${ERR}")
    endif()
endfunction()

# Cold creates one complete entry; warm and offline reuse it.
run_build("cold")
set(CACHE_ROOT "${PROJECT}/target/.janus-cache/v1")
file(GLOB ENTRIES "${CACHE_ROOT}/entries/*.entry")
list(LENGTH ENTRIES COLD_COUNT)
if(NOT COLD_COUNT EQUAL 1)
    message(FATAL_ERROR "cold build did not create exactly one cache entry")
endif()
run_build("warm")
run_build("offline warm" --offline)
file(GLOB ENTRIES "${CACHE_ROOT}/entries/*.entry")
list(LENGTH ENTRIES WARM_COUNT)
if(NOT WARM_COUNT EQUAL COLD_COUNT)
    message(FATAL_ERROR "warm/offline build created incompatible cache entries")
endif()

# A valid LLVM module missing a required consumer definition fails closed.
find_program(LLVM_DIS_EXECUTABLE NAMES llvm-dis REQUIRED)
find_program(LLVM_AS_EXECUTABLE NAMES llvm-as REQUIRED)
file(GLOB CONSUMER_BITCODE "${CACHE_ROOT}/consumers/*.bc")
file(GLOB CONSUMER_METADATA "${CACHE_ROOT}/consumers/*.entry")
list(GET CONSUMER_BITCODE 0 STRUCTURAL_CONSUMER)
list(GET CONSUMER_METADATA 0 STRUCTURAL_CONSUMER_METADATA)
set(STRUCTURAL_IR "${ROOT}/structurally-incomplete-consumer.ll")
execute_process(COMMAND "${LLVM_DIS_EXECUTABLE}" "${STRUCTURAL_CONSUMER}"
                        -o "${STRUCTURAL_IR}"
                RESULT_VARIABLE LLVM_DIS_STATUS)
file(READ "${STRUCTURAL_IR}" STRUCTURAL_IR_TEXT)
string(REGEX REPLACE
       "define i32 @identity__int\\([^}]*\\}"
       "declare i32 @identity__int(i32)"
       STRUCTURAL_IR_TEXT "${STRUCTURAL_IR_TEXT}")
file(WRITE "${STRUCTURAL_IR}" "${STRUCTURAL_IR_TEXT}")
execute_process(COMMAND "${LLVM_AS_EXECUTABLE}" "${STRUCTURAL_IR}"
                        -o "${STRUCTURAL_CONSUMER}"
                RESULT_VARIABLE LLVM_AS_STATUS)
if(NOT LLVM_DIS_STATUS EQUAL 0 OR NOT LLVM_AS_STATUS EQUAL 0)
    message(FATAL_ERROR "could not forge structurally incomplete consumer")
endif()
file(SHA256 "${STRUCTURAL_CONSUMER}" STRUCTURAL_CONSUMER_DIGEST)
file(READ "${STRUCTURAL_CONSUMER_METADATA}" STRUCTURAL_METADATA_TEXT)
string(REGEX REPLACE "digest=[0-9a-f]+"
       "digest=${STRUCTURAL_CONSUMER_DIGEST}"
       STRUCTURAL_METADATA_TEXT "${STRUCTURAL_METADATA_TEXT}")
file(WRITE "${STRUCTURAL_CONSUMER_METADATA}" "${STRUCTURAL_METADATA_TEXT}")
file(REMOVE_RECURSE "${CACHE_ROOT}/entries" "${CACHE_ROOT}/artifacts")
run_traced_build("structurally incomplete consumer fallback" "compiled")

# A consumer bitcode whose digest metadata was forged still fails closed and is repaired.
file(GLOB CONSUMER_BITCODE "${CACHE_ROOT}/consumers/*.bc")
file(GLOB CONSUMER_METADATA "${CACHE_ROOT}/consumers/*.entry")
list(LENGTH CONSUMER_BITCODE CONSUMER_BITCODE_COUNT)
list(LENGTH CONSUMER_METADATA CONSUMER_METADATA_COUNT)
if(NOT CONSUMER_BITCODE_COUNT EQUAL 1 OR NOT CONSUMER_METADATA_COUNT EQUAL 1)
    message(FATAL_ERROR "expected exactly one consumer cache entry")
endif()
list(GET CONSUMER_BITCODE 0 CORRUPT_CONSUMER)
list(GET CONSUMER_METADATA 0 CORRUPT_CONSUMER_METADATA)
file(WRITE "${CORRUPT_CONSUMER}" "not llvm bitcode")
file(SHA256 "${CORRUPT_CONSUMER}" CORRUPT_CONSUMER_DIGEST)
file(READ "${CORRUPT_CONSUMER_METADATA}" CORRUPT_METADATA_TEXT)
string(REGEX REPLACE "digest=[0-9a-f]+" "digest=${CORRUPT_CONSUMER_DIGEST}"
       CORRUPT_METADATA_TEXT "${CORRUPT_METADATA_TEXT}")
file(WRITE "${CORRUPT_CONSUMER_METADATA}" "${CORRUPT_METADATA_TEXT}")
file(REMOVE_RECURSE "${CACHE_ROOT}/entries" "${CACHE_ROOT}/artifacts")
run_traced_build("corrupt consumer fallback" "compiled")
execute_process(COMMAND "${PROJECT}/target/debug/cached"
                RESULT_VARIABLE CORRUPT_CONSUMER_RESULT)
if(NOT CORRUPT_CONSUMER_RESULT EQUAL 7)
    message(FATAL_ERROR
            "consumer corruption fallback produced ${CORRUPT_CONSUMER_RESULT}")
endif()

# Private dependency changes keep the consumer fingerprint, public changes do not.
file(GLOB CONSUMERS_BEFORE "${CACHE_ROOT}/consumers/*")
file(WRITE "${PROJECT}/src/lib/answer.janus"
     "module lib.answer\nprivate val base : int = 1\nprivate def helper() : int { val transform : (int) => int = (value : int) => value + base\nreturn transform(8) }\ndef answer() : int { return helper() }\n")
file(WRITE "${PROJECT}/src/lib/dynamic.janus"
     "module lib.dynamic\nprivate def seed() : int { return 2 }\nprivate val offset : int = seed()\ndef dynamic_value() : int { return offset }\n")
file(WRITE "${PROJECT}/src/lib/constant.janus"
     "module lib.constant\nval adjustment : int = 2\n")
file(WRITE "${PROJECT}/src/lib/lifecycle.janus"
     "module lib.lifecycle\nprivate def lifecycle_seed() : int { return 4 }\nprivate val lifecycle_offset : int = lifecycle_seed()\ndef lifecycle_value() : int { return lifecycle_offset }\n")
file(WRITE "${PROJECT}/src/lib/generic.janus"
     "module lib.generic\n\nprivate def unrelated_generic_helper() : int { return 2 }\ndef identity[T](value : T) : T { return move value }\n")
run_traced_build("private dependency change" "reused")
execute_process(COMMAND "${PROJECT}/target/debug/cached" RESULT_VARIABLE PRIVATE_RESULT)
if(NOT PRIVATE_RESULT EQUAL 15)
    message(FATAL_ERROR "reused consumer retained stale transitive/global dependency code: ${PRIVATE_RESULT}")
endif()
set(PRIVATE_CLEAN_OUTPUT "${ROOT}/private-clean-output")
run_build("private clean comparison" --no-cache -o "${PRIVATE_CLEAN_OUTPUT}")
execute_process(COMMAND "${PRIVATE_CLEAN_OUTPUT}" RESULT_VARIABLE PRIVATE_CLEAN_RESULT)
if(NOT PRIVATE_CLEAN_RESULT EQUAL PRIVATE_RESULT)
    message(FATAL_ERROR "reused private-change result differs from --no-cache")
endif()
file(WRITE "${PROJECT}/src/lib/lifecycle.janus"
     "module lib.lifecycle\ndef lifecycle_value() : int { return 0 }\n")
run_traced_build("private lifecycle removal" "reused")
execute_process(COMMAND "${PROJECT}/target/debug/cached"
                RESULT_VARIABLE LIFECYCLE_REMOVAL_RESULT)
if(NOT LIFECYCLE_REMOVAL_RESULT EQUAL 11)
    message(FATAL_ERROR
            "removing the last private dynamic global retained stale lifecycle code: ${LIFECYCLE_REMOVAL_RESULT}")
endif()
file(GLOB CONSUMERS_PRIVATE "${CACHE_ROOT}/consumers/*")
list(LENGTH CONSUMERS_BEFORE CONSUMER_COUNT_BEFORE)
list(LENGTH CONSUMERS_PRIVATE CONSUMER_COUNT_PRIVATE)
if(NOT CONSUMER_COUNT_PRIVATE EQUAL CONSUMER_COUNT_BEFORE)
    message(FATAL_ERROR "private change invalidated the consumer fingerprint")
endif()
file(WRITE "${PROJECT}/src/lib/answer.janus"
     "module lib.answer\nprivate val base : int = 1\nprivate def helper() : int { val transform : (int) => int = (value : int) => value + base\nreturn transform(8) }\ndef answer() : int { return helper() }\ndef added() : int { return 8 }\n")
run_traced_build("public dependency change" "compiled")
file(GLOB CONSUMERS_PUBLIC "${CACHE_ROOT}/consumers/*")
list(LENGTH CONSUMERS_PUBLIC CONSUMER_COUNT_PUBLIC)
if(NOT CONSUMER_COUNT_PUBLIC GREATER CONSUMER_COUNT_PRIVATE)
    message(FATAL_ERROR "public change did not invalidate the consumer")
endif()

# Options are isolated, and --no-cache neither reads nor writes cache.
run_build("release isolation" --release)
file(GLOB ENTRIES "${CACHE_ROOT}/entries/*.entry")
list(LENGTH ENTRIES RELEASE_COUNT)
if(NOT RELEASE_COUNT GREATER WARM_COUNT)
    message(FATAL_ERROR "debug and release shared an incompatible artifact")
endif()
set(CACHE_COUNT_BEFORE_NO_CACHE "${RELEASE_COUNT}")
run_build("no cache" --no-cache)
file(GLOB ENTRIES "${CACHE_ROOT}/entries/*.entry")
list(LENGTH ENTRIES CACHE_COUNT_AFTER_NO_CACHE)
if(NOT CACHE_COUNT_AFTER_NO_CACHE EQUAL CACHE_COUNT_BEFORE_NO_CACHE)
    message(FATAL_ERROR "--no-cache wrote a cache entry")
endif()

# A corrupt artifact falls back to a clean build and repairs the entry.
list(GET ENTRIES 0 ENTRY)
get_filename_component(ENTRY_NAME "${ENTRY}" NAME_WE)
set(ARTIFACT "${CACHE_ROOT}/artifacts/${ENTRY_NAME}.bin")
file(WRITE "${ARTIFACT}" "corrupt")
run_build("corruption fallback")
execute_process(
    COMMAND "${PROJECT}/target/debug/cached"
    RESULT_VARIABLE RUN_STATUS
)
if(NOT RUN_STATUS EQUAL 11)
    message(FATAL_ERROR "corruption fallback produced stale/invalid binary: ${RUN_STATUS}")
endif()

# Cached output is behaviorally equivalent to a cache-disabled clean output.
run_build("clean comparison" --no-cache -o "${CLEAN_OUTPUT}")
execute_process(COMMAND "${PROJECT}/target/debug/cached"
                RESULT_VARIABLE CACHED_RESULT)
execute_process(COMMAND "${CLEAN_OUTPUT}" RESULT_VARIABLE CLEAN_RESULT)
if(NOT CACHED_RESULT EQUAL CLEAN_RESULT OR NOT CLEAN_RESULT EQUAL 11)
    message(FATAL_ERROR
            "cached and clean build outputs differ: cached=${CACHED_RESULT}, clean=${CLEAN_RESULT}")
endif()

# A standalone source with the default basename output has an empty parent path.
set(STANDALONE "${ROOT}/standalone")
file(MAKE_DIRECTORY "${STANDALONE}")
file(WRITE "${STANDALONE}/main.janus" "def main() : int { return 0 }\n")
execute_process(
    COMMAND "${JANUS}" build main.janus
    WORKING_DIRECTORY "${STANDALONE}"
    RESULT_VARIABLE STANDALONE_STATUS
    OUTPUT_VARIABLE STANDALONE_OUT
    ERROR_VARIABLE STANDALONE_ERR
)
set(STANDALONE_OUTPUT "${STANDALONE}/main")
if(WIN32)
    set(STANDALONE_OUTPUT "${STANDALONE_OUTPUT}.exe")
endif()
if(NOT STANDALONE_STATUS EQUAL 0 OR NOT EXISTS "${STANDALONE_OUTPUT}")
    message(FATAL_ERROR
            "standalone default output failed: ${STANDALONE_STATUS}\n${STANDALONE_OUT}\n${STANDALONE_ERR}")
endif()

# Source paths are observable in LLVM IR and therefore isolate cache keys.
file(WRITE "${STANDALONE}/same-a.janus" "def main() : int { return 0 }\n")
file(WRITE "${STANDALONE}/same-b.janus" "def main() : int { return 0 }\n")
execute_process(
    COMMAND "${JANUS}" build same-a.janus --emit llvm-ir -o same-a.ll
    WORKING_DIRECTORY "${STANDALONE}" RESULT_VARIABLE IR_A_STATUS)
execute_process(
    COMMAND "${JANUS}" build same-b.janus --emit llvm-ir -o same-b.cached.ll
    WORKING_DIRECTORY "${STANDALONE}" RESULT_VARIABLE IR_B_CACHE_STATUS)
execute_process(
    COMMAND "${JANUS}" build same-b.janus --emit llvm-ir --no-cache -o same-b.clean.ll
    WORKING_DIRECTORY "${STANDALONE}" RESULT_VARIABLE IR_B_CLEAN_STATUS)
if(NOT IR_A_STATUS EQUAL 0 OR NOT IR_B_CACHE_STATUS EQUAL 0 OR
   NOT IR_B_CLEAN_STATUS EQUAL 0)
    message(FATAL_ERROR "source-path cache isolation build failed")
endif()
file(SHA256 "${STANDALONE}/same-b.cached.ll" IR_B_CACHE_SHA)
file(SHA256 "${STANDALONE}/same-b.clean.ll" IR_B_CLEAN_SHA)
if(NOT IR_B_CACHE_SHA STREQUAL IR_B_CLEAN_SHA)
    message(FATAL_ERROR "cached LLVM IR retained another source path")
endif()

# clean removes only Janus build products and is idempotent.
file(WRITE "${PROJECT}/keep.txt" "keep")
execute_process(
    COMMAND "${JANUS}" clean
    WORKING_DIRECTORY "${PROJECT}"
    RESULT_VARIABLE CLEAN_STATUS
    OUTPUT_VARIABLE CLEAN_OUT
    ERROR_VARIABLE CLEAN_ERR
)
if(NOT CLEAN_STATUS EQUAL 0 OR EXISTS "${PROJECT}/target"
   OR NOT EXISTS "${PROJECT}/keep.txt")
    message(FATAL_ERROR "janus clean contract failed: ${CLEAN_STATUS}\n${CLEAN_OUT}\n${CLEAN_ERR}")
endif()
execute_process(
    COMMAND "${JANUS}" clean
    WORKING_DIRECTORY "${PROJECT}"
    RESULT_VARIABLE CLEAN_AGAIN_STATUS
)
if(NOT CLEAN_AGAIN_STATUS EQUAL 0)
    message(FATAL_ERROR "janus clean is not idempotent")
endif()
