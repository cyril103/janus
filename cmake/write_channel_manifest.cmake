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
set(PROJECT_VERSION "${CMAKE_MATCH_1}")
string(
    REGEX MATCH
    "^v(([0-9]+\\.[0-9]+\\.[0-9]+)(-[0-9A-Za-z.-]+)?)$"
    RELEASE_VERSION
    "${RELEASE}"
)
if(NOT RELEASE_VERSION)
    message(FATAL_ERROR "invalid release name: ${RELEASE}")
endif()
set(CHANNEL_VERSION "${CMAKE_MATCH_1}")
set(RELEASE_CORE_VERSION "${CMAKE_MATCH_2}")
if(NOT RELEASE_CORE_VERSION STREQUAL PROJECT_VERSION)
    message(FATAL_ERROR
        "release ${RELEASE} does not match Janus ${PROJECT_VERSION}")
endif()
file(WRITE "${OUTPUT}" "${CHANNEL_VERSION} ${RELEASE}\n")
