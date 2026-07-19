if(NOT DEFINED ARCHIVE OR NOT EXISTS "${ARCHIVE}")
    message(FATAL_ERROR "ARCHIVE must name an existing package")
endif()

file(SHA256 "${ARCHIVE}" DIGEST)
get_filename_component(FILENAME "${ARCHIVE}" NAME)
file(WRITE "${ARCHIVE}.sha256" "${DIGEST}  ${FILENAME}\n")
