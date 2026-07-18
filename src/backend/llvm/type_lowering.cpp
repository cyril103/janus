#include "janus/backend/llvm/type_lowering.hpp"

#include "janus/types/type.hpp"

#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/LLVMContext.h>

namespace janus::backend::llvm {

::llvm::Type *lower_type(const Type &type,
                         ::llvm::LLVMContext &context) noexcept {
  switch (type.kind()) {
  case TypeKind::Int:
    return ::llvm::Type::getInt32Ty(context);
  }

  return nullptr;
}

} // namespace janus::backend::llvm
