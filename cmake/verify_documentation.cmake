if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
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

file(READ "${SOURCE_DIR}/editors/vscode/package.json" VSCODE_PACKAGE)
string(JSON VSCODE_VERSION GET "${VSCODE_PACKAGE}" version)
if(NOT VSCODE_VERSION STREQUAL PROJECT_VERSION)
    message(FATAL_ERROR
        "VS Code extension version ${VSCODE_VERSION} does not match Janus ${PROJECT_VERSION}")
endif()

file(READ "${SOURCE_DIR}/editors/vscode/package-lock.json" VSCODE_LOCK)
string(JSON VSCODE_LOCK_VERSION GET "${VSCODE_LOCK}" version)
string(JSON VSCODE_LOCK_ROOT_VERSION GET "${VSCODE_LOCK}" packages "" version)
if(NOT VSCODE_LOCK_VERSION STREQUAL PROJECT_VERSION
   OR NOT VSCODE_LOCK_ROOT_VERSION STREQUAL PROJECT_VERSION)
    message(FATAL_ERROR
        "VS Code lockfile versions do not match Janus ${PROJECT_VERSION}")
endif()

file(READ "${SOURCE_DIR}/CHANGELOG.md" CHANGELOG)
string(FIND "${CHANGELOG}" "## [${PROJECT_VERSION}]" CHANGELOG_VERSION)
if(CHANGELOG_VERSION EQUAL -1)
    message(FATAL_ERROR
        "CHANGELOG.md has no section for Janus ${PROJECT_VERSION}")
endif()

file(READ "${SOURCE_DIR}/README.md" README)
string(REGEX MATCH
    "Janus [0-9]+\\.[0-9]+(\\.[0-9]+)? est expérimental"
    STALE_README_VERSION
    "${README}"
)
if(STALE_README_VERSION)
    message(FATAL_ERROR
        "README.md contains a perishable experimental version: ${STALE_README_VERSION}")
endif()

file(READ "${SOURCE_DIR}/docs/graphics.md" GRAPHICS_GUIDE)
string(FIND "${GRAPHICS_GUIDE}" "les polices personnalisées, les manettes" STALE_GRAPHICS_LIMIT)
if(NOT STALE_GRAPHICS_LIMIT EQUAL -1)
    message(FATAL_ERROR
        "graphics guide still lists custom fonts and gamepads as unavailable")
endif()

foreach(REQUIRED_TEXT
        "## Polices et texte UTF-8"
        "loadFontUtf8"
        "### Manettes"
        "isGamepadAvailable"
        "setGamepadVibration")
    string(FIND "${GRAPHICS_GUIDE}" "${REQUIRED_TEXT}" REQUIRED_TEXT_POSITION)
    if(REQUIRED_TEXT_POSITION EQUAL -1)
        message(FATAL_ERROR
            "graphics guide does not document available API: ${REQUIRED_TEXT}")
    endif()
endforeach()

file(READ "${SOURCE_DIR}/stdlib/std/graphics/resources.janus" GRAPHICS_RESOURCES)
file(READ "${SOURCE_DIR}/stdlib/std/graphics/input.janus" GRAPHICS_INPUT)
foreach(REQUIRED_SYMBOL loadFontUtf8)
    string(FIND "${GRAPHICS_RESOURCES}" "${REQUIRED_SYMBOL}" SYMBOL_POSITION)
    if(SYMBOL_POSITION EQUAL -1)
        message(FATAL_ERROR "graphics resources lack ${REQUIRED_SYMBOL}")
    endif()
endforeach()
foreach(REQUIRED_SYMBOL isGamepadAvailable setGamepadVibration)
    string(FIND "${GRAPHICS_INPUT}" "${REQUIRED_SYMBOL}" SYMBOL_POSITION)
    if(SYMBOL_POSITION EQUAL -1)
        message(FATAL_ERROR "graphics input module lacks ${REQUIRED_SYMBOL}")
    endif()
endforeach()

message(STATUS "Janus ${PROJECT_VERSION} documentation consistency verified")
