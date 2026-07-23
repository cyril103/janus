#include "janus/backend/llvm/type_lowering.hpp"

#include "janus/types/type.hpp"

#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/LLVMContext.h>

namespace janus::backend::llvm {

::llvm::Type *lower_type(const Type &type,
                         ::llvm::LLVMContext &context) noexcept {
  switch (type.kind()) {
  case TypeKind::Int:
  case TypeKind::UInt:
    return ::llvm::Type::getInt32Ty(context);
  case TypeKind::Double:
    return ::llvm::Type::getDoubleTy(context);
  case TypeKind::Float:
    return ::llvm::Type::getFloatTy(context);
  case TypeKind::Byte:
  case TypeKind::UByte:
    return ::llvm::Type::getInt8Ty(context);
  case TypeKind::Short:
  case TypeKind::UShort:
    return ::llvm::Type::getInt16Ty(context);
  case TypeKind::Char:
    return ::llvm::Type::getInt32Ty(context);
  case TypeKind::Bool:
    return ::llvm::Type::getInt1Ty(context);
  case TypeKind::String:
    return ::llvm::StructType::get(::llvm::PointerType::getUnqual(context),
                                   ::llvm::Type::getInt64Ty(context));
  case TypeKind::Unit:
    return ::llvm::Type::getVoidTy(context);
  case TypeKind::USize:
  case TypeKind::ISize:
  case TypeKind::Long:
  case TypeKind::ULong:
    return ::llvm::Type::getInt64Ty(context);
  case TypeKind::Enum:
    return ::llvm::Type::getInt32Ty(context);
  case TypeKind::Function:
    return ::llvm::StructType::get(::llvm::PointerType::getUnqual(context),
                                   ::llvm::PointerType::getUnqual(context));
  case TypeKind::Pointer:
    return ::llvm::PointerType::getUnqual(context);
  case TypeKind::Class:
    return ::llvm::PointerType::getUnqual(context);
  case TypeKind::Struct:
    return nullptr;
  }

  return nullptr;
}

} // namespace janus::backend::llvm
