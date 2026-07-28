#include "janus/driver/doctest.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
  }
}

} // namespace

int main() {
  const std::string markdown = R"(# Examples

```janus incomplete
val fragment : int = 1
```

```janus
// doctest: doctest name=working-example
def main() : int { return 0 }
```

```janus
// doctest: compile_fail=JANA0001 name=unknown-value
def main() : int { return missing }
```

```janus title="illustration.janus"
def main() : int { return 0 }
```
)";

  const std::vector<janus::driver::Doctest> parsed =
      janus::driver::parse_doctests("docs/examples.md", markdown);
  expect(parsed.size() == 2, "only explicit doctests are executable");
  expect(parsed[0].name == "working-example",
         "an explicit doctest name is retained");
  expect(parsed[0].line == 9, "the source line is retained");
  expect(parsed[0].expectation ==
             janus::driver::DoctestExpectation::CompilePass,
         "ordinary doctests expect successful compilation");
  expect(parsed[1].expected_diagnostic == "JANA0001",
         "compile-fail expectations retain the diagnostic code");
  expect(parsed[1].line == 14, "compile-fail source line is retained");
  expect(parsed[1].display_name() == "docs/examples.md:14 (unknown-value)",
         "display names contain document, line, and optional name");

  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "janus-doctest-test";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "docs" / "nested");
  {
    std::ofstream output{root / "README.md"};
    output << "```janus\n"
              "// doctest: doctest name=readme\n"
              "def main() : int { return 0 }\n"
              "```\n";
  }
  {
    std::ofstream output{root / "docs" / "nested" / "z.md"};
    output << "```janus\n"
              "// doctest: doctest name=nested\n"
              "def main() : int { return 0 }\n"
              "```\n";
  }

  const std::vector<janus::driver::Doctest> discovered =
      janus::driver::discover_doctests(root, {"README.md", "docs"});
  expect(discovered.size() == 2, "files and directories are discovered");
  expect(discovered[0].document.generic_string() == "README.md" &&
             discovered[1].document.generic_string() == "docs/nested/z.md",
         "discovery is deterministic and paths are package-relative");
  expect(janus::driver::matches_doctest_filter(discovered[1], "nested"),
         "named doctests are filterable");
  expect(janus::driver::matches_doctest_filter(discovered[1], "z.md:3"),
         "document and line are filterable");
  expect(!janus::driver::matches_doctest_filter(discovered[0], "nested"),
         "filters reject unrelated doctests");

  bool invalid_code_rejected = false;
  try {
    static_cast<void>(janus::driver::parse_doctests(
        "docs/invalid.md", "```janus\n"
                           "// doctest: compile_fail=unknown\n"
                           "def main() : int { return 0 }\n"
                           "```\n"));
  } catch (const std::runtime_error &error) {
    invalid_code_rejected = std::string{error.what()}.find(
                                "docs/invalid.md:2") != std::string::npos;
  }
  expect(invalid_code_rejected,
         "invalid directives report the document and fence line");

  std::filesystem::remove_all(root);
  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }
  std::cout << "documentation test discovery is deterministic\n";
  return 0;
}
