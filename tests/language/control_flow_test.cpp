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
    expect(false, "invalid control-flow source must fail");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(expected_message) !=
               std::string_view::npos,
           "control-flow error contains the expected explanation");
  }
}

} // namespace

int main() {
  constexpr std::string_view source = R"(
def main() : int {
    var index : int = 1
    var total : int = 0
    while index <= 5 {
        total = total + index
        index = index + 1
    }
    if total == 15 {
        return 42
    } else {
        return 0
    }
}
)";

  janus::frontend::Parser parser{source};
  const janus::ast::Program program = parser.parse_program();
  expect(std::holds_alternative<std::shared_ptr<janus::ast::WhileStatement>>(
             program.functions[0].body[2]),
         "while is represented in the AST");
  expect(std::holds_alternative<std::shared_ptr<janus::ast::IfStatement>>(
             program.functions[0].body[3]),
         "if/else is represented in the AST");

  janus::frontend::Parser jump_parser{
      "def main() : int { while true { continue break } return 0 }"};
  const janus::ast::Program jump_program = jump_parser.parse_program();
  const auto &jump_loop =
      *std::get<std::shared_ptr<janus::ast::WhileStatement>>(
          jump_program.functions[0].body[0]);
  expect(std::holds_alternative<janus::ast::ContinueStatement>(
             jump_loop.body[0]),
         "continue is represented explicitly in the AST");
  expect(std::holds_alternative<janus::ast::BreakStatement>(jump_loop.body[1]),
         "break is represented explicitly in the AST");

  janus::semantic::Analyzer analyzer;
  static_cast<void>(analyzer.analyze(program));
  janus::frontend::Parser valid_jump_parser{
      "def main() : int { while true { while true { break } continue } "
      "return 0 }"};
  static_cast<void>(analyzer.analyze(valid_jump_parser.parse_program()));

  llvm::LLVMContext context;
  janus::backend::llvm::IrGenerator generator{context};
  const std::unique_ptr<llvm::Module> module =
      generator.generate(program, "control_flow");
  std::string ir;
  llvm::raw_string_ostream output{ir};
  module->print(output, nullptr);
  output.flush();
  expect(ir.find("while.condition") != std::string::npos,
         "while creates a condition block");
  expect(ir.find("if.then") != std::string::npos &&
             ir.find("if.else") != std::string::npos,
         "if/else creates distinct LLVM blocks");
  expect(ir.find("br i1") != std::string::npos,
         "conditions generate conditional branches");

  janus::frontend::Parser emitted_jump_parser{R"(
def main() : int {
    var index : int = 0
    while index < 10 {
        index = index + 1
        if index < 3 {
            continue
        }
        if index == 5 {
            break
        }
    }
    return index
}
)"};
  const janus::ast::Program emitted_jump_program =
      emitted_jump_parser.parse_program();
  static_cast<void>(analyzer.analyze(emitted_jump_program));
  llvm::LLVMContext jump_context;
  janus::backend::llvm::IrGenerator jump_generator{jump_context};
  const std::unique_ptr<llvm::Module> jump_module =
      jump_generator.generate(emitted_jump_program, "loop_jumps");
  std::string jump_ir;
  llvm::raw_string_ostream jump_output{jump_ir};
  jump_module->print(jump_output, nullptr);
  jump_output.flush();
  const std::size_t first_then = jump_ir.find("if.then:");
  const std::size_t second_then = jump_ir.find("if.then", first_then + 1);
  expect(first_then != std::string::npos &&
             jump_ir.find("br label %while.condition", first_then) !=
                 std::string::npos,
         "continue branches to the nearest while condition");
  expect(second_then != std::string::npos &&
             jump_ir.find("br label %while.end", second_then) !=
                 std::string::npos,
         "break branches to the nearest while exit");

  janus::frontend::Parser cleanup_parser{R"(
def breakOuter() : Unit {}
def breakIteration() : Unit {}
def breakNested() : Unit {}
def continueOuter() : Unit {}
def continueIteration() : Unit {}
def continueNested() : Unit {}
def breakLoop() : Unit {
    defer breakOuter()
    while true {
        defer breakIteration()
        if true {
            defer breakNested()
            break
        }
    }
}
def continueLoop() : Unit {
    defer continueOuter()
    var index : int = 0
    while index < 1 {
        index = index + 1
        defer continueIteration()
        if true {
            defer continueNested()
            continue
        }
    }
}
def main() : int {
    breakLoop()
    continueLoop()
    return 0
}
)"};
  const janus::ast::Program cleanup_program = cleanup_parser.parse_program();
  static_cast<void>(analyzer.analyze(cleanup_program));
  llvm::LLVMContext cleanup_context;
  janus::backend::llvm::IrGenerator cleanup_generator{cleanup_context};
  const std::unique_ptr<llvm::Module> cleanup_module =
      cleanup_generator.generate(cleanup_program, "loop_jump_cleanup");
  std::string cleanup_ir;
  llvm::raw_string_ostream cleanup_output{cleanup_ir};
  cleanup_module->print(cleanup_output, nullptr);
  cleanup_output.flush();
  const std::size_t break_function =
      cleanup_ir.find("define void @breakLoop()");
  const std::size_t break_nested =
      cleanup_ir.find("call void @breakNested()", break_function);
  const std::size_t break_iteration =
      cleanup_ir.find("call void @breakIteration()", break_nested);
  const std::size_t break_branch =
      cleanup_ir.find("br label %while.end", break_iteration);
  const std::size_t break_outer =
      cleanup_ir.find("call void @breakOuter()", break_function);
  expect(break_nested < break_iteration && break_iteration < break_branch,
         "break unwinds abandoned scopes in LIFO order");
  expect(break_outer != std::string::npos &&
             !(break_nested < break_outer && break_outer < break_branch),
         "break retains function-scope deferred actions");
  const std::size_t continue_function =
      cleanup_ir.find("define void @continueLoop()");
  const std::size_t continue_nested =
      cleanup_ir.find("call void @continueNested()", continue_function);
  const std::size_t continue_iteration =
      cleanup_ir.find("call void @continueIteration()", continue_nested);
  const std::size_t continue_branch =
      cleanup_ir.find("br label %while.condition", continue_iteration);
  const std::size_t continue_outer =
      cleanup_ir.find("call void @continueOuter()", continue_function);
  expect(continue_nested < continue_iteration &&
             continue_iteration < continue_branch,
         "continue unwinds abandoned scopes in LIFO order");
  expect(continue_outer != std::string::npos &&
             !(continue_nested < continue_outer &&
               continue_outer < continue_branch),
         "continue retains function-scope deferred actions");

  constexpr std::string_view definitely_initialized = R"(
def main() : int {
    var result : int
    if true {
        result = 1
    } else {
        result = 2
    }
    return result
}
)";
  janus::frontend::Parser initialized_parser{definitely_initialized};
  const janus::ast::Program initialized_program =
      initialized_parser.parse_program();
  static_cast<void>(analyzer.analyze(initialized_program));

  expect_compile_error(
      "def main() : int { var x : int if true { x = 1 } return x }",
      "used before initialization");
  expect_compile_error(
      "def main() : int { var x : int while false { x = 1 } return x }",
      "used before initialization");
  expect_compile_error(
      "def main() : int { if 1 { return 1 } else { return 0 } }",
      "where type 'bool' is required");
  expect_compile_error(
      "def main() : int { if true { return 1 } else { return 0 } return 2 }",
      "unreachable statement");
  expect_compile_error("def main() : int { while true { return 1 } }",
                       "must return a value");
  expect_compile_error("def main() : int { break return 0 }",
                       "break can only be used inside a loop");
  expect_compile_error("def main() : int { continue return 0 }",
                       "continue can only be used inside a loop");
  expect_compile_error(
      "def main() : int { while true { break println(1) } return 0 }",
      "unreachable statement");
  expect_compile_error(
      "def main() : int { while true { if true { continue } "
      "else { break } println(1) } return 0 }",
      "unreachable statement");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }

  std::cout << "if/else and while generate typed control flow\n";
  return 0;
}
