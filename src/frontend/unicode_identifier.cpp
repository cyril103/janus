#include "janus/frontend/unicode_identifier.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <vector>

namespace janus::frontend::unicode {
namespace {

struct CodePointRange {
  char32_t first;
  char32_t last;
};
struct Decomposition {
  char32_t scalar;
  std::uint16_t offset;
  std::uint8_t length;
};
struct CombiningClass {
  char32_t scalar;
  std::uint8_t value;
};
struct Composition {
  char32_t first;
  char32_t second;
  char32_t result;
};

#include "unicode_identifier_tables.inc"

template <typename Range, std::size_t Size>
bool contains(const Range (&ranges)[Size], char32_t value) noexcept {
  const auto found = std::lower_bound(
      std::begin(ranges), std::end(ranges), value,
      [](const Range &range, char32_t scalar) { return range.last < scalar; });
  return found != std::end(ranges) && found->first <= value;
}

std::uint8_t combining_class(char32_t value) noexcept {
  const auto found = std::lower_bound(
      std::begin(kCombiningClasses), std::end(kCombiningClasses), value,
      [](const CombiningClass &entry, char32_t scalar) {
        return entry.scalar < scalar;
      });
  return found != std::end(kCombiningClasses) && found->scalar == value
             ? found->value
             : 0;
}

void append_decomposed(char32_t value, std::vector<char32_t> &output) {
  // Hangul decomposition, Unicode Standard Annex #15 section 3.12.
  constexpr char32_t s_base = 0xAC00, l_base = 0x1100, v_base = 0x1161,
                     t_base = 0x11A7;
  constexpr int l_count = 19, v_count = 21, t_count = 28,
                n_count = v_count * t_count, s_count = l_count * n_count;
  if (value >= s_base && value < s_base + s_count) {
    const int index = static_cast<int>(value - s_base);
    output.push_back(l_base + index / n_count);
    output.push_back(v_base + (index % n_count) / t_count);
    if (const int trailing = index % t_count; trailing != 0)
      output.push_back(t_base + trailing);
    return;
  }
  const auto found =
      std::lower_bound(std::begin(kDecompositions), std::end(kDecompositions),
                       value, [](const Decomposition &entry, char32_t scalar) {
                         return entry.scalar < scalar;
                       });
  if (found == std::end(kDecompositions) || found->scalar != value) {
    output.push_back(value);
    return;
  }
  for (std::size_t index = 0; index < found->length; ++index)
    append_decomposed(kDecompositionScalars[found->offset + index], output);
}

char32_t compose_pair(char32_t first, char32_t second) noexcept {
  constexpr char32_t s_base = 0xAC00, l_base = 0x1100, v_base = 0x1161,
                     t_base = 0x11A7;
  constexpr int l_count = 19, v_count = 21, t_count = 28,
                n_count = v_count * t_count, s_count = l_count * n_count;
  if (first >= l_base && first < l_base + l_count && second >= v_base &&
      second < v_base + v_count)
    return s_base + ((first - l_base) * v_count + second - v_base) * t_count;
  if (first >= s_base && first < s_base + s_count &&
      (first - s_base) % t_count == 0 && second > t_base &&
      second < t_base + t_count)
    return first + second - t_base;

  const auto found = std::lower_bound(
      std::begin(kCompositions), std::end(kCompositions),
      std::array<char32_t, 2>{first, second},
      [](const Composition &entry, const std::array<char32_t, 2> &pair) {
        return entry.first < pair[0] ||
               (entry.first == pair[0] && entry.second < pair[1]);
      });
  return found != std::end(kCompositions) && found->first == first &&
                 found->second == second
             ? found->result
             : 0;
}

void append_utf8(std::string &output, char32_t value) {
  if (value <= 0x7F)
    output.push_back(static_cast<char>(value));
  else if (value <= 0x7FF) {
    output.push_back(static_cast<char>(0xC0 | value >> 6));
    output.push_back(static_cast<char>(0x80 | (value & 0x3F)));
  } else if (value <= 0xFFFF) {
    output.push_back(static_cast<char>(0xE0 | value >> 12));
    output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | (value & 0x3F)));
  } else {
    output.push_back(static_cast<char>(0xF0 | value >> 18));
    output.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | (value & 0x3F)));
  }
}

} // namespace

DecodedScalar decode(std::string_view source, std::size_t offset) {
  if (offset >= source.size())
    throw std::invalid_argument{"incomplete UTF-8 sequence"};
  const auto lead = static_cast<unsigned char>(source[offset]);
  if (lead < 0x80)
    return {lead, 1};
  std::size_t length = 0;
  char32_t value = 0;
  char32_t minimum = 0;
  if (lead >= 0xC2 && lead <= 0xDF) {
    length = 2;
    value = lead & 0x1F;
    minimum = 0x80;
  } else if (lead >= 0xE0 && lead <= 0xEF) {
    length = 3;
    value = lead & 0x0F;
    minimum = 0x800;
  } else if (lead >= 0xF0 && lead <= 0xF4) {
    length = 4;
    value = lead & 0x07;
    minimum = 0x10000;
  } else
    throw std::invalid_argument{"invalid UTF-8 leading byte"};
  if (offset + length > source.size())
    throw std::invalid_argument{"incomplete UTF-8 sequence"};
  for (std::size_t index = 1; index < length; ++index) {
    const auto byte = static_cast<unsigned char>(source[offset + index]);
    if ((byte & 0xC0) != 0x80)
      throw std::invalid_argument{"invalid UTF-8 continuation byte"};
    value = (value << 6) | (byte & 0x3F);
  }
  if (value < minimum || value > 0x10FFFF ||
      (value >= 0xD800 && value <= 0xDFFF))
    throw std::invalid_argument{"invalid UTF-8 scalar value"};
  return {value, length};
}

bool is_xid_start(char32_t value) noexcept {
  return contains(kXidStart, value);
}
bool is_xid_continue(char32_t value) noexcept {
  return contains(kXidContinue, value);
}

bool is_disallowed_identifier_control(char32_t value) noexcept {
  // C0/C1 plus Unicode Default_Ignorable_Code_Point ranges. These code points
  // can make two identifiers look equal while retaining distinct identities.
  return value <= 0x1F || (value >= 0x7F && value <= 0x9F) || value == 0xAD ||
         value == 0x34F || value == 0x61C ||
         (value >= 0x115F && value <= 0x1160) ||
         (value >= 0x17B4 && value <= 0x17B5) ||
         (value >= 0x180B && value <= 0x180F) ||
         (value >= 0x200B && value <= 0x200F) ||
         (value >= 0x202A && value <= 0x202E) ||
         (value >= 0x2060 && value <= 0x206F) || value == 0x3164 ||
         (value >= 0xFE00 && value <= 0xFE0F) || value == 0xFEFF ||
         (value >= 0xFFF0 && value <= 0xFFF8) || value == 0xFFA0 ||
         (value >= 0x1BCA0 && value <= 0x1BCA3) ||
         (value >= 0x1D173 && value <= 0x1D17A) ||
         (value >= 0xE0000 && value <= 0xE0FFF);
}

std::string normalize_nfc(std::string_view source) {
  std::vector<char32_t> scalars;
  for (std::size_t offset = 0; offset < source.size();) {
    const DecodedScalar scalar = decode(source, offset);
    append_decomposed(scalar.value, scalars);
    offset += scalar.length;
  }
  for (std::size_t index = 1; index < scalars.size(); ++index) {
    const std::uint8_t current = combining_class(scalars[index]);
    if (current == 0)
      continue;
    std::size_t insertion = index;
    while (insertion > 0) {
      const std::uint8_t previous = combining_class(scalars[insertion - 1]);
      if (previous == 0 || previous <= current)
        break;
      std::swap(scalars[insertion], scalars[insertion - 1]);
      --insertion;
    }
  }
  if (!scalars.empty()) {
    std::size_t starter = 0;
    std::uint8_t previous_class = 0;
    for (std::size_t index = 1; index < scalars.size();) {
      const std::uint8_t current_class = combining_class(scalars[index]);
      const char32_t composed = compose_pair(scalars[starter], scalars[index]);
      if (composed != 0 &&
          (previous_class < current_class || previous_class == 0)) {
        scalars[starter] = composed;
        scalars.erase(scalars.begin() + static_cast<std::ptrdiff_t>(index));
      } else {
        if (current_class == 0)
          starter = index;
        previous_class = current_class;
        ++index;
      }
    }
  }
  std::string result;
  for (char32_t scalar : scalars)
    append_utf8(result, scalar);
  return result;
}

std::string_view unicode_version() noexcept { return kUnicodeVersion; }

} // namespace janus::frontend::unicode
