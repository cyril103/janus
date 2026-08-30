#include "janus/frontend/parser.hpp"
#include "janus/semantic/analyzer.hpp"

#include <algorithm>
#include <iostream>
#include <string>

int main() {
  janus::frontend::Parser parser{R"(
def replacement(value : int) : int { return value }
/// Kept for migration.
/// @deprecated use [[replacement]]
def legacy(value : int) : int { return replacement(value) }
class Service() {
    def replacement() : int { return 2 }
    /// Kept for migration.
    /// @deprecated use [[Service.replacement]]
    def legacy() : int { return 1 }
}
def main() : int {
    val first : int = legacy(40)
    val service : Service = new Service()
    val second : int = service.legacy()
    delete service
    return first + second - 41
}
)"};
  const janus::semantic::AnalysisResult result =
      janus::semantic::Analyzer{}.analyze(parser.parse_program());
  const auto warnings = std::count_if(
      result.diagnostics.begin(), result.diagnostics.end(),
      [](const janus::Diagnostic &diagnostic) {
        return diagnostic.code ==
               janus::DiagnosticCode::AnalyzerDeprecatedUse;
      });
  if (warnings != 2) {
    std::cerr << "expected two deprecation warnings, got " << warnings << '\n';
    return 1;
  }
  const bool replacement_named = std::all_of(
      result.diagnostics.begin(), result.diagnostics.end(),
      [](const janus::Diagnostic &diagnostic) {
        return diagnostic.code !=
                   janus::DiagnosticCode::AnalyzerDeprecatedUse ||
               (!diagnostic.notes.empty() &&
                diagnostic.notes.front().find("replacement") !=
                    std::string::npos);
      });
  if (!replacement_named) {
    std::cerr << "deprecation warnings must name their replacements\n";
    return 1;
  }
  return 0;
}
