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
  llvm::LLVMContext context;

  const janus::Type &double_type = janus::Type::double_type();
  expect(double_type.name() == "double", "double has the expected name");
  expect(double_type.bit_width() == 64, "double is 64 bits wide");
  expect(double_type.is_floating_point(), "double is floating point");
  expect(janus::backend::llvm::lower_type(double_type, context)->isDoubleTy(),
         "double lowers to LLVM double");

  const janus::Type &byte_type = janus::Type::byte_type();
  expect(byte_type.name() == "byte", "byte has the expected name");
  expect(byte_type.bit_width() == 8, "byte is 8 bits wide");
  expect(byte_type.is_integer(), "byte is an integer");
  expect(byte_type.is_signed(), "byte is signed");
  expect(janus::backend::llvm::lower_type(byte_type, context)->isIntegerTy(8),
         "byte lowers to LLVM i8");

  const janus::Type &char_type = janus::Type::char_type();
  expect(char_type.name() == "char", "char has the expected name");
  expect(char_type.bit_width() == 32, "char is 32 bits wide");
  expect(char_type.is_character(), "char represents a character");
  expect(janus::backend::llvm::lower_type(char_type, context)->isIntegerTy(32),
         "char lowers to LLVM i32");

  const janus::Type &bool_type = janus::Type::bool_type();
  expect(bool_type.name() == "bool", "bool has the expected name");
  expect(bool_type.bit_width() == 1, "bool contains one bit of information");
  expect(bool_type.is_boolean(), "bool is boolean");
  expect(janus::backend::llvm::lower_type(bool_type, context)->isIntegerTy(1),
         "bool lowers to LLVM i1");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }

  std::cout << "double, byte, char and bool lower to LLVM types\n";
  return 0;
}
