set(JANUS_LLVM_MINIMUM_MAJOR 18)
set(JANUS_LLVM_MAXIMUM_MAJOR 21)

if(NOT DEFINED LLVM_VERSION_MAJOR OR LLVM_VERSION_MAJOR STREQUAL "")
    message(FATAL_ERROR
            "LLVM_VERSION_MAJOR is unavailable; Janus cannot validate LLVM compatibility")
endif()

if(LLVM_VERSION_MAJOR LESS JANUS_LLVM_MINIMUM_MAJOR OR
   LLVM_VERSION_MAJOR GREATER JANUS_LLVM_MAXIMUM_MAJOR)
    message(FATAL_ERROR
            "Unsupported LLVM version: found ${LLVM_VERSION_MAJOR}. "
            "Supported LLVM versions are 18 through 21 inclusive. "
            "Select a matching LLVM_DIR, Clang, and LLD toolchain.")
endif()
