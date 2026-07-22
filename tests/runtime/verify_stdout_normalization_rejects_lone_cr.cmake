include("${CMAKE_CURRENT_LIST_DIR}/compare_janus_output.cmake")
compare_janus_output("${EXPECTED_OUTPUT}" "${ACTUAL_OUTPUT}")
