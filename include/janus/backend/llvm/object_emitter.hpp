#pragma once

#include <filesystem>

namespace llvm {
class Module;
}

namespace janus::backend::llvm {

void emit_object(::llvm::Module &module, const std::filesystem::path &output,
                 bool optimize = false);

} // namespace janus::backend::llvm
