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
    expect(false, "invalid enum program must fail");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(expected_message) !=
               std::string_view::npos,
           "enum error contains the expected explanation");
  }
}

} // namespace

int main() {
  constexpr std::string_view source = R"(
enum Direction {
    North,
    East = 4,
    South,
    West = -2
}

def main() : int {
    val current : Direction = Direction.South
    val unchecked : Direction = Direction(99)
    if current == Direction.South {
        return int(current)
    }
    return int(Direction.West)
}
)";

  janus::frontend::Parser parser{source};
  const janus::ast::Program program = parser.parse_program();
  expect(program.enums.size() == 1, "parser retains the enum declaration");
  expect(program.enums.front().cases.size() == 4,
         "parser retains every enum case");
  expect(program.enums.front().cases[0].value == 0,
         "implicit discriminants start at zero");
  expect(program.enums.front().cases[2].value == 5,
         "implicit discriminants continue after an explicit value");
  expect(program.enums.front().cases[3].value == -2,
         "negative explicit discriminants are supported");

  janus::semantic::Analyzer analyzer;
  const janus::semantic::AnalysisResult analysis = analyzer.analyze(program);
  expect(analysis.functions.at("main").at("current").type.name() == "Direction",
         "enum variables retain their nominal type");

  llvm::LLVMContext context;
  janus::backend::llvm::IrGenerator generator{context};
  const std::unique_ptr<llvm::Module> module =
      generator.generate(program, "enum");
  std::string ir;
  llvm::raw_string_ostream output{ir};
  module->print(output, nullptr);
  output.flush();

  expect(ir.find("%current = alloca i32") != std::string::npos,
         "enums use a compact signed 32-bit representation");
  expect(ir.find("store i32 5, ptr %current") != std::string::npos,
         "enum cases lower to their discriminants");
  expect(ir.find("icmp eq i32") != std::string::npos,
         "values of the same enum can be compared");

  expect_compile_error("enum Color { Red } def main() : int { "
                       "val color : Color = Color.Blue return 0 }",
                       "enum 'Color' has no case 'Blue'");
  expect_compile_error("enum Color { Red, Red } def main() : int { return 0 }",
                       "enum case 'Red' is already declared");
  expect_compile_error(
      "enum Color { Red } enum State { Red } def main() : int { "
      "val color : Color = Color.Red val state : State = State.Red "
      "if color == state { return 1 } return 0 }",
      "operands must have the same type");
  expect_compile_error("enum Empty {} def main() : int { return 0 }",
                       "must declare at least one case");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }

  std::cout << "enums are nominal signed 32-bit types\n";
  return 0;
}
