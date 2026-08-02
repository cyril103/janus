#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace janus {

struct SourceLocation {
  std::size_t offset{};
  std::uint32_t line{1};
  std::uint32_t column{1};
};

enum class DiagnosticSeverity {
  Note,
  Warning,
  Error,
};

enum class DiagnosticCode {
  Unclassified,
  LexerUnexpectedCharacter,
  ParserExpectedExpression,
  AnalyzerUnknownValue,
  AnalyzerPotentialMemoryLeak,
  AnalyzerOwningValueOverwritten,
  AnalyzerOwningResultDiscarded,
  ModuleNotFound,
  BackendCyclicGlobalConstant,
};

[[nodiscard]] constexpr std::string_view
diagnostic_code_name(DiagnosticCode code) noexcept {
  switch (code) {
  case DiagnosticCode::Unclassified:
    return "J0000";
  case DiagnosticCode::LexerUnexpectedCharacter:
    return "JLEX0001";
  case DiagnosticCode::ParserExpectedExpression:
    return "JPAR0001";
  case DiagnosticCode::AnalyzerUnknownValue:
    return "JANA0001";
  case DiagnosticCode::AnalyzerPotentialMemoryLeak:
    return "JANA0002";
  case DiagnosticCode::AnalyzerOwningValueOverwritten:
    return "JANA0003";
  case DiagnosticCode::AnalyzerOwningResultDiscarded:
    return "JANA0004";
  case DiagnosticCode::ModuleNotFound:
    return "JMOD0001";
  case DiagnosticCode::BackendCyclicGlobalConstant:
    return "JBCK0001";
  }
  return "J0000";
}

struct DiagnosticLocation {
  SourceLocation location;
  std::string label;
};

struct SourceRange {
  SourceLocation start;
  SourceLocation end;
};

struct DiagnosticSuggestion {
  std::string message;
  SourceRange range;
  std::string replacement;
};

struct Diagnostic {
  DiagnosticSeverity severity{DiagnosticSeverity::Error};
  DiagnosticCode code{DiagnosticCode::Unclassified};
  std::string message;
  SourceLocation primary_location;
  std::vector<std::string> notes;
  std::vector<DiagnosticLocation> secondary_locations;
  std::vector<DiagnosticSuggestion> suggestions;
};

class CompileError final : public std::runtime_error {
public:
  CompileError(SourceLocation location, std::string message)
      : CompileError{Diagnostic{DiagnosticSeverity::Error,
                                DiagnosticCode::Unclassified,
                                std::move(message),
                                location,
                                {},
                                {},
                                {}}} {}

  CompileError(DiagnosticCode code, SourceLocation location,
               std::string message)
      : CompileError{Diagnostic{DiagnosticSeverity::Error,
                                code,
                                std::move(message),
                                location,
                                {},
                                {},
                                {}}} {}

  explicit CompileError(Diagnostic diagnostic)
      : std::runtime_error{diagnostic.message},
        diagnostics_{std::move(diagnostic)} {}

  explicit CompileError(std::vector<Diagnostic> diagnostics)
      : std::runtime_error{diagnostics.empty() ? "compilation failed"
                                               : diagnostics.front().message},
        diagnostics_{std::move(diagnostics)} {
    if (diagnostics_.empty())
      diagnostics_.push_back(Diagnostic{DiagnosticSeverity::Error,
                                        DiagnosticCode::Unclassified,
                                        "compilation failed",
                                        SourceLocation{},
                                        {},
                                        {},
                                        {}});
  }

  [[nodiscard]] SourceLocation location() const noexcept {
    return diagnostic().primary_location;
  }

  [[nodiscard]] const Diagnostic &diagnostic() const noexcept {
    return diagnostics_.front();
  }

  [[nodiscard]] const std::vector<Diagnostic> &diagnostics() const noexcept {
    return diagnostics_;
  }

private:
  std::vector<Diagnostic> diagnostics_;
};

} // namespace janus
