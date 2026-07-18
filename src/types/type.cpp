#include "janus/types/type.hpp"

namespace janus {

const Type &Type::int_type() noexcept {
  static const Type type{TypeKind::Int, "int", 32, true};
  return type;
}

Type::Type(TypeKind kind, std::string_view name, std::uint32_t bit_width,
           bool is_signed) noexcept
    : kind_{kind}, name_{name}, bit_width_{bit_width}, is_signed_{is_signed} {}

TypeKind Type::kind() const noexcept { return kind_; }

std::string_view Type::name() const noexcept { return name_; }

std::uint32_t Type::bit_width() const noexcept { return bit_width_; }

bool Type::is_integer() const noexcept { return kind_ == TypeKind::Int; }

bool Type::is_signed() const noexcept { return is_signed_; }

} // namespace janus
