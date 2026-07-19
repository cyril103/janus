#include "janus/driver/semver.hpp"

#include <iostream>
#include <stdexcept>

int main() {
  try {
    using janus::driver::matches_version;
    using janus::driver::parse_semantic_version;
    const auto stable = parse_semantic_version("1.4.2");
    if (!matches_version("^1.2.0", stable) ||
        !matches_version("~1.4.0", stable) ||
        !matches_version(">=1.0.0, <2.0.0", stable) ||
        !matches_version("1.4.*", stable) || matches_version("^2.0.0", stable))
      throw std::runtime_error{"semantic version requirement mismatch"};
    if (janus::driver::compare(parse_semantic_version("1.0.0-beta.2"),
                               parse_semantic_version("1.0.0-beta.11")) >= 0 ||
        janus::driver::compare(parse_semantic_version("1.0.0-beta"),
                               parse_semantic_version("1.0.0")) >= 0)
      throw std::runtime_error{"semantic prerelease ordering mismatch"};
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
