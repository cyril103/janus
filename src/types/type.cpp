#include "janus/types/type.hpp"

namespace janus {

const Type &Type::int_type() {
  static const Type type{TypeKind::Int, "int", 32, true};
  return type;
}

const Type &Type::double_type() {
  static const Type type{TypeKind::Double, "double", 64, false};
  return type;
}

const Type &Type::byte_type() {
  static const Type type{TypeKind::Byte, "byte", 8, true};
  return type;
}

const Type &Type::char_type() {
  static const Type type{TypeKind::Char, "char", 32, false};
  return type;
}

const Type &Type::bool_type() {
  static const Type type{TypeKind::Bool, "bool", 1, false};
  return type;
}

const Type &Type::string_type() {
  static const Type type{TypeKind::String, "string", 0, false};
  return type;
}

const Type &Type::unit_type() {
  static const Type type{TypeKind::Unit, "Unit", 0, false};
  return type;
}

const Type &Type::usize_type() {
  static const Type type{TypeKind::USize, "usize", 64, false};
  return type;
}

Type Type::enum_type(std::string_view name) {
  return Type{TypeKind::Enum, name, 32, true};
}

Type Type::pointer_type(std::string_view name) {
  return Type{TypeKind::Pointer, name, 0, false};
}

Type Type::class_type(std::string_view name) {
  return Type{TypeKind::Class, name, 0, false};
}

Type::Type(TypeKind kind, std::string_view name, std::uint32_t bit_width,
           bool is_signed)
    : kind_{kind}, name_{name}, bit_width_{bit_width}, is_signed_{is_signed} {}

TypeKind Type::kind() const noexcept { return kind_; }

std::string_view Type::name() const noexcept { return name_; }

std::uint32_t Type::bit_width() const noexcept { return bit_width_; }

bool Type::is_integer() const noexcept {
  return kind_ == TypeKind::Int || kind_ == TypeKind::Byte ||
         kind_ == TypeKind::USize;
}

bool Type::is_floating_point() const noexcept {
  return kind_ == TypeKind::Double;
}

bool Type::is_character() const noexcept { return kind_ == TypeKind::Char; }

bool Type::is_boolean() const noexcept { return kind_ == TypeKind::Bool; }

bool Type::is_string() const noexcept { return kind_ == TypeKind::String; }

bool Type::is_unit() const noexcept { return kind_ == TypeKind::Unit; }

bool Type::is_signed() const noexcept { return is_signed_; }

} // namespace janus
