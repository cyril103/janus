#include "janus/semantic/compilation_session.hpp"

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
      ("janus-compilation-session-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  write_source(root / "values.janus",
               "module values\nconst answer : int = 1\n");
  write_source(root / "main.janus",
               "import values\ndef main() : int { return values.answer }\n");

  janus::semantic::CompilationSession session{
      {root}, {.require_entry_point = false, .target = {}}, true};
  session.set_source_override(root / "values.janus",
                              "module values\nconst answer : int = 42\n");
  const janus::semantic::AnalyzedProgram result =
      session.analyze(root / "main.janus");
  expect(result.program.functions.size() == 1,
         "session loads the complete module graph");
  const auto value = result.analysis.global_constant_values.find("values.answer");
  expect(value != result.analysis.global_constant_values.end(),
         "session returns semantic data anchored in its owned AST");
  if (value != result.analysis.global_constant_values.end())
    expect(std::get<std::uint64_t>(value->second.data) == 42,
           "open-document overlays are analyzed instead of disk contents");
  expect(result.timings.loading.count() >= 0 &&
             result.timings.parsing.count() >= 0 &&
             result.timings.analysis.count() >= 0,
         "session reports every frontend timing phase");

  std::filesystem::remove_all(root);
  return failures == 0 ? 0 : 1;
}
