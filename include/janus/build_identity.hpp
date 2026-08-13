#pragma once

#include "janus-build-identity.hpp"

#include <string>
#include <string_view>

namespace janus::build {
inline constexpr const char *version = JANUS_BUILD_VERSION;
inline constexpr const char *display_version = JANUS_BUILD_DISPLAY_VERSION;
inline constexpr const char *revision = JANUS_BUILD_REVISION;
inline constexpr const char *channel = JANUS_BUILD_CHANNEL;
inline constexpr const char *identity = JANUS_BUILD_IDENTITY;
inline constexpr const char *target = JANUS_BUILD_TARGET;
inline constexpr const char *llvm = JANUS_BUILD_LLVM;
inline constexpr const char *source_digest = JANUS_BUILD_SOURCE_DIGEST;
inline constexpr bool dirty = std::string_view{JANUS_BUILD_DIRTY} == "true";
inline std::string escape_json(std::string_view value) {
  std::string result;
  constexpr char hex[] = "0123456789abcdef";
  for (const unsigned char character : value) {
    switch (character) {
    case '"': result += "\\\""; break;
    case '\\': result += "\\\\"; break;
    case '\b': result += "\\b"; break;
    case '\f': result += "\\f"; break;
    case '\n': result += "\\n"; break;
    case '\r': result += "\\r"; break;
    case '\t': result += "\\t"; break;
    default:
      if (character < 0x20) {
        result += "\\u00";
        result += hex[character >> 4];
        result += hex[character & 0x0f];
      } else {
        result += static_cast<char>(character);
      }
    }
  }
  return result;
}
inline std::string json() {
  return std::string{"{\"schema_version\":1,\"version\":\""} + escape_json(version) +
         "\",\"display_version\":\"" + escape_json(display_version) +
         "\",\"revision\":\"" + escape_json(revision) + "\",\"dirty\":" +
         (dirty ? "true" : "false") + ",\"channel\":\"" + escape_json(channel) +
         "\",\"identity\":\"" + escape_json(identity) +
         "\",\"target\":\"" + escape_json(target) +
         "\",\"llvm\":\"" + escape_json(llvm) + "\",\"source_digest\":" +
         (std::string_view{source_digest}.empty()
              ? "null"
              : std::string{"\""} + escape_json(source_digest) + "\"") + "}";
}
} // namespace janus::build
