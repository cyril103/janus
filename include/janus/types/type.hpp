#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace janus {

enum class TypeKind {
  Int,
  UInt,
  Long,
  ULong,
  Float,
  Double,
  Byte,
  UByte,
  Short,
  UShort,
  Char,
  Bool,
  String,
  Unit,
  ISize,
  USize,
  Enum,
  Function,
  Pointer,
  Class,
  Struct,
};

class Type final {
public:
  [[nodiscard]] static const Type &int_type();
  [[nodiscard]] static const Type &uint_type();
  [[nodiscard]] static const Type &long_type();
  [[nodiscard]] static const Type &ulong_type();
  [[nodiscard]] static const Type &float_type();
  [[nodiscard]] static const Type &double_type();
  [[nodiscard]] static const Type &byte_type();
  [[nodiscard]] static const Type &ubyte_type();
  [[nodiscard]] static const Type &short_type();
  [[nodiscard]] static const Type &ushort_type();
  [[nodiscard]] static const Type &char_type();
  [[nodiscard]] static const Type &bool_type();
  [[nodiscard]] static const Type &string_type();
  [[nodiscard]] static const Type &unit_type();
  [[nodiscard]] static const Type &isize_type();
  [[nodiscard]] static const Type &usize_type();
  [[nodiscard]] static Type enum_type(std::string_view name);
  [[nodiscard]] static Type function_type(std::string_view name);
  [[nodiscard]] static Type pointer_type(std::string_view name);
  [[nodiscard]] static Type class_type(std::string_view name);
  [[nodiscard]] static Type struct_type(std::string_view name);

  [[nodiscard]] TypeKind kind() const noexcept;
  [[nodiscard]] std::string_view name() const noexcept;
  [[nodiscard]] std::uint32_t bit_width() const noexcept;
  [[nodiscard]] bool is_integer() const noexcept;
  [[nodiscard]] bool is_floating_point() const noexcept;
  [[nodiscard]] bool is_character() const noexcept;
  [[nodiscard]] bool is_boolean() const noexcept;
  [[nodiscard]] bool is_string() const noexcept;
  [[nodiscard]] bool is_unit() const noexcept;
  [[nodiscard]] bool is_signed() const noexcept;

private:
  Type(TypeKind kind, std::string_view name, std::uint32_t bit_width,
       bool is_signed);

  TypeKind kind_;
  std::string name_;
  std::uint32_t bit_width_;
  bool is_signed_;
};

} // namespace janus
