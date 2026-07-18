#include "janus/backend/llvm/type_lowering.hpp"
#include "janus/types/type.hpp"

#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/LLVMContext.h>

#include <iostream>
#include <string_view>

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
  }
}

} // namespace

int main() {
  const janus::Type &type = janus::Type::int_type();

  expect(type.kind() == janus::TypeKind::Int, "kind is Int");
  expect(type.name() == "int", "source-language name is int");
  expect(type.bit_width() == 32, "int is exactly 32 bits wide");
  expect(type.is_integer(), "int is an integer type");
  expect(type.is_signed(), "int is signed");

  llvm::LLVMContext context;
  llvm::Type *llvm_type = janus::backend::llvm::lower_type(type, context);

  expect(llvm_type != nullptr, "int can be lowered to LLVM");
  expect(llvm_type != nullptr && llvm_type->isIntegerTy(32),
         "int lowers to LLVM i32");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }

  std::cout << "int: signed 32-bit integer -> LLVM i32\n";
  return 0;
}
