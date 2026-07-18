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
  case TypeKind::Double:
    return ::llvm::Type::getDoubleTy(context);
  case TypeKind::Byte:
    return ::llvm::Type::getInt8Ty(context);
  case TypeKind::Char:
    return ::llvm::Type::getInt32Ty(context);
  case TypeKind::Bool:
    return ::llvm::Type::getInt1Ty(context);
  case TypeKind::String:
    return ::llvm::StructType::get(::llvm::PointerType::getUnqual(context),
                                   ::llvm::Type::getInt64Ty(context));
  case TypeKind::Class:
    return ::llvm::PointerType::getUnqual(context);
  }

  return nullptr;
}

} // namespace janus::backend::llvm
