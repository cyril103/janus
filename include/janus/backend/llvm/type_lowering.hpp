#pragma once

namespace llvm {
class LLVMContext;
class Type;
} // namespace llvm

namespace janus {

class Type;

namespace backend::llvm {

[[nodiscard]] ::llvm::Type *lower_type(const Type &type,
                                       ::llvm::LLVMContext &context) noexcept;

} // namespace backend::llvm
} // namespace janus
