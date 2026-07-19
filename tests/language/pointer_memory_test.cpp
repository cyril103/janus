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
    expect(false, "invalid pointer source must fail");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(expected_message) !=
               std::string_view::npos,
           "pointer error contains the expected explanation");
  }
}

} // namespace

int main() {
  constexpr std::string_view source = R"(
def main() : int {
    var data : Ptr[int] = alloc[int](usize(2))
    data.store(usize(0), 40)
    data.store(usize(1), 2)
    val first : int = data.load(usize(0))
    val second : int = data.load(usize(1))
    data = realloc[int](data, usize(4))
    data.store(usize(2), first + second)
    val missing : Ptr[int] = null[int]()
    val allocated : bool = data != missing
    val elementSize : usize = sizeof[int]()
    val elementAlignment : usize = alignof[int]()
    val result : int = data.load(usize(2))
    free(data)
    return result
}
)";

  janus::frontend::Parser parser{source};
  const janus::ast::Program program = parser.parse_program();
  janus::semantic::Analyzer analyzer;
  const janus::semantic::AnalysisResult analysis = analyzer.analyze(program);
  expect(analysis.functions.at("main").at("data").type.name() == "Ptr[int]",
         "Ptr retains its element type");

  llvm::LLVMContext context;
  janus::backend::llvm::IrGenerator generator{context};
  const std::unique_ptr<llvm::Module> module =
      generator.generate(program, "pointer_memory");
  std::string ir;
  llvm::raw_string_ostream output{ir};
  module->print(output, nullptr);
  output.flush();
  const bool computes_allocation_size =
      ir.find("allocation.bytes = mul i64 2") != std::string::npos ||
      ir.find("call ptr @janus_alloc(i64 8)") != std::string::npos;
  expect(computes_allocation_size &&
             ir.find("call ptr @janus_alloc") != std::string::npos,
         "alloc multiplies count by sizeof(T)");
  expect(ir.find("call ptr @janus_realloc") != std::string::npos,
         "realloc resizes the raw buffer");
  expect(ir.find("getelementptr inbounds i32") != std::string::npos,
         "load/store use typed pointer arithmetic");
  expect(ir.find("call void @janus_free") != std::string::npos,
         "free releases the raw buffer");
  expect(ir.find("icmp ne ptr") != std::string::npos,
         "pointers support equality comparisons");

  expect_compile_error(
      "def main() : int { val data : Ptr = null[int]() return 0 }",
      "Ptr expects exactly one type argument");
  expect_compile_error(
      "def main() : int { val data : Ptr[int] = alloc[int](1) return 0 }",
      "where type 'usize' is required");
  expect_compile_error(
      "def main() : int { val data : Ptr[int] = alloc[int](usize(1)) "
      "data.store(usize(0), true) return 0 }",
      "where type 'int' is required");
  expect_compile_error(
      "def main() : int { val data : Ptr[int] = alloc[int](usize(1)) "
      "val value : int = data.load(0) return value }",
      "where type 'usize' is required");
  expect_compile_error("def main() : int { free(1) return 0 }",
                       "free requires a Ptr[T]");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }

  std::cout << "Ptr[T] provides typed manual memory operations\n";
  return 0;
}
