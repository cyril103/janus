#include "janus/diagnostics/compile_error.hpp"
#include "janus/frontend/module_loader.hpp"

#include <chrono>
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

void write_source(const std::filesystem::path &path, std::string_view source) {
  std::ofstream output{path, std::ios::binary};
  output << source;
}

} // namespace

int main() {
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      ("janus-module-loader-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);

  write_source(root / "shared.janus", "module shared\n"
                                      "def answer() : int { return 42 }\n");
  write_source(root / "left.janus",
               "module left\n"
               "import shared\n"
               "def fromLeft() : int { return shared.answer() }\n");
  write_source(root / "right.janus",
               "module right\n"
               "import shared.{answer}\n"
               "def fromRight() : int { return answer() }\n");

  janus::frontend::ModuleLoader loader;
  for (const std::string_view imports :
       {"import left\nimport right\n", "import right\nimport left\n"}) {
    write_source(root / "main.janus",
                 std::string{imports} +
                     "def main() : int { return fromLeft() + fromRight() }\n");
    try {
      const janus::ast::Program program = loader.load(root / "main.janus");
      expect(program.functions.size() == 4,
             "diamond imports load every module exactly once");
    } catch (const janus::CompileError &error) {
      std::cerr << "FAILED: valid diamond import was rejected: " << error.what()
                << '\n';
      ++failures;
    }
  }

  write_source(root / "right.janus", "module right\n"
                                     "import shared.{missing}\n"
                                     "def fromRight() : int { return 0 }\n");
  write_source(root / "main.janus",
               "import left\n"
               "import right\n"
               "def main() : int { return fromLeft() + fromRight() }\n");
  bool invalid_import_localized = false;
  try {
    static_cast<void>(loader.load(root / "main.janus"));
  } catch (const janus::CompileError &error) {
    invalid_import_localized =
        std::string{error.what()}.find("shared.missing") != std::string::npos &&
        error.diagnostic().source_path ==
            std::filesystem::weakly_canonical(root / "right.janus");
  }
  expect(invalid_import_localized,
         "an invalid selective import points to its importing module");

  write_source(root / "cycle_a.janus", "module cycle_a\n"
                                       "import cycle_b\n");
  write_source(root / "cycle_b.janus", "module cycle_b\n"
                                       "import cycle_a\n");
  bool cycle_rejected = false;
  try {
    static_cast<void>(loader.load(root / "cycle_a.janus"));
  } catch (const janus::CompileError &error) {
    cycle_rejected =
        std::string{error.what()}.find("cyclic module import") !=
            std::string::npos &&
        error.diagnostic().source_path ==
            std::filesystem::weakly_canonical(root / "cycle_b.janus");
  }
  expect(cycle_rejected, "cyclic module imports remain detected");

  std::filesystem::remove_all(root);
  return failures == 0 ? 0 : 1;
}
