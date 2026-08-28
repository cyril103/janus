#include "janus/backend/llvm/ir_generator.hpp"
#include "janus/diagnostics/compile_error.hpp"
#include "janus/frontend/parser.hpp"
#include "janus/semantic/analyzer.hpp"

#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/raw_ostream.h>

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

void expect_compile_error(std::string_view source,
                          std::string_view expected_message) {
  try {
    janus::frontend::Parser parser{source};
    const janus::ast::Program program = parser.parse_program();
    janus::semantic::Analyzer analyzer;
    static_cast<void>(analyzer.analyze(program));
    expect(false, "invalid mutable-variable source must fail");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(expected_message) !=
               std::string_view::npos,
           "mutable-variable error contains the expected explanation");
  }
}

} // namespace

int main() {
  janus::frontend::Parser compound_parser{R"(
def rhs() : int { return 1 }
def unsignedBits(value : uint) : uint {
    var bits : uint = value
    bits &= uint(255)
    bits |= uint(256)
    bits ^= uint(3)
    bits >>= usize(1)
    return bits
}
def main() : int {
    var value : int = 64
    value += 2
    value -= 1
    value *= 3
    value /= 5
    value %= 7
    value &= 15
    value |= 16
    value ^= 3
    value <<= 2
    value >>= 1
    value += rhs()
    return value
}
)"};
  const janus::ast::Program compound_program = compound_parser.parse_program();
  const auto &compound_body = compound_program.functions.back().body;
  const janus::ast::AssignmentOperator expected_operators[] = {
      janus::ast::AssignmentOperator::Add,
      janus::ast::AssignmentOperator::Subtract,
      janus::ast::AssignmentOperator::Multiply,
      janus::ast::AssignmentOperator::Divide,
      janus::ast::AssignmentOperator::Remainder,
      janus::ast::AssignmentOperator::BitwiseAnd,
      janus::ast::AssignmentOperator::BitwiseOr,
      janus::ast::AssignmentOperator::BitwiseXor,
      janus::ast::AssignmentOperator::ShiftLeft,
      janus::ast::AssignmentOperator::ShiftRight,
  };
  for (std::size_t index = 0; index < std::size(expected_operators); ++index) {
    const auto *assignment =
        std::get_if<janus::ast::AssignmentStatement>(&compound_body[index + 1]);
    expect(assignment != nullptr &&
               assignment->operation == expected_operators[index],
           "parser preserves each compound assignment operator");
  }
  janus::semantic::Analyzer compound_analyzer;
  static_cast<void>(compound_analyzer.analyze(compound_program));
  llvm::LLVMContext compound_context;
  janus::backend::llvm::IrGenerator compound_generator{compound_context};
  const auto compound_module =
      compound_generator.generate(compound_program, "compound_assignment");
  std::string compound_ir;
  llvm::raw_string_ostream compound_output{compound_ir};
  compound_module->print(compound_output, nullptr);
  compound_output.flush();
  expect(compound_ir.find("add i32") != std::string::npos &&
             compound_ir.find("shl i32") != std::string::npos &&
             compound_ir.find("ashr i32") != std::string::npos,
         "compound assignments reuse integer binary lowering");
  const std::string rhs_call = "call i32 @rhs()";
  const std::size_t rhs_position = compound_ir.find(rhs_call);
  expect(rhs_position != std::string::npos &&
             compound_ir.find(rhs_call, rhs_position + 1) == std::string::npos,
         "a compound-assignment RHS is emitted exactly once");

  janus::frontend::Parser inferred_parser{R"(
def main() : int {
    var fixed = 5
    fixed = 6
    return fixed
}
)"};
  const janus::ast::Program inferred_program = inferred_parser.parse_program();
  janus::semantic::Analyzer inferred_analyzer;
  const auto inferred_analysis = inferred_analyzer.analyze(inferred_program);
  expect(inferred_analysis.functions.at("main").at("fixed").type.name() ==
             "int",
         "var fixes its inferred type at declaration");

  constexpr std::string_view source = R"(
def main() : int {
    var x : int = 5
    x = 6 // reassignment is allowed
    var y : int
    y = x
    return y
}
)";

  janus::frontend::Parser parser{source};
  const janus::ast::Program program = parser.parse_program();
  const auto &body = program.functions.front().body;
  expect(body.size() == 5, "main contains declarations and assignments");

  const auto &x_declaration = std::get<janus::ast::ValueDeclaration>(body[0]);
  expect(x_declaration.is_mutable, "var x is mutable");
  expect(x_declaration.initializer.has_value(), "x has an initializer");

  const auto &y_declaration = std::get<janus::ast::ValueDeclaration>(body[2]);
  expect(y_declaration.is_mutable, "var y is mutable");
  expect(!y_declaration.initializer.has_value(),
         "y is declared without an initializer");

  janus::semantic::Analyzer analyzer;
  const janus::semantic::AnalysisResult analysis = analyzer.analyze(program);
  const janus::semantic::SymbolTable &symbols = analysis.functions.at("main");
  expect(symbols.at("x").is_mutable, "x is mutable in the symbol table");
  expect(symbols.at("x").is_initialized, "x is initialized");
  expect(symbols.at("y").is_initialized, "assignment marks y as initialized");

  llvm::LLVMContext context;
  janus::backend::llvm::IrGenerator generator{context};
  const std::unique_ptr<llvm::Module> module =
      generator.generate(program, "mutable_variables");
  std::string ir;
  llvm::raw_string_ostream output{ir};
  module->print(output, nullptr);
  output.flush();

  expect(ir.find("store i32 5, ptr %x") != std::string::npos,
         "x receives its initial value");
  expect(ir.find("store i32 6, ptr %x") != std::string::npos,
         "x receives its reassigned value");
  expect(ir.find("%y = alloca i32") != std::string::npos,
         "y has storage without a default value");
  expect(ir.find("store i32 %x.value, ptr %y") != std::string::npos,
         "assignment stores x in y");

  expect_compile_error("def main() : int { var y : int return y }",
                       "variable 'y' is used before initialization");
  expect_compile_error("def main() : int { val x : int = 5 x = 6 return x }",
                       "cannot assign to immutable value 'x'");
  expect_compile_error("def main() : int { val x : int = 5 x += 1 return x }",
                       "cannot assign to immutable value 'x'");
  expect_compile_error("def main() : int { var x : bool = true x += false return 0 }",
                       "arithmetic operators require operands");
  expect_compile_error("def main() : int { var x : int = 1 x += true return x }",
                       "binary operator operands must have the same type");
  expect_compile_error("def main() : int { var x : int x += 1 return x }",
                       "variable 'x' is used before initialization");
  expect_compile_error("def main() : int { var x : bool = 1 return 0 }",
                       "expression of type 'int'");
  expect_compile_error("def main() : int { var x : bool x = 1 return 0 }",
                       "expression of type 'int'");
  expect_compile_error(
      "def main() : int { var fixed = 1 fixed = false return fixed }",
      "expression of type 'bool'");
  expect_compile_error("def main() : int { var missing return 0 }",
                       "inferred local 'missing' requires an initializer");
  expect_compile_error("def main() : int { missing = 1 return 0 }",
                       "unknown value 'missing'");
  expect_compile_error("def main() : int { val x : int return 0 }",
                       "expected '='");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }

  std::cout << "var supports reassignment and definite initialization\n";
  return 0;
}
