#include "janus/driver/native_test.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
int failures = 0;
void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "failure: " << message << '\n';
    ++failures;
  }
}
} // namespace

int main() {
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "janus-native-test-test";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "nested");
  {
    std::ofstream output{root / "native.janus"};
    output << "/// @test\n/// @serial\n"
              "def succeeds() : Unit {}\n"
              "/// @test\n/// @ignore\n/// @shouldPanic expected failure\n"
              "def panics() : Unit { panic(\"expected failure\") }\n";
  }
  {
    std::ofstream output{root / "nested/legacy.janus"};
    output << "def main() : int { return 0 }\n";
  }
  const auto tests = janus::driver::discover_native_tests(root);
  expect(tests.size() == 3, "native functions and legacy files are discovered");
  expect(tests[0].identifier == "native.succeeds", "identifier is stable");
  expect(tests[0].serial, "serial metadata is retained");
  expect(tests[1].ignored, "ignored metadata is retained");
  expect(tests[1].expected_panic == "expected failure",
         "panic message is retained");
  expect(tests[2].legacy, "legacy main-based tests remain supported");
  expect(janus::driver::matches_native_test_filter(tests[0], "succeed", false),
         "substring filter matches");
  expect(!janus::driver::matches_native_test_filter(tests[0], "succeed", true),
         "exact filter rejects partial identifiers");
  expect(janus::driver::native_test_source("def f() : Unit {}", tests[0])
                 .find("succeeds()") != std::string::npos,
         "a native test receives an isolated entry point");
  std::filesystem::remove_all(root);
  return failures == 0 ? 0 : 1;
}
