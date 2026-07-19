if(NOT DEFINED SOURCE_DIR OR NOT DEFINED RELEASE OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR "SOURCE_DIR, RELEASE and OUTPUT are required")
endif()

file(READ "${SOURCE_DIR}/CMakeLists.txt" PROJECT_FILE)
string(
    REGEX MATCH
    "project\\([\n\r\t ]*janus[\n\r\t ]+VERSION[\n\r\t ]+([0-9]+\\.[0-9]+\\.[0-9]+)"
    VERSION_DECLARATION
    "${PROJECT_FILE}"
)
if(NOT VERSION_DECLARATION)
    message(FATAL_ERROR "could not read the Janus project version")
endif()
file(WRITE "${OUTPUT}" "${CMAKE_MATCH_1} ${RELEASE}\n")
