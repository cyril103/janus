#include "janus/diagnostics/compile_error.hpp"
#include "janus/frontend/module_loader.hpp"
#include "janus/frontend/parser.hpp"
#include "janus/semantic/analyzer.hpp"

#include <filesystem>
#include <iostream>
#include <string_view>

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
  }
}

void expect_compile_error(std::string_view source,
                          std::string_view expected_message) {
  try {
    janus::frontend::Parser parser{source};
    const janus::ast::Program program = parser.parse_program();
    janus::semantic::Analyzer analyzer;
    static_cast<void>(analyzer.analyze(program));
    expect(false, "invalid global declaration must fail");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(expected_message) !=
               std::string_view::npos,
           "global diagnostic contains the expected explanation");
  }
}

} // namespace

int main() {
  janus::frontend::Parser parser{R"(
val answer : int = 42
var requests : int = 0
private val enabled : bool = !false
val greeting : string = "Bonjour"

def main() : int {
    requests = 2
    val answer : int = 7
    return answer
}
)"};
  const janus::ast::Program program = parser.parse_program();
  janus::semantic::Analyzer analyzer;
  const janus::semantic::AnalysisResult analysis = analyzer.analyze(program);

  expect(analysis.globals.size() == 4,
         "semantic analysis exposes four global symbols");
  expect(!analysis.globals.at("answer").is_mutable,
         "global val is immutable");
  expect(analysis.globals.at("requests").is_mutable,
         "global var is mutable");
  expect(analysis.globals.at("requests").type.concrete->kind() ==
             janus::TypeKind::Int,
         "global var keeps its declared type");
  expect(analysis.functions.at("main").at("answer").type.concrete->kind() ==
             janus::TypeKind::Int,
         "a local value may shadow a global");

  expect_compile_error(
      "var pending : int\ndef main() : int { return 0 }",
      "global variable 'pending' requires an initializer");
  expect_compile_error(
      "val dynamic : int = compute()\ndef compute() : int { return 1 }\n"
      "def main() : int { return dynamic }",
      "global initializer must be a compile-time literal");
  expect_compile_error(
      "val wrong : bool = 1\ndef main() : int { return 0 }",
      "cannot initialize global value 'wrong'");
  expect_compile_error(
      "val unit : Unit = println(\"x\")\ndef main() : int { return 0 }",
      "must use a statically initialized built-in value type");
  expect_compile_error(
      "val answer : int = 1\nval answer : int = 2\n"
      "def main() : int { return answer }",
      "global value 'answer' is already declared");
  expect_compile_error(
      "val answer : int = 1\n"
      "def main() : int { answer = 2 return answer }",
      "cannot assign to immutable global value 'answer'");

  janus::frontend::ModuleLoader loader;
  const janus::ast::Program imported_program =
      loader.load(std::filesystem::path{JANUS_GLOBALS_ENTRY});
  static_cast<void>(analyzer.analyze(imported_program));

  try {
    const janus::ast::Program private_access_program =
        loader.load(std::filesystem::path{JANUS_GLOBALS_PRIVATE_ACCESS});
    static_cast<void>(analyzer.analyze(private_access_program));
    expect(false, "an imported private global must not be visible");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find("unknown value 'secret'") !=
               std::string_view::npos,
           "private global access is rejected outside its module");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }

  std::cout << "global values are validated and resolved semantically\n";
  return 0;
}
