#include "janus/ast/ast.hpp"
#include "janus/backend/llvm/ir_generator.hpp"
#include "janus/frontend/module_loader.hpp"
#include "janus/frontend/parser.hpp"
#include "janus/semantic/analyzer.hpp"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>
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

bool warns_with_code(const janus::semantic::AnalysisResult &analysis,
                     janus::DiagnosticCode code,
                     std::string_view message_fragment) {
  return std::any_of(
      analysis.diagnostics.begin(), analysis.diagnostics.end(),
      [&](const janus::Diagnostic &diagnostic) {
        return diagnostic.severity == janus::DiagnosticSeverity::Warning &&
               diagnostic.code == code &&
               diagnostic.message.find(message_fragment) != std::string::npos;
      });
}
} // namespace

int main() {
  janus::frontend::Parser parser{R"(
def main() : int {
    val values : Array[int] = new Array[int](usize(1))
    val first : int = values[usize(0)]
    values[usize(0)] = 41
    values[usize(0)] += 1
    return first
}
)"};
  const janus::ast::Program program = parser.parse_program();
  const auto &body = program.functions.front().body;
  const auto &read = std::get<janus::ast::ValueDeclaration>(body[1]);
  expect(read.initializer.has_value() &&
             std::holds_alternative<janus::ast::IndexExpression>(
                 read.initializer->value),
         "parser represents indexed reads explicitly");
  const auto &replacement =
      std::get<janus::ast::AssignmentStatement>(body[2]);
  expect(replacement.index_target != nullptr &&
             replacement.operation == janus::ast::AssignmentOperator::Assign,
         "parser represents an indexed assignment target");
  const auto &compound = std::get<janus::ast::AssignmentStatement>(body[3]);
  expect(compound.index_target != nullptr &&
             compound.operation == janus::ast::AssignmentOperator::Add,
         "parser preserves indexed compound assignment");

  janus::frontend::ModuleLoader loader{
      {std::filesystem::path{JANUS_STDLIB_DIR}}};
  const janus::ast::Program loaded =
      loader.load(std::filesystem::path{JANUS_INDEXING_ENTRY});
  const auto main_function = std::find_if(
      loaded.functions.begin(), loaded.functions.end(),
      [](const janus::ast::FunctionDeclaration &candidate) {
        return candidate.name == "main" && !candidate.module_name.has_value();
      });
  janus::semantic::Analyzer analyzer;
  const janus::semantic::AnalysisResult analysis = analyzer.analyze(loaded);
  expect(std::none_of(
             analysis.diagnostics.begin(), analysis.diagnostics.end(),
             [](const janus::Diagnostic &diagnostic) {
               return diagnostic.code ==
                      janus::DiagnosticCode::AnalyzerBorrowedTemporaryOwner;
             }),
         "borrow and borrow var indexed containers do not warn as abandoned "
         "owners");
  const janus::ast::IndexExpression *loaded_read = nullptr;
  const janus::ast::AssignmentStatement *loaded_replace_statement = nullptr;
  const janus::ast::AssignmentStatement *loaded_compound_statement = nullptr;
  for (const janus::ast::Statement &statement : main_function->body) {
    if (const auto *expression_statement =
            std::get_if<janus::ast::ExpressionStatement>(&statement))
      if (const auto *call = std::get_if<janus::ast::CallExpression>(
              &expression_statement->expression.value);
          call != nullptr && !call->arguments.empty())
        if (const auto *index = std::get_if<janus::ast::IndexExpression>(
                &call->arguments.front()->value))
          loaded_read = index;
    if (const auto *assignment =
            std::get_if<janus::ast::AssignmentStatement>(&statement);
        assignment != nullptr && assignment->index_target != nullptr) {
      if (assignment->operation == janus::ast::AssignmentOperator::Assign &&
          loaded_replace_statement == nullptr)
        loaded_replace_statement = assignment;
      if (assignment->operation != janus::ast::AssignmentOperator::Assign &&
          loaded_compound_statement == nullptr)
        loaded_compound_statement = assignment;
    }
  }
  expect(loaded_read != nullptr && loaded_replace_statement != nullptr &&
             loaded_compound_statement != nullptr,
         "runtime fixture exposes read, replacement and compound index nodes");
  const auto &loaded_replace = *loaded_replace_statement->index_target;
  const auto &loaded_compound = *loaded_compound_statement->index_target;
  const auto &read_capability = analysis.indexed_capabilities.at(loaded_read);
  const auto &replace_capability =
      analysis.indexed_capabilities.at(&loaded_replace);
  const auto &compound_capability =
      analysis.indexed_capabilities.at(&loaded_compound);
  const auto canonical_array = std::find_if(
      loaded.classes.begin(), loaded.classes.end(),
      [](const janus::ast::ClassDeclaration &candidate) {
        return candidate.name == "Array" && candidate.module_name == "std.array";
      });
  const auto canonical_get = std::find_if(
      canonical_array->methods.begin(), canonical_array->methods.end(),
      [](const janus::ast::FunctionDeclaration &candidate) {
        return candidate.name == "get";
      });
  const auto canonical_set = std::find_if(
      canonical_array->methods.begin(), canonical_array->methods.end(),
      [](const janus::ast::FunctionDeclaration &candidate) {
        return candidate.name == "set";
      });
  expect(read_capability.read == &*canonical_get &&
             canonical_get->type_constraints.size() == 1 &&
             canonical_get->type_constraints.front().parameter == "T" &&
             canonical_get->type_constraints.front().trait.name == "Copy",
         "analysis records the canonical generic get T:Copy declaration");
  expect(read_capability.replace == nullptr &&
             replace_capability.read == nullptr &&
             replace_capability.replace == &*canonical_set &&
             compound_capability.read == read_capability.read &&
             compound_capability.replace == replace_capability.replace,
         "analysis records exact read and replacement capabilities by use");

  const auto grid = std::find_if(
      loaded.classes.begin(), loaded.classes.end(),
      [](const janus::ast::ClassDeclaration &candidate) {
        return candidate.name == "Grid" && !candidate.module_name.has_value();
      });
  const auto grid_get = std::find_if(
      grid->methods.begin(), grid->methods.end(),
      [](const janus::ast::FunctionDeclaration &candidate) {
        return candidate.name == "get";
      });
  const auto grid_set = std::find_if(
      grid->methods.begin(), grid->methods.end(),
      [](const janus::ast::FunctionDeclaration &candidate) {
        return candidate.name == "set";
      });
  expect(std::any_of(
             analysis.indexed_capabilities.begin(),
             analysis.indexed_capabilities.end(), [&](const auto &entry) {
               return entry.second.read == &*grid_get &&
                      entry.second.index_type.is_concrete() &&
                      entry.second.index_type.concrete->kind() ==
                          janus::TypeKind::Int;
             }),
         "a user Index implementation supplies its exact method and key type");
  expect(std::any_of(
             analysis.indexed_capabilities.begin(),
             analysis.indexed_capabilities.end(), [&](const auto &entry) {
               return entry.second.replace == &*grid_set;
             }),
         "a user IndexMut implementation supplies its exact replacement");

  llvm::LLVMContext context;
  janus::backend::llvm::IrGenerator generator{context};
  const std::unique_ptr<llvm::Module> indexed_module =
      generator.generate(loaded, "indexing_capabilities");
  std::string indexed_ir;
  llvm::raw_string_ostream indexed_output{indexed_ir};
  indexed_module->print(indexed_output, nullptr);
  indexed_output.flush();
  expect(indexed_ir.find("call i32 @Grid__int__get") != std::string::npos &&
             indexed_ir.find("call void @Grid__int__set") !=
                 std::string::npos,
         "protocol indexing lowers to direct user-method calls without "
         "dispatch overhead");

  const std::filesystem::path lambda_entry =
      std::filesystem::temp_directory_path() / "janus-indexing-lambda.janus";
  const janus::ast::Program lambda_program = loader.load(lambda_entry, R"(
import std.array as arrays
def main() : int {
    val values : arrays.Array[int] = new arrays.Array[int](usize(1))
    defer delete values
    values.push(42)
    val read : () => int = () => values[usize(0)]
    defer delete read
    return read() - 42
}
)");
  llvm::LLVMContext lambda_context;
  janus::backend::llvm::IrGenerator lambda_generator{lambda_context};
  static_cast<void>(
      lambda_generator.generate(lambda_program, "indexing_lambda_capture"));
  expect(true, "lambda dependency analysis visits indexed operands");

  const std::filesystem::path temporary_entry =
      std::filesystem::temp_directory_path() /
      "janus-indexing-temporary-owner.janus";
  const janus::ast::Program temporary_program = loader.load(temporary_entry, R"(
import std.array as arrays

def makeValues() : arrays.Array[int] {
    val values : arrays.Array[int] = new arrays.Array[int](usize(1))
    values.push(7)
    return move values
}

def abandonedRead() : int {
    return makeValues()[usize(0)]
}

def abandonedWrite() : Unit {
    makeValues()[usize(0)] = 9
}

def main() : int {
    return 0
}
)");
  const auto temporary_analysis =
      janus::semantic::Analyzer{}.analyze(temporary_program);
  expect(warns_with_code(
             temporary_analysis,
             janus::DiagnosticCode::AnalyzerBorrowedTemporaryOwner,
             "indexed read"),
         "indexed reads diagnose an abandoned temporary owner");
  expect(warns_with_code(
             temporary_analysis,
             janus::DiagnosticCode::AnalyzerBorrowedTemporaryOwner,
             "indexed replacement"),
         "indexed replacements diagnose an abandoned temporary owner");

  const std::filesystem::path homonym_entry =
      std::filesystem::temp_directory_path() /
      "janus-indexing-trait-homonym.janus";
  const janus::ast::Program homonym_program = loader.load(homonym_entry, R"(
import std.index as canonical

trait Index[Key] {
    type Output
    borrow def get(key : Key) : Output where Output <: Copy
}

class Pretender[T](val value : T) extends Index[int] {
    type Output = T
    borrow def get(key : int) : T where T <: Copy { return value }
}

def main() : int {
    val value : Pretender[int] = new Pretender[int](42)
    defer delete value
    return value[0]
}
)");
  try {
    static_cast<void>(janus::semantic::Analyzer{}.analyze(homonym_program));
    expect(false, "a short-name Index homonym is not a canonical capability");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(
               "does not provide canonical indexed read capability") !=
               std::string_view::npos,
           "a short-name Index homonym is rejected by semantic identity");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }
  std::cout << "indexed syntax has explicit read and place nodes\n";
  return 0;
}
