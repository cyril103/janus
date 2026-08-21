#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
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
  AnalyzerMustUseResult,
  AnalyzerOwningFieldOverwritten,
  AnalyzerIncompleteDestructor,
  AnalyzerUnsafeReallocation,
  AnalyzerUnprotectedEarlyExit,
  AnalyzerLoopAllocation,
  AnalyzerEscapingOwningCapture,
  AnalyzerAmbiguousPointerCast,
  AnalyzerLossyNumericCast,
  AnalyzerUnusedValue,
  AnalyzerOwningBufferFreedWithoutCleanup,
  AnalyzerOwningPointerElementOverwritten,
  AnalyzerOwningBufferReallocated,
  AnalyzerBorrowedTemporaryOwner,
  AnalyzerUnprotectedPanic,
  AnalyzerUnannotatedExternOwnership,
  AnalyzerPotentialOwnershipCycle,
  AnalyzerUnannotatedExternReturn,
  AnalyzerInvalidArrayLiteral,
  AnalyzerBorrowConflict,
  AnalyzerBorrowInvalidation,
  AnalyzerBorrowEscape,
  AnalyzerInvalidBorrowAccess,
  AnalyzerInvalidBorrowSource,
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
  case DiagnosticCode::AnalyzerMustUseResult:
    return "JANA0005";
  case DiagnosticCode::AnalyzerOwningFieldOverwritten:
    return "JANA0006";
  case DiagnosticCode::AnalyzerIncompleteDestructor:
    return "JANA0007";
  case DiagnosticCode::AnalyzerUnsafeReallocation:
    return "JANA0008";
  case DiagnosticCode::AnalyzerUnprotectedEarlyExit:
    return "JANA0009";
  case DiagnosticCode::AnalyzerLoopAllocation:
    return "JANA0010";
  case DiagnosticCode::AnalyzerEscapingOwningCapture:
    return "JANA0011";
  case DiagnosticCode::AnalyzerAmbiguousPointerCast:
    return "JANA0012";
  case DiagnosticCode::AnalyzerLossyNumericCast:
    return "JANA0013";
  case DiagnosticCode::AnalyzerUnusedValue:
    return "JANA0014";
  case DiagnosticCode::AnalyzerOwningBufferFreedWithoutCleanup:
    return "JANA0015";
  case DiagnosticCode::AnalyzerOwningPointerElementOverwritten:
    return "JANA0016";
  case DiagnosticCode::AnalyzerOwningBufferReallocated:
    return "JANA0017";
  case DiagnosticCode::AnalyzerBorrowedTemporaryOwner:
    return "JANA0018";
  case DiagnosticCode::AnalyzerUnprotectedPanic:
    return "JANA0019";
  case DiagnosticCode::AnalyzerUnannotatedExternOwnership:
    return "JANA0020";
  case DiagnosticCode::AnalyzerPotentialOwnershipCycle:
    return "JANA0021";
  case DiagnosticCode::AnalyzerUnannotatedExternReturn:
    return "JANA0022";
  case DiagnosticCode::AnalyzerInvalidArrayLiteral:
    return "JANA0023";
  case DiagnosticCode::AnalyzerBorrowConflict:
    return "JANA0024";
  case DiagnosticCode::AnalyzerBorrowInvalidation:
    return "JANA0025";
  case DiagnosticCode::AnalyzerBorrowEscape:
    return "JANA0026";
  case DiagnosticCode::AnalyzerInvalidBorrowAccess:
    return "JANA0027";
  case DiagnosticCode::AnalyzerInvalidBorrowSource:
    return "JANA0028";
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
  Diagnostic() = default;
  Diagnostic(DiagnosticSeverity diagnostic_severity,
             DiagnosticCode diagnostic_code, std::string diagnostic_message,
             SourceLocation location, std::vector<std::string> diagnostic_notes,
             std::vector<DiagnosticLocation> locations,
             std::vector<DiagnosticSuggestion> diagnostic_suggestions,
             std::filesystem::path path = {})
      : severity{diagnostic_severity}, code{diagnostic_code},
        message{std::move(diagnostic_message)}, primary_location{location},
        notes{std::move(diagnostic_notes)},
        secondary_locations{std::move(locations)},
        suggestions{std::move(diagnostic_suggestions)},
        source_path{std::move(path)} {}

  DiagnosticSeverity severity{DiagnosticSeverity::Error};
  DiagnosticCode code{DiagnosticCode::Unclassified};
  std::string message;
  SourceLocation primary_location;
  std::vector<std::string> notes;
  std::vector<DiagnosticLocation> secondary_locations;
  std::vector<DiagnosticSuggestion> suggestions;
  std::filesystem::path source_path;
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
