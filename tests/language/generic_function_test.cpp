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
    expect(false, "invalid generic source must produce a compile error");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(expected_message) !=
               std::string_view::npos,
           "generic compile error contains the expected explanation");
  }
}

} // namespace

int main() {
  constexpr std::string_view source = R"(
def identity[T](value : T) : T {
    return value
}

def main() : int {
    val integer : int = identity[int](5)
    val floating : double = identity[double](2.5)
    val message : string = identity[string]("Janus")
    return integer
}
)";

  janus::frontend::Parser parser{source};
  const janus::ast::Program program = parser.parse_program();
  expect(program.functions.size() == 2, "two functions are parsed");
  expect(program.functions.front().type_parameters.size() == 1,
         "identity declares one type parameter");
  expect(program.functions.front().type_parameters.front() == "T",
         "the type parameter is named T");
  expect(program.functions.front().parameters.size() == 1,
         "identity declares one value parameter");
  expect(program.functions.front().parameters.front().type.name == "T",
         "the value parameter uses T");
  expect(program.functions.front().return_type.name == "T",
         "identity returns T");

  janus::semantic::Analyzer analyzer;
  const janus::semantic::AnalysisResult analysis = analyzer.analyze(program);
  expect(!analysis.functions.at("identity").at("value").type.is_concrete(),
         "identity parameter remains symbolic during generic analysis");

  llvm::LLVMContext context;
  janus::backend::llvm::IrGenerator generator{context};
  const std::unique_ptr<llvm::Module> module =
      generator.generate(program, "generics");
  std::string ir;
  llvm::raw_string_ostream output{ir};
  module->print(output, nullptr);
  output.flush();

  expect(ir.find("define i32 @identity__int(i32") != std::string::npos,
         "identity[int] is monomorphized with i32");
  expect(ir.find("define double @identity__double(double") != std::string::npos,
         "identity[double] is monomorphized with double");
  expect(ir.find("call i32 @identity__int(i32 5)") != std::string::npos,
         "main calls the int specialization");
  expect(ir.find("call double @identity__double(double 2.500000e+00)") !=
             std::string::npos,
         "main calls the double specialization");
  expect(ir.find("define { ptr, i64 } @identity__string({ ptr, i64 }") !=
             std::string::npos,
         "identity[string] is monomorphized with the string structure");

  expect_compile_error("def identity[T](x : T) : T { return x } "
                       "def main() : int { return identity(1) }",
                       "expects 1 type argument");
  expect_compile_error("def identity[T](x : T) : T { return x } "
                       "def main() : int { return identity[bool](1) }",
                       "expression of type 'int'");
  expect_compile_error("def identity[T](x : T) : T { return x } "
                       "def main() : int { return identity[missing](1) }",
                       "unknown type 'missing'");
  expect_compile_error("def duplicate[T, T](x : T) : T { return x } "
                       "def main() : int { return 0 }",
                       "type parameter 'T' is already declared");
  expect_compile_error("def main() : int { return missing }",
                       "unknown value 'missing'");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }

  std::cout << "generic identity function is monomorphized for each type\n";
  return 0;
}
