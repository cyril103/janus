#pragma once

#include <cstdint>
#include <string_view>

namespace janus {

enum class TypeKind {
  Int,
};

class Type final {
public:
  [[nodiscard]] static const Type &int_type() noexcept;

  [[nodiscard]] TypeKind kind() const noexcept;
  [[nodiscard]] std::string_view name() const noexcept;
  [[nodiscard]] std::uint32_t bit_width() const noexcept;
  [[nodiscard]] bool is_integer() const noexcept;
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
