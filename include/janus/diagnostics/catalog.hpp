#pragma once

#include "janus/diagnostics/compile_error.hpp"

#include <array>
#include <optional>
#include <string_view>

namespace janus::diagnostics {

struct DiagnosticExplanation {
  DiagnosticCode code;
  std::string_view title;
  std::string_view explanation;
  std::string_view action;
};

inline constexpr std::array all_diagnostic_codes{
    DiagnosticCode::GeneralInternalFailure,
    DiagnosticCode::LexerLegacy,
    DiagnosticCode::LexerUnexpectedCharacter,
    DiagnosticCode::ParserLegacy,
    DiagnosticCode::ParserExpectedExpression,
    DiagnosticCode::ModuleLegacy,
    DiagnosticCode::AnalyzerLegacy,
    DiagnosticCode::AnalyzerUnknownValue,
    DiagnosticCode::AnalyzerPotentialMemoryLeak,
    DiagnosticCode::AnalyzerOwningValueOverwritten,
    DiagnosticCode::AnalyzerOwningResultDiscarded,
    DiagnosticCode::AnalyzerMustUseResult,
    DiagnosticCode::AnalyzerOwningFieldOverwritten,
    DiagnosticCode::AnalyzerIncompleteDestructor,
    DiagnosticCode::AnalyzerUnsafeReallocation,
    DiagnosticCode::AnalyzerUnprotectedEarlyExit,
    DiagnosticCode::AnalyzerLoopAllocation,
    DiagnosticCode::AnalyzerEscapingOwningCapture,
    DiagnosticCode::AnalyzerAmbiguousPointerCast,
    DiagnosticCode::AnalyzerLossyNumericCast,
    DiagnosticCode::AnalyzerUnusedValue,
    DiagnosticCode::AnalyzerOwningBufferFreedWithoutCleanup,
    DiagnosticCode::AnalyzerOwningPointerElementOverwritten,
    DiagnosticCode::AnalyzerOwningBufferReallocated,
    DiagnosticCode::AnalyzerBorrowedTemporaryOwner,
    DiagnosticCode::AnalyzerUnprotectedPanic,
    DiagnosticCode::AnalyzerUnannotatedExternOwnership,
    DiagnosticCode::AnalyzerPotentialOwnershipCycle,
    DiagnosticCode::AnalyzerUnannotatedExternReturn,
    DiagnosticCode::AnalyzerInvalidArrayLiteral,
    DiagnosticCode::AnalyzerBorrowConflict,
    DiagnosticCode::AnalyzerBorrowInvalidation,
    DiagnosticCode::AnalyzerBorrowEscape,
    DiagnosticCode::AnalyzerInvalidBorrowAccess,
    DiagnosticCode::AnalyzerInvalidBorrowSource,
    DiagnosticCode::AnalyzerTailrecRequired,
    DiagnosticCode::AnalyzerInvalidTailrec,
    DiagnosticCode::AnalyzerNonTerminalTailrec,
    DiagnosticCode::AnalyzerIncompatibleTailrec,
    DiagnosticCode::AnalyzerDeprecatedUse,
    DiagnosticCode::AnalyzerHighGrowthLoop,
    DiagnosticCode::AnalyzerImplicitOwnershipTransfer,
    DiagnosticCode::ModuleNotFound,
    DiagnosticCode::ConstantLegacy,
    DiagnosticCode::BackendLegacy,
    DiagnosticCode::BackendCyclicGlobalConstant,
    DiagnosticCode::DriverLegacy,
};

[[nodiscard]] inline std::optional<DiagnosticCode>
diagnostic_code_from_name(std::string_view name) noexcept {
  for (const DiagnosticCode code : all_diagnostic_codes)
    if (diagnostic_code_name(code) == name)
      return code;
  return std::nullopt;
}

[[nodiscard]] inline DiagnosticExplanation
explain_diagnostic(DiagnosticCode code) noexcept {
  switch (code) {
  case DiagnosticCode::GeneralInternalFailure:
    return {code, "internal compiler or tool failure",
            "A tool operation failed outside a more specific compiler diagnostic.",
            "Report the command and a minimal reproducer."};
  case DiagnosticCode::LexerLegacy:
  case DiagnosticCode::ParserLegacy:
  case DiagnosticCode::ModuleLegacy:
  case DiagnosticCode::AnalyzerLegacy:
  case DiagnosticCode::ConstantLegacy:
  case DiagnosticCode::BackendLegacy:
  case DiagnosticCode::DriverLegacy:
    return {code, "stable subsystem diagnostic",
            "This error is classified by its producing compiler subsystem.",
            "Read the primary message; report a reproducer if a more precise code would help."};
  case DiagnosticCode::LexerUnexpectedCharacter:
    return {code, "unexpected source character",
            "The lexer cannot form a Janus token at this position.",
            "Remove the character or replace it with valid Janus syntax."};
  case DiagnosticCode::ParserExpectedExpression:
    return {code, "expression expected",
            "The parser reached a position where an expression is required.",
            "Complete the expression and check delimiters immediately before it."};
  case DiagnosticCode::AnalyzerUnknownValue:
    return {code, "unknown value",
            "Name resolution found no visible declaration for this value.",
            "Check spelling, imports, qualification and declaration visibility."};
  case DiagnosticCode::AnalyzerBorrowConflict:
    return {code, "conflicting borrows",
            "A mutable borrow must be exclusive and cannot overlap another access.",
            "Shorten one lexical scope or sequence the borrows."};
  case DiagnosticCode::AnalyzerBorrowInvalidation:
    return {code, "borrow invalidated",
            "An operation would move, destroy, mutate or reallocate a borrowed owner.",
            "End the borrow before the operation, or postpone the invalidating operation."};
  case DiagnosticCode::AnalyzerBorrowEscape:
    return {code, "borrow escapes its owner",
            "The borrowed value could outlive the storage from which it was created.",
            "Keep its use inside the owner's lexical scope and do not store or return it."};
  case DiagnosticCode::AnalyzerInvalidBorrowAccess:
    return {code, "operation forbidden through this borrow",
            "The requested operation requires ownership or an exclusive mutable borrow.",
            "Use a borrow-compatible method or request `borrow var` access."};
  case DiagnosticCode::AnalyzerInvalidBorrowSource:
    return {code, "invalid borrow source",
            "A borrow requires stable storage whose lifetime covers the complete use.",
            "Bind the owner to a local value before borrowing it."};
  case DiagnosticCode::AnalyzerLossyNumericCast:
    return {code, "potentially lossy numeric cast",
            "The conversion may truncate, overflow or lose precision.",
            "Choose checkedCast, saturatingCast or truncatingCast explicitly."};
  case DiagnosticCode::AnalyzerTailrecRequired:
  case DiagnosticCode::AnalyzerInvalidTailrec:
  case DiagnosticCode::AnalyzerNonTerminalTailrec:
  case DiagnosticCode::AnalyzerIncompatibleTailrec:
    return {code, "invalid tail-recursion contract",
            "The declared recursion cycle does not match the backend musttail contract.",
            "Make every cycle edge terminal and signature-compatible, or remove tailrec."};
  case DiagnosticCode::AnalyzerDeprecatedUse:
    return {code, "deprecated API use",
            "The referenced declaration is retained only for source migration.",
            "Use the replacement named in the diagnostic before the API is removed."};
  case DiagnosticCode::AnalyzerHighGrowthLoop:
    return {code, "high-growth loop",
            "The loop update may overflow or consume excessive execution time.",
            "Add an explicit bound, use a safe numeric type, or enforce a time budget."};
  case DiagnosticCode::AnalyzerImplicitOwnershipTransfer:
    return {code, "implicit ownership transfer",
            "A non-Copy value cannot cross an owning value boundary by "
            "implicit copy.",
            "Use `move value` to transfer ownership, or borrow the value "
            "explicitly."};
  case DiagnosticCode::ModuleNotFound:
    return {code, "module not found",
            "Module resolution exhausted the project, dependency and standard-library roots.",
            "Check the module name, dependency declaration and source-root layout."};
  case DiagnosticCode::BackendCyclicGlobalConstant:
    return {code, "cyclic global constant",
            "Global constant initialization contains a dependency cycle.",
            "Break the cycle or move one value behind a runtime function."};
  default:
    return {code, "ownership or semantic contract violation",
            "The program violates the stable semantic contract named by this code.",
            "Read the primary message and notes; make ownership and intent explicit."};
  }
}

} // namespace janus::diagnostics
