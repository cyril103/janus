#include "janus/driver/formatter.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

int main() {
  const std::string source = "def main() : int {  \n"
                             " val text : string = \"{ unchanged }\"\n"
                             "if true {\n"
                             "return 0\n"
                             "}\n"
                             "}\n";
  const std::string expected = "def main() : int {\n"
                               "    val text : string = \"{ unchanged }\"\n"
                               "    if true {\n"
                               "        return 0\n"
                               "    }\n"
                               "}\n";
  const std::string formatted = janus::driver::format_source(source);
  if (formatted != expected) {
    std::cerr << "unexpected formatted source:\n" << formatted;
    return 1;
  }
  if (janus::driver::format_source(formatted) != formatted) {
    std::cerr << "formatting is not idempotent\n";
    return 1;
  }
  const std::string expression_body_comment =
      "def f() : int\n"
      "// commentaire\n"
      "=> 1\n";
  const std::string formatted_expression_body_comment =
      janus::driver::format_source(expression_body_comment);
  if (formatted_expression_body_comment != expression_body_comment ||
      janus::driver::format_source(formatted_expression_body_comment) !=
          expression_body_comment) {
    std::cerr << "formatter did not preserve a comment before a function "
                 "arrow idempotently:\n"
              << formatted_expression_body_comment;
    return 1;
  }
  const std::string comments = "def main() : int {\n// keep { this comment "
                               "}\n\n\nreturn 0 // and this\n}\n";
  const janus::driver::FormatOptions compact{2, 0, 100};
  const std::string expected_comments =
      "def main() : int {\n  // keep { this comment }\n  return 0 // and "
      "this\n}\n";
  if (janus::driver::format_source(comments, compact) != expected_comments) {
    std::cerr << "comments or formatter options were not preserved\n";
    return 1;
  }
  const std::string indexing =
      "def indexed(values : Array[string]) : string {\n"
      "// brackets in comments [ stay untouched ]\n"
      "val marker : string = \"[not an index]\"\n"
      "values[usize(0)] += \"!\" // indexed target\n"
      "return values[usize(0)]\n}\n";
  const std::string formatted_indexing =
      "def indexed(values : Array[string]) : string {\n"
      "    // brackets in comments [ stay untouched ]\n"
      "    val marker : string = \"[not an index]\"\n"
      "    values[usize(0)] += \"!\" // indexed target\n"
      "    return values[usize(0)]\n}\n";
  if (janus::driver::format_source(indexing) != formatted_indexing ||
      janus::driver::format_source(formatted_indexing) != formatted_indexing) {
    std::cerr << "indexed syntax, comments, or strings were not preserved\n";
    return 1;
  }

  const std::string else_if =
      "def choose(first : bool, second : bool) : int {\n"
      "if first {\n"
      "return 1\n"
      "} else if second {\n"
      "return 2\n"
      "} else {\n"
      "return 0\n"
      "}\n"
      "}\n";
  const std::string expected_else_if =
      "def choose(first : bool, second : bool) : int {\n"
      "    if first {\n"
      "        return 1\n"
      "    } else if second {\n"
      "        return 2\n"
      "    } else {\n"
      "        return 0\n"
      "    }\n"
      "}\n";
  const std::string formatted_else_if = janus::driver::format_source(else_if);
  if (formatted_else_if != expected_else_if ||
      janus::driver::format_source(formatted_else_if) != formatted_else_if) {
    std::cerr << "else-if formatting is not canonical and idempotent\n";
    return 1;
  }
  const std::string lambda_block =
      "def factory(base : int) : (int) => int {\n"
      "return (value : int) => {\nval sum : int = base + value\n"
      "if sum > 0 {\nreturn sum\n}\nreturn 0\n}\n}\n";
  const std::string formatted_lambda_block =
      "def factory(base : int) : (int) => int {\n"
      "    return (value : int) => {\n"
      "        val sum : int = base + value\n"
      "        if sum > 0 {\n"
      "            return sum\n"
      "        }\n"
      "        return 0\n"
      "    }\n"
      "}\n";
  if (janus::driver::format_source(lambda_block) != formatted_lambda_block ||
      janus::driver::format_source(formatted_lambda_block) !=
          formatted_lambda_block) {
    std::cerr << "lambda block formatting is not roundtrip-idempotent\n";
    return 1;
  }
  const std::string contextual_lambdas =
      "def use() : int {\n"
      "val unary : (int) => int = value=>value + 1\n"
      "val binary : (int, int) => int = (left,right)=>left + right\n"
      "val shared : (borrow int) => int = (borrow value)=>value\n"
      "val mutable : (borrow var int) => int = (borrow var value)=>value\n"
      "return binary(unary(1), shared(2))\n}\n";
  const std::string formatted_contextual_lambdas =
      "def use() : int {\n"
      "    val unary : (int) => int = value=>value + 1\n"
      "    val binary : (int, int) => int = (left,right)=>left + right\n"
      "    val shared : (borrow int) => int = (borrow value)=>value\n"
      "    val mutable : (borrow var int) => int = (borrow var value)=>value\n"
      "    return binary(unary(1), shared(2))\n}\n";
  const std::string contextual_formatted =
      janus::driver::format_source(contextual_lambdas);
  if (contextual_formatted != formatted_contextual_lambdas ||
      janus::driver::format_source(contextual_formatted) !=
          contextual_formatted) {
    std::cerr << "contextual lambda formatting is not preserving and "
                 "idempotent:\n"
              << contextual_formatted;
    return 1;
  }
  const std::string derivations = "struct Point(val x : int, val y : int)\n"
                                  "derives Copy, Equality, Hashing, Debug {\n"
                                  "}\n";
  const std::string formatted_derivations =
      janus::driver::format_source(derivations);
  if (formatted_derivations != derivations ||
      janus::driver::format_source(formatted_derivations) !=
          formatted_derivations) {
    std::cerr << "derivation clauses are not preserved idempotently\n";
    return 1;
  }
  const std::string imports = "import std.fs as fs\n"
                              "import std.result.{\n"
                              "Result,\n"
                              "Ok as Success\n"
                              "}\n";
  const std::string formatted_imports = "import std.fs as fs\n"
                                        "import std.result.{\n"
                                        "    Result,\n"
                                        "    Ok as Success\n"
                                        "}\n";
  if (janus::driver::format_source(imports) != formatted_imports ||
      janus::driver::format_source(formatted_imports) != formatted_imports) {
    std::cerr << "selective import aliases are not preserved idempotently\n";
    return 1;
  }
  const std::string array_literals =
      "def bytes() : Array[ubyte] {\nreturn [\n0xF0,\n0x90,\n0xF0\n]\n}\n";
  const std::string formatted_arrays =
      "def bytes() : Array[ubyte] {\n    return [\n        0xF0,\n        0x90,\n        0xF0\n    ]\n}\n";
  if (janus::driver::format_source(array_literals) != formatted_arrays ||
      janus::driver::format_source(formatted_arrays) != formatted_arrays) {
    std::cerr << "array literal formatting is not stable and idempotent\n";
    return 1;
  }
  const std::string multiline_calls =
      "def check() : bool {\nreturn assertTrue(checkFile(\n"
      "\"tests/compiler.janus\",\noutput\n))\n}\n";
  const std::string formatted_multiline_calls =
      "def check() : bool {\n"
      "    return assertTrue(checkFile(\n"
      "        \"tests/compiler.janus\",\n"
      "        output\n"
      "    ))\n"
      "}\n";
  if (janus::driver::format_source(multiline_calls) !=
          formatted_multiline_calls ||
      janus::driver::format_source(formatted_multiline_calls) !=
          formatted_multiline_calls) {
    std::cerr << "multiline calls are not indented idempotently:\n"
              << janus::driver::format_source(multiline_calls);
    return 1;
  }
  const std::string local_types =
      "def main() : int {\nval inferred=answer()\nval explicit:int=1\nreturn explicit\n}\n";
  const std::string formatted_local_types =
      "def main() : int {\n    val inferred=answer()\n    val explicit:int=1\n    return explicit\n}\n";
  if (janus::driver::format_source(local_types) != formatted_local_types) {
    std::cerr << "formatter changed omitted versus explicit local types:\n"
              << janus::driver::format_source(local_types);
    return 1;
  }
  const std::string constants =
      "const answer : int = 42\n"
      "const def choose(flag : bool) : int {\n"
      "if flag {\nreturn answer\n} else {\nreturn 0\n}\n}\n"
      "staticAssert(choose(true) == 42, \"stable\")\n";
  const std::string formatted_constants =
      "const answer : int = 42\n"
      "const def choose(flag : bool) : int {\n"
      "    if flag {\n        return answer\n    } else {\n"
      "        return 0\n    }\n}\n"
      "staticAssert(choose(true) == 42, \"stable\")\n";
  if (janus::driver::format_source(constants) != formatted_constants ||
      janus::driver::format_source(formatted_constants) !=
          formatted_constants) {
    std::cerr << "constant syntax formatting is not idempotent\n";
    return 1;
  }
  const std::string expression_bodies =
      "def square(value : int) : int=>value * value\n"
      "def choose(value : int) : int => match value {\n"
      "0=>0,\n_=>value\n}\n"
      "def callback() : (int) => int => (value : int)=>value + 1\n";
  const std::string formatted_expression_bodies =
      "def square(value : int) : int => value * value\n"
      "def choose(value : int) : int => match value { 0=>0, _=>value }\n"
      "def callback() : (int) => int => (value : int)=>value + 1\n";
  const std::string formatted_expression_body =
      janus::driver::format_source(expression_bodies);
  if (formatted_expression_body != formatted_expression_bodies ||
      janus::driver::format_source(formatted_expression_body) !=
          formatted_expression_body) {
    std::cerr << "expression-body formatting is not canonical, disambiguated and idempotent:\n"
              << formatted_expression_body;
    return 1;
  }
  const std::string continued_expression_body =
      "def total(first : int, second : int) : int=>\nfirst + second\n";
  const std::string formatted_continued_expression_body =
      "def total(first : int, second : int) : int => first + second\n";
  if (janus::driver::format_source(continued_expression_body) !=
          formatted_continued_expression_body ||
      janus::driver::format_source(formatted_continued_expression_body) !=
          formatted_continued_expression_body) {
    std::cerr << "multiline expression-body continuation is not deterministic\n";
    return 1;
  }
  const std::string complex_continuations =
      "def total(first : int, second : int, third : int) : int\n"
      "=>\n"
      "first +\n"
      "second +\n"
      "third\n"
      "class Calculator() {\n"
      "def choose(value : int) : int =>\n"
      "match value {\n"
      "0 => (x : int)=>x(0),\n"
      "_ => value\n"
      "}\n"
      "}\n";
  const std::string formatted_complex_continuations =
      "def total(first : int, second : int, third : int) : int => first + second + third\n"
      "class Calculator() {\n"
      "    def choose(value : int) : int => match value { 0 => (x : int)=>x(0), _ => value }\n"
      "}\n";
  const std::string formatted_complex =
      janus::driver::format_source(complex_continuations);
  if (formatted_complex != formatted_complex_continuations ||
      janus::driver::format_source(formatted_complex) != formatted_complex) {
    std::cerr << "full expression-body continuations are not deterministic:\n"
              << formatted_complex;
    return 1;
  }
  const std::string invalid_source = "def broken( : int => (x:int)=>x\n";
  if (janus::driver::format_source(invalid_source).find("(x:int)=>x") ==
      std::string::npos) {
    std::cerr << "invalid-source fallback corrupts unrelated arrows\n";
    return 1;
  }
  const std::string integer_spellings =
      "def bits() : uint {\nreturn 0xA2_0A + 0B1111_0000 + 1_000\n}\n";
  const std::string formatted_integer_spellings =
      "def bits() : uint {\n    return 0xA2_0A + 0B1111_0000 + 1_000\n}\n";
  if (janus::driver::format_source(integer_spellings) !=
          formatted_integer_spellings ||
      janus::driver::format_source(formatted_integer_spellings) !=
          formatted_integer_spellings) {
    std::cerr << "formatter did not preserve integer base and separators\n";
    return 1;
  }
  const std::string bitwise =
      "def bits(value : ubyte) : ubyte {\nreturn (value&ubyte(15))<<1|value>>2^ubyte(3)\n}\n";
  const std::string formatted_bitwise = janus::driver::format_source(bitwise);
  if (janus::driver::format_source(formatted_bitwise) != formatted_bitwise) {
    std::cerr << "bitwise formatting is not idempotent\n";
    return 1;
  }
  const std::string guarded_match =
      "def decode(opcode:uint):int {\nreturn match opcode {\n"
      "uint(0)=>0,\nuint(8) if opcode&uint(15)==uint(1)=>1,\n_=>-1\n}\n}\n";
  const std::string formatted_guarded_match =
      janus::driver::format_source(guarded_match);
  if (formatted_guarded_match.find(
          "uint(8) if opcode&uint(15)==uint(1)=>1") ==
          std::string::npos ||
      janus::driver::format_source(formatted_guarded_match) !=
          formatted_guarded_match) {
    std::cerr << "literal and guarded match formatting is not canonical and idempotent:\n"
              << formatted_guarded_match;
    return 1;
  }
  const std::string compound_assignments =
      "def update() : int {\nvar x:int=1\nx+=2 // keep rhs\n"
      "x-=3 x*=4\nx/=5\nx%=6\nx&=7\nx|=8\nx^=9\nx<<=1\nx>>=2\nreturn x\n}\n";
  const std::string formatted_compound =
      janus::driver::format_source(compound_assignments);
  if (formatted_compound.find("    x += 2 // keep rhs\n") ==
          std::string::npos ||
      formatted_compound.find("    x -= 3 x *= 4\n") ==
          std::string::npos ||
      formatted_compound.find("    x <<= 1\n") == std::string::npos ||
      janus::driver::format_source(formatted_compound) != formatted_compound) {
    std::cerr << "compound assignments are not canonical, comment-safe and idempotent:\n"
              << formatted_compound;
    return 1;
  }
  janus::driver::FormatOptions narrow;
  narrow.max_line_length = 48;
  const std::string width_input =
      "def short(value : int) : int =>\n"
      "    value + 1\n"
      "def long(first : int, second : int) : int => first + second\n";
  const std::string width_expected =
      "def short(value : int) : int => value + 1\n"
      "def long(first : int, second : int) : int =>\n"
      "    first + second\n";
  const std::string width_formatted =
      janus::driver::format_source(width_input, narrow);
  if (width_formatted != width_expected ||
      janus::driver::format_source(width_formatted, narrow) != width_expected) {
    std::cerr << "expression bodies do not obey canonical line width idempotently:\n"
              << width_formatted;
    return 1;
  }
  const std::string semantic_input =
      "def callback() : (int) => int => (value : int) => value + 1\n"
      "def choose(value : int) : int => match value { 0 => 1, _ => value }\n"
      "def text() : string =>\n"
      "    \"=> is text, not a function arrow\" // preserve => comment\n";
  const std::string semantic_expected =
      "def callback() : (int) => int =>\n"
      "    (value : int) => value + 1\n"
      "def choose(value : int) : int =>\n"
      "    match value { 0 => 1, _ => value }\n"
      "def text() : string =>\n"
      "    \"=> is text, not a function arrow\" // preserve => comment\n";
  const std::string semantic_formatted =
      janus::driver::format_source(semantic_input, narrow);
  if (semantic_formatted != semantic_expected ||
      janus::driver::format_source(semantic_formatted, narrow) !=
          semantic_expected) {
    std::cerr << "width formatting confused arrows or changed comments/strings:\n"
              << semantic_formatted;
    return 1;
  }
  const std::string arrow_comments =
      "def afterArrow() : int => // keep after arrow\n"
      "    1\n"
      "def beforeArrow() : int // keep before arrow\n"
      "=> 2\n";
  const std::string formatted_arrow_comments =
      "def afterArrow() : int => // keep after arrow\n"
      "    1\n"
      "def beforeArrow() : int // keep before arrow\n"
      "=> 2\n";
  const std::string actual_arrow_comments =
      janus::driver::format_source(arrow_comments);
  if (actual_arrow_comments != formatted_arrow_comments ||
      janus::driver::format_source(actual_arrow_comments) !=
          formatted_arrow_comments) {
    std::cerr << "formatter moved or removed comments around a function arrow:\n"
              << actual_arrow_comments;
    return 1;
  }
  const std::filesystem::path config =
      std::filesystem::temp_directory_path() / "janus-formatter-width-test.toml";
  {
    std::ofstream output{config};
    output << "max_line_length = 72\n";
  }
  const janus::driver::FormatOptions configured =
      janus::driver::load_format_options(config);
  std::filesystem::remove(config);
  if (configured.max_line_length != 72) {
    std::cerr << "max_line_length configuration was not loaded\n";
    return 1;
  }
  {
    std::ofstream output{config};
    output << "max_line_length = 20\n";
  }
  bool rejected_invalid_width = false;
  try {
    static_cast<void>(janus::driver::load_format_options(config));
  } catch (const std::runtime_error &) {
    rejected_invalid_width = true;
  }
  std::filesystem::remove(config);
  if (!rejected_invalid_width) {
    std::cerr << "invalid max_line_length configuration was accepted\n";
    return 1;
  }
  std::cout << "Janus formatting is deterministic\n";
  return 0;
}
