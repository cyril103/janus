if(NOT DEFINED JANUSC OR NOT DEFINED WORK_DIR)
  message(FATAL_ERROR "JANUSC and WORK_DIR are required")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

function(write_probe NAME SOURCE)
  file(WRITE "${WORK_DIR}/${NAME}.janus" "${SOURCE}")
endfunction()

function(expect_failure NAME EXPECTED)
  execute_process(
    COMMAND "${JANUSC}" ${ARGN} "${WORK_DIR}/${NAME}.janus"
    WORKING_DIRECTORY "${WORK_DIR}"
    RESULT_VARIABLE STATUS OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
  if(STATUS EQUAL 0 OR NOT ERROR MATCHES "${EXPECTED}")
    message(FATAL_ERROR
      "${NAME} should fail with /${EXPECTED}/ (status ${STATUS})\n${ERROR}\n${OUTPUT}")
  endif()
endfunction()

function(expect_success NAME)
  execute_process(
    COMMAND "${JANUSC}" ${ARGN} "${WORK_DIR}/${NAME}.janus"
    WORKING_DIRECTORY "${WORK_DIR}"
    RESULT_VARIABLE STATUS OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
  if(NOT STATUS EQUAL 0)
    message(FATAL_ERROR "${NAME} should succeed\n${ERROR}\n${OUTPUT}")
  endif()
endfunction()

write_probe(static-false
  "staticAssert(false, \"must fail\")\ndef main() : int { return 0 }\n")
expect_failure(static-false "static assertion failed: must fail")

write_probe(val-dependency
  "val ordinary : int = 7\nconst invalid : int = ordinary\ndef main() : int { return invalid }\n")
expect_failure(val-dependency "cannot depend on non-constant global 'ordinary'")

write_probe(zero-step-expression
  "const result : int = 1 + 1\ndef main() : int { return result }\n")
expect_failure(zero-step-expression "step budget exceeded \\(0\\)" --const-steps 0)

write_probe(local-budget
  "def main() : int {\n    const text : string = \"arbitrarily large local constant\"\n    return 0\n}\n")
expect_failure(local-budget "memory budget exceeded \\(1 bytes\\)"
  --const-memory 1 --const-value-size 1)

write_probe(float-overflow
  "const x : float = 3.4e38f * 2.0f\ndef main() : int { return 0 }\n")
expect_failure(float-overflow "floating constant expression overflows type 'float'")

write_probe(float-rounding
  "const x : float = 16777216.0f + 1.0f\nstaticAssert(x == 16777216.0f)\ndef main() : int { return 0 }\n")
expect_success(float-rounding)

write_probe(generic-wide
  "const def identity[T](value : T) : T { return value }\nconst wide : long = identity[long](2147483648)\ndef main() : int { return int(wide) }\n")
expect_success(generic-wide)

write_probe(private_mod
  "module private_mod\nprivate const secret : int = 42\nconst visible : int = 1\n")
write_probe(bridge
  "module bridge\nimport private_mod\nconst bridgeValue : int = private_mod.visible\n")
write_probe(static-private
  "import private_mod\nstaticAssert(private_mod.secret == 42)\ndef main() : int { return 0 }\n")
expect_failure(static-private "private")
write_probe(static-transitive-unimported
  "import bridge\nstaticAssert(private_mod.visible == 1)\ndef main() : int { return 0 }\n")
expect_failure(static-transitive-unimported "not imported")

write_probe(self_assert_mod
  "module self_assert_mod\nprivate const secret : int = 42\nstaticAssert(secret == 42)\n")
write_probe(import-self-assert
  "import self_assert_mod\ndef main() : int { return 0 }\n")
expect_success(import-self-assert)

write_probe(imported_false
  "module imported_false\nstaticAssert(false, \"imported failure\")\n")
write_probe(import-false-entry
  "import imported_false\ndef main() : int { return 0 }\n")
execute_process(
  COMMAND "${JANUSC}" "${WORK_DIR}/import-false-entry.janus"
  WORKING_DIRECTORY "${WORK_DIR}"
  RESULT_VARIABLE IMPORTED_STATUS OUTPUT_VARIABLE IMPORTED_OUTPUT
  ERROR_VARIABLE IMPORTED_ERROR)
if(IMPORTED_STATUS EQUAL 0 OR
   NOT IMPORTED_ERROR MATCHES "imported_false.janus:2:1: error: \\[JANA0999\\] static assertion failed: imported failure")
  message(FATAL_ERROR
    "imported static assertion lost its failure or provenance\n${IMPORTED_ERROR}\n${IMPORTED_OUTPUT}")
endif()
