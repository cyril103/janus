#pragma once

#include <cstdint>
#include <string_view>

namespace janus {

enum class TypeKind {
  Int,
  Double,
  Byte,
  Char,
  Bool,
  String,
  Class,
};

class Type final {
public:
  [[nodiscard]] static const Type &int_type() noexcept;
  [[nodiscard]] static const Type &double_type() noexcept;
  [[nodiscard]] static const Type &byte_type() noexcept;
  [[nodiscard]] static const Type &char_type() noexcept;
  [[nodiscard]] static const Type &bool_type() noexcept;
  [[nodiscard]] static const Type &string_type() noexcept;
  [[nodiscard]] static Type class_type(std::string_view name) noexcept;

  [[nodiscard]] TypeKind kind() const noexcept;
  [[nodiscard]] std::string_view name() const noexcept;
  [[nodiscard]] std::uint32_t bit_width() const noexcept;
  [[nodiscard]] bool is_integer() const noexcept;
  [[nodiscard]] bool is_floating_point() const noexcept;
  [[nodiscard]] bool is_character() const noexcept;
  [[nodiscard]] bool is_boolean() const noexcept;
  [[nodiscard]] bool is_string() const noexcept;
  [[nodiscard]] bool is_signed() const noexcept;

private:
  Type(TypeKind kind, std::string_view name, std::uint32_t bit_width,
       bool is_signed) noexcept;

  TypeKind kind_;
  std::string_view name_;
  std::uint32_t bit_width_;
  bool is_signed_;
};

} // namespace janus
