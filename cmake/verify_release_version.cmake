if(NOT DEFINED TAG OR NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "TAG and SOURCE_DIR are required")
endif()

string(REGEX REPLACE "^v" "" TAG_VERSION "${TAG}")
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
if(NOT TAG_VERSION STREQUAL PROJECT_VERSION)
    message(
        FATAL_ERROR
        "release tag ${TAG} does not match project version ${PROJECT_VERSION}"
    )
endif()
message(STATUS "release version ${PROJECT_VERSION} verified")
