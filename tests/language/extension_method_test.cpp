#include "janus/diagnostics/compile_error.hpp"
#include "janus/frontend/parser.hpp"
#include "janus/semantic/analyzer.hpp"

#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

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
    expect(false, "invalid extension program must fail");
  } catch (const janus::CompileError &error) {
    const bool matches = std::string_view{error.what()}.find(
                             expected_message) != std::string_view::npos;
    if (!matches)
      std::cerr << "expected diagnostic containing: " << expected_message
                << "\nactual diagnostic: " << error.what() << '\n';
    expect(matches, "extension error contains the expected explanation");
  }
}

janus::ast::Program combine_modules(std::string_view dependency_source,
                                    std::string_view consumer_source) {
  janus::frontend::Parser dependency_parser{dependency_source};
  janus::ast::Program dependency = dependency_parser.parse_program();
  janus::frontend::Parser consumer_parser{consumer_source};
  janus::ast::Program consumer = consumer_parser.parse_program();
  consumer.enums.insert(consumer.enums.end(),
                        std::make_move_iterator(dependency.enums.begin()),
                        std::make_move_iterator(dependency.enums.end()));
  consumer.classes.insert(consumer.classes.end(),
                          std::make_move_iterator(dependency.classes.begin()),
                          std::make_move_iterator(dependency.classes.end()));
  consumer.extensions.insert(
      consumer.extensions.end(),
      std::make_move_iterator(dependency.extensions.begin()),
      std::make_move_iterator(dependency.extensions.end()));
  return consumer;
}

} // namespace

int main() {
  constexpr std::string_view source = R"(
enum Option[T] { Some(T), None }
extend[T] Option[T] {
    consume def map[U](scoped transform : (T) => U) : Option[U] {
        defer delete transform
        return match move this {
            Some(value) => Option.Some[U](transform(move value)),
            None => Option.None[U]()
        }
    }
    borrow def isSome() : bool {
        return match this { Some(value) => true, None => false }
    }
}
def main() : int {
    val source : Option[int] = Option.Some[int](20)
    val mapped : Option[int] = source.map(value => value * 2)
    return if mapped.isSome() { 0 } else { 1 }
}
)";
  janus::frontend::Parser parser{source};
  const janus::ast::Program program = parser.parse_program();
  expect(program.extensions.size() == 1,
         "parser records one extension declaration");
  expect(program.extensions.front().type_parameters.size() == 1 &&
             program.extensions.front().methods.size() == 2,
         "parser records extension and method generic parameters");
  expect(program.extensions.front().receiver_ownerships[0] ==
                 janus::ast::ParameterOwnership::Consume &&
             program.extensions.front().receiver_ownerships[1] ==
                 janus::ast::ParameterOwnership::Borrow,
         "parser records receiver ownership contracts");

  janus::semantic::Analyzer analyzer;
  const janus::semantic::AnalysisResult analysis = analyzer.analyze(program);
  expect(analysis.extension_calls.size() == 2,
         "analyzer resolves generic consume and borrow extension calls");

  expect_compile_error(
      "class Box() {} extend Box { def value() : int { return 1 } } "
      "def main() : int { return 0 }",
      "requires 'borrow', 'borrow var', or 'consume'");
  expect_compile_error(
      "class Box() {} extend Box { borrow def value() : int { return 1 } "
      "borrow def value() : int { return 2 } } "
      "def main() : int { return 0 }",
      "already declared in this block");
  expect_compile_error("class Box() { borrow def value() : int { return 1 } } "
                       "extend Box { borrow def value() : int { return 2 } } "
                       "def main() : int { return 0 }",
                       "cannot replace a native method");
  expect_compile_error(
      "enum Box[T] { Value(T) } "
      "extend[T] Box[T] { consume def take() : T { "
      "return match move this { Value(value) => move value } } } "
      "def main() : int { val box : Box[int] = Box.Value[int](1) "
      "val value : int = box.take() return match box { Value(item) => item } }",
      "used before initialization");

  janus::ast::Program foreign_public =
      combine_modules("module types enum Foreign { Value }",
                      "module consumer import types "
                      "extend Foreign { borrow def code() : int { return 1 } } "
                      "def main() : int { return 0 }");
  try {
    static_cast<void>(analyzer.analyze(foreign_public));
    expect(false, "public orphan extension must fail");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(
               "public extension of imported type") != std::string_view::npos,
           "public orphan extension explains the coherence rule");
  }

  janus::ast::Program private_foreign = combine_modules(
      "module types enum Foreign { Value }",
      "module consumer import types "
      "private extend Foreign { borrow def code() : int { return 7 } } "
      "def main() : int { return Foreign.Value().code() - 7 }");
  const janus::semantic::AnalysisResult private_analysis =
      analyzer.analyze(private_foreign);
  expect(private_analysis.extension_calls.size() == 1,
         "private extension adapts an imported type locally");

  janus::ast::Program ambiguous =
      combine_modules("module types enum Foreign { Value } "
                      "extend Foreign { borrow def code() : int { return 1 } } "
                      "extend Foreign { borrow def code() : int { return 2 } }",
                      "module consumer import types "
                      "def main() : int { return Foreign.Value().code() }");
  try {
    static_cast<void>(analyzer.analyze(ambiguous));
    expect(false, "ambiguous imported extensions must fail");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(
               "ambiguous between 2 visible extension blocks") !=
               std::string_view::npos,
           "ambiguous extensions are never selected silently");
  }

  std::vector<janus::ast::Program> inactive_imports;
  inactive_imports.push_back(combine_modules(
      "module types enum Foreign { Value } "
      "extend Foreign { borrow def code() : int { return 1 } }",
      "module consumer import types as imported "
      "def main() : int { return imported.Foreign.Value().code() }"));
  inactive_imports.push_back(
      combine_modules("module types enum Foreign { Value } "
                      "extend Foreign { borrow def code() : int { return 1 } }",
                      "module consumer import types.{Foreign} "
                      "def main() : int { return Foreign.Value().code() }"));
  for (const janus::ast::Program &inactive : inactive_imports) {
    try {
      static_cast<void>(analyzer.analyze(inactive));
      expect(false,
             "qualified and selective imports must not activate extensions");
    } catch (const janus::CompileError &error) {
      expect(std::string_view{error.what()}.find(
                 "extension exists in module 'types'") !=
                 std::string_view::npos,
             "inactive extension import suggests a plain import");
    }
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }
  std::cout << "static extension methods resolve coherently\n";
  return 0;
}
