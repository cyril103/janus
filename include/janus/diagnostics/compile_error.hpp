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

struct Diagnostic {
  DiagnosticSeverity severity{DiagnosticSeverity::Error};
  DiagnosticCode code{DiagnosticCode::Unclassified};
  std::string message;
  SourceLocation primary_location;
  std::vector<std::string> notes;
  std::vector<DiagnosticLocation> secondary_locations;
};

class CompileError final : public std::runtime_error {
public:
  CompileError(SourceLocation location, std::string message)
      : CompileError{Diagnostic{DiagnosticSeverity::Error,
                                DiagnosticCode::Unclassified,
                                std::move(message),
                                location,
                                {},
                                {}}} {}

  CompileError(DiagnosticCode code, SourceLocation location,
               std::string message)
      : CompileError{Diagnostic{DiagnosticSeverity::Error,
                                code,
                                std::move(message),
                                location,
                                {},
                                {}}} {}

  explicit CompileError(Diagnostic diagnostic)
      : std::runtime_error{diagnostic.message},
        diagnostic_{std::move(diagnostic)} {}

  [[nodiscard]] SourceLocation location() const noexcept {
    return diagnostic_.primary_location;
  }

  [[nodiscard]] const Diagnostic &diagnostic() const noexcept {
    return diagnostic_;
  }

private:
  Diagnostic diagnostic_;
};

} // namespace janus
