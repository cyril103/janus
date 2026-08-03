#pragma once

#include <cstdio>
#include <cstdlib>

namespace janus::test {

[[noreturn]] inline void fail_requirement(const char *expression,
                                          const char *file, int line) {
  std::fprintf(stderr, "%s:%d: requirement failed: %s\n", file, line,
               expression);
  std::exit(EXIT_FAILURE);
}

inline void require(bool condition, const char *expression, const char *file,
                    int line) {
  if (!condition)
    fail_requirement(expression, file, line);
}

} // namespace janus::test

#define JANUS_REQUIRE(condition)                                               \
  ::janus::test::require(static_cast<bool>(condition), #condition, __FILE__,   \
                         __LINE__)
