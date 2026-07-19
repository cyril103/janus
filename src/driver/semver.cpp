#include "janus/driver/semver.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::string trim(std::string_view value) {
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.front())))
    value.remove_prefix(1);
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.back())))
    value.remove_suffix(1);
  return std::string{value};
}

std::uint64_t parse_number(std::string_view value) {
  if (value.empty() || (value.size() > 1 && value.front() == '0'))
    throw std::runtime_error{"invalid semantic version number"};
  std::uint64_t result{};
  const auto parsed =
      std::from_chars(value.data(), value.data() + value.size(), result);
  if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
    throw std::runtime_error{"invalid semantic version number"};
  return result;
}

bool numeric_identifier(std::string_view value) {
  return !value.empty() &&
         std::all_of(value.begin(), value.end(), [](unsigned char character) {
           return std::isdigit(character) != 0;
         });
}

std::vector<std::string> split(std::string_view value, char separator) {
  std::vector<std::string> result;
  while (true) {
    const std::size_t position = value.find(separator);
    result.emplace_back(value.substr(0, position));
    if (position == std::string_view::npos)
      return result;
    value.remove_prefix(position + 1);
  }
}

bool satisfies_comparator(std::string comparator,
                          const janus::driver::SemanticVersion &version) {
  comparator = trim(comparator);
  std::string_view operation{"="};
  for (const std::string_view candidate : {">=", "<=", ">", "<", "="}) {
    if (comparator.starts_with(candidate)) {
      operation = candidate;
      comparator.erase(0, candidate.size());
      break;
    }
  }
  const janus::driver::SemanticVersion expected =
      janus::driver::parse_semantic_version(trim(comparator));
  const std::strong_ordering order = janus::driver::compare(version, expected);
  if (operation == ">=")
    return order >= 0;
  if (operation == "<=")
    return order <= 0;
  if (operation == ">")
    return order > 0;
  if (operation == "<")
    return order < 0;
  return order == 0;
}

} // namespace

namespace janus::driver {

std::string SemanticVersion::str() const {
  std::string result = std::to_string(major) + "." + std::to_string(minor) +
                       "." + std::to_string(patch);
  if (!prerelease.empty()) {
    result += '-';
    for (std::size_t index = 0; index < prerelease.size(); ++index) {
      if (index != 0)
        result += '.';
      result += prerelease[index];
    }
  }
  return result;
}

SemanticVersion parse_semantic_version(std::string_view text) {
  const std::size_t build = text.find('+');
  if (build != std::string_view::npos)
    text = text.substr(0, build);
  std::string_view core = text;
  std::string_view prerelease;
  if (const std::size_t dash = text.find('-'); dash != std::string_view::npos) {
    core = text.substr(0, dash);
    prerelease = text.substr(dash + 1);
  }
  const std::vector<std::string> numbers = split(core, '.');
  if (numbers.size() != 3)
    throw std::runtime_error{"semantic version requires major.minor.patch"};
  SemanticVersion version{parse_number(numbers[0]),
                          parse_number(numbers[1]),
                          parse_number(numbers[2]),
                          {}};
  if (!prerelease.empty()) {
    version.prerelease = split(prerelease, '.');
    for (const std::string &identifier : version.prerelease) {
      if (identifier.empty() ||
          !std::all_of(identifier.begin(), identifier.end(),
                       [](unsigned char character) {
                         return std::isalnum(character) || character == '-';
                       }) ||
          (numeric_identifier(identifier) && identifier.size() > 1 &&
           identifier.front() == '0'))
        throw std::runtime_error{"invalid semantic version prerelease"};
    }
  } else if (text.find('-') != std::string_view::npos) {
    throw std::runtime_error{"empty semantic version prerelease"};
  }
  return version;
}

std::strong_ordering compare(const SemanticVersion &left,
                             const SemanticVersion &right) {
  if (left.major != right.major)
    return left.major <=> right.major;
  if (left.minor != right.minor)
    return left.minor <=> right.minor;
  if (left.patch != right.patch)
    return left.patch <=> right.patch;
  if (left.prerelease.empty() || right.prerelease.empty()) {
    if (left.prerelease.empty() == right.prerelease.empty())
      return std::strong_ordering::equal;
    return left.prerelease.empty() ? std::strong_ordering::greater
                                   : std::strong_ordering::less;
  }
  const std::size_t count =
      std::min(left.prerelease.size(), right.prerelease.size());
  for (std::size_t index = 0; index < count; ++index) {
    const std::string &a = left.prerelease[index];
    const std::string &b = right.prerelease[index];
    if (a == b)
      continue;
    const bool a_numeric = numeric_identifier(a);
    const bool b_numeric = numeric_identifier(b);
    if (a_numeric && b_numeric)
      return parse_number(a) <=> parse_number(b);
    if (a_numeric != b_numeric)
      return a_numeric ? std::strong_ordering::less
                       : std::strong_ordering::greater;
    return a < b ? std::strong_ordering::less : std::strong_ordering::greater;
  }
  return left.prerelease.size() <=> right.prerelease.size();
}

bool matches_version(std::string_view requirement,
                     const SemanticVersion &version) {
  const std::string normalized = trim(requirement);
  if (normalized.empty() || normalized == "*")
    return true;
  if (normalized.front() == '^' || normalized.front() == '~') {
    const char kind = normalized.front();
    const SemanticVersion lower =
        parse_semantic_version(std::string_view{normalized}.substr(1));
    SemanticVersion upper = lower;
    upper.prerelease.clear();
    if (kind == '~') {
      ++upper.minor;
      upper.patch = 0;
    } else if (upper.major != 0) {
      ++upper.major;
      upper.minor = 0;
      upper.patch = 0;
    } else if (upper.minor != 0) {
      ++upper.minor;
      upper.patch = 0;
    } else {
      ++upper.patch;
    }
    return compare(version, lower) >= 0 && compare(version, upper) < 0;
  }
  if (normalized.ends_with(".*")) {
    const std::vector<std::string> parts = split(normalized, '.');
    if (parts.size() == 2)
      return version.major == parse_number(parts[0]);
    if (parts.size() == 3)
      return version.major == parse_number(parts[0]) &&
             version.minor == parse_number(parts[1]);
    throw std::runtime_error{"invalid wildcard version requirement"};
  }
  bool result = true;
  for (const std::string &comparator : split(normalized, ','))
    result = satisfies_comparator(comparator, version) && result;
  return result;
}

} // namespace janus::driver
