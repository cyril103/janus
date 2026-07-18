#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace janus {

enum class TypeKind {
  Int,
  Double,
  Byte,
  Char,
  Bool,
  String,
  Unit,
  Class,
};

class Type final {
public:
  [[nodiscard]] static const Type &int_type();
  [[nodiscard]] static const Type &double_type();
  [[nodiscard]] static const Type &byte_type();
  [[nodiscard]] static const Type &char_type();
  [[nodiscard]] static const Type &bool_type();
  [[nodiscard]] static const Type &string_type();
  [[nodiscard]] static const Type &unit_type();
  [[nodiscard]] static Type class_type(std::string_view name);

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
