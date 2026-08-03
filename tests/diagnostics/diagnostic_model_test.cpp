#include "janus/diagnostics/compile_error.hpp"

#include "../support/require.hpp"

#include <array>
#include <string>
#include <string_view>
#include <vector>

int main(int argc, char **argv) {
  if (argc == 2 && std::string_view{argv[1]} == "--verify-require-failure")
    JANUS_REQUIRE(false);

  using janus::DiagnosticCode;

  constexpr std::array codes{
      DiagnosticCode::LexerUnexpectedCharacter,
      DiagnosticCode::ParserExpectedExpression,
      DiagnosticCode::AnalyzerUnknownValue,
      DiagnosticCode::ModuleNotFound,
      DiagnosticCode::BackendCyclicGlobalConstant,
  };
  for (std::size_t left = 0; left < codes.size(); ++left) {
    JANUS_REQUIRE(janus::diagnostic_code_name(codes[left]) != "J0000");
    for (std::size_t right = left + 1; right < codes.size(); ++right)
      JANUS_REQUIRE(janus::diagnostic_code_name(codes[left]) !=
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
      std::vector<janus::DiagnosticSuggestion>{
          {"replace the name",
           {janus::SourceLocation{12, 3, 7}, janus::SourceLocation{18, 3, 13}},
           "value"}},
  };
  const janus::CompileError error{std::move(diagnostic)};

  JANUS_REQUIRE(std::string_view{error.what()} == "unknown value 'answer'");
  JANUS_REQUIRE(error.location().line == 3);
  JANUS_REQUIRE(error.location().column == 7);
  JANUS_REQUIRE(error.diagnostic().severity ==
                janus::DiagnosticSeverity::Error);
  JANUS_REQUIRE(error.diagnostic().code ==
                DiagnosticCode::AnalyzerUnknownValue);
  JANUS_REQUIRE(error.diagnostic().notes.size() == 1);
  JANUS_REQUIRE(error.diagnostic().secondary_locations.size() == 1);
  JANUS_REQUIRE(error.diagnostic().secondary_locations.front().location.line ==
                1);
  JANUS_REQUIRE(error.diagnostic().secondary_locations.front().label ==
                "related declaration");
  JANUS_REQUIRE(error.diagnostic().suggestions.size() == 1);
  JANUS_REQUIRE(error.diagnostic().suggestions.front().replacement == "value");
}
