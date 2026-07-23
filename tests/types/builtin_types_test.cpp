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

  const janus::Type &uint_type = janus::Type::uint_type();
  expect(uint_type.name() == "uint", "uint has the expected name");
  expect(uint_type.bit_width() == 32, "uint is 32 bits wide");
  expect(uint_type.is_integer(), "uint is an integer");
  expect(!uint_type.is_signed(), "uint is unsigned");
  expect(janus::backend::llvm::lower_type(uint_type, context)->isIntegerTy(32),
         "uint lowers to LLVM i32");

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

  const janus::Type &ubyte_type = janus::Type::ubyte_type();
  expect(ubyte_type.name() == "ubyte", "ubyte has the expected name");
  expect(ubyte_type.bit_width() == 8, "ubyte is 8 bits wide");
  expect(ubyte_type.is_integer(), "ubyte is an integer");
  expect(!ubyte_type.is_signed(), "ubyte is unsigned");
  expect(janus::backend::llvm::lower_type(ubyte_type, context)->isIntegerTy(8),
         "ubyte lowers to LLVM i8");

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

  const janus::Type &string_type = janus::Type::string_type();
  expect(string_type.name() == "string", "string has the expected name");
  expect(string_type.is_string(), "string is a string type");
  auto *llvm_string_type = llvm::dyn_cast<llvm::StructType>(
      janus::backend::llvm::lower_type(string_type, context));
  expect(llvm_string_type != nullptr, "string lowers to an LLVM structure");
  expect(llvm_string_type != nullptr && llvm_string_type->getNumElements() == 2,
         "string contains a pointer and a length");
  expect(llvm_string_type != nullptr &&
             llvm_string_type->getElementType(0)->isPointerTy(),
         "string data is represented by a pointer");
  expect(llvm_string_type != nullptr &&
             llvm_string_type->getElementType(1)->isIntegerTy(64),
         "string byte length is represented by i64");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }

  std::cout << "primitive types lower to their LLVM representations\n";
  return 0;
}
