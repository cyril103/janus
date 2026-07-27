#include "janus/diagnostics/compile_error.hpp"

#include <array>
#include <cassert>
#include <string>
#include <string_view>
#include <vector>

int main() {
  using janus::DiagnosticCode;

  constexpr std::array codes{
      DiagnosticCode::LexerUnexpectedCharacter,
      DiagnosticCode::ParserExpectedExpression,
      DiagnosticCode::AnalyzerUnknownValue,
      DiagnosticCode::ModuleNotFound,
      DiagnosticCode::BackendCyclicGlobalConstant,
  };
  for (std::size_t left = 0; left < codes.size(); ++left) {
    assert(janus::diagnostic_code_name(codes[left]) != "J0000");
    for (std::size_t right = left + 1; right < codes.size(); ++right)
      assert(janus::diagnostic_code_name(codes[left]) !=
             janus::diagnostic_code_name(codes[right]));
  }

  janus::Diagnostic diagnostic{
      janus::DiagnosticSeverity::Error,
      DiagnosticCode::AnalyzerUnknownValue,
      "unknown value 'answer'",
      janus::SourceLocation{12, 3, 7},
      std::vector<std::string>{"declare the value before using it"},
      std::vector<janus::DiagnosticLocation>{
          {janus::SourceLocation{2, 1, 3}, "related declaration"}},
  };
  const janus::CompileError error{std::move(diagnostic)};

  assert(std::string_view{error.what()} == "unknown value 'answer'");
  assert(error.location().line == 3);
  assert(error.location().column == 7);
  assert(error.diagnostic().severity == janus::DiagnosticSeverity::Error);
  assert(error.diagnostic().code == DiagnosticCode::AnalyzerUnknownValue);
  assert(error.diagnostic().notes.size() == 1);
  assert(error.diagnostic().secondary_locations.size() == 1);
  assert(error.diagnostic().secondary_locations.front().location.line == 1);
  assert(error.diagnostic().secondary_locations.front().label ==
         "related declaration");
}
