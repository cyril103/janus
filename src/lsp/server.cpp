#include "janus/lsp/server.hpp"
#include "janus/build_identity.hpp"

#include "janus/diagnostics/compile_error.hpp"
#include "janus/driver/dependency.hpp"
#include "janus/driver/api_index.hpp"
#include "janus/driver/formatter.hpp"
#include "janus/driver/manifest.hpp"
#include "janus/frontend/lexer.hpp"
#include "janus/frontend/module_loader.hpp"
#include "janus/frontend/parser.hpp"
#include "janus/semantic/analyzer.hpp"

#include <llvm/Support/JSON.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

std::string serialize(llvm::json::Value value) {
  std::string result;
  llvm::raw_string_ostream output{result};
  output << value;
  return result;
}

llvm::json::Value request_id(const llvm::json::Object &request) {
  if (const llvm::json::Value *id = request.get("id"))
    return *id;
  return nullptr;
}

std::string response(llvm::json::Value id, llvm::json::Value result) {
  return serialize(llvm::json::Object{
      {"jsonrpc", "2.0"},
      {"id", std::move(id)},
      {"result", std::move(result)},
  });
}

std::string error_response(llvm::json::Value id, std::int64_t code,
                           std::string message) {
  return serialize(llvm::json::Object{
      {"jsonrpc", "2.0"},
      {"id", std::move(id)},
      {"error",
       llvm::json::Object{{"code", code}, {"message", std::move(message)}}},
  });
}

llvm::json::Object position(std::uint32_t line, std::uint32_t column) {
  return llvm::json::Object{
      {"line", static_cast<std::int64_t>(line)},
      {"character", static_cast<std::int64_t>(column)},
  };
}

std::string publish_diagnostics(std::string_view uri,
                                llvm::json::Array diagnostics) {
  return serialize(llvm::json::Object{
      {"jsonrpc", "2.0"},
      {"method", "textDocument/publishDiagnostics"},
      {"params",
       llvm::json::Object{
           {"uri", std::string{uri}},
           {"diagnostics", std::move(diagnostics)},
       }},
  });
}

using DocumentSymbol = janus::lsp::IndexedSymbol;

std::vector<janus::frontend::Token> tokens(std::string_view source) {
  std::vector<janus::frontend::Token> result;
  janus::frontend::Lexer lexer{source};
  try {
    while (true) {
      const janus::frontend::Token token = lexer.next();
      result.push_back(token);
      if (token.kind == janus::frontend::TokenKind::End)
        return result;
    }
  } catch (const janus::CompileError &) {
    return result;
  }
}

std::string
qualified_type_name(const std::vector<janus::frontend::Token> &document_tokens,
                    std::size_t start) {
  using janus::frontend::TokenKind;
  if (start >= document_tokens.size() ||
      document_tokens[start].kind != TokenKind::Identifier)
    return {};
  std::string result{document_tokens[start].lexeme};
  while (start + 2 < document_tokens.size() &&
         document_tokens[start + 1].kind == TokenKind::Dot &&
         document_tokens[start + 2].kind == TokenKind::Identifier) {
    result += "." + std::string{document_tokens[start + 2].lexeme};
    start += 2;
  }
  return result;
}

std::string function_signature(const janus::ast::FunctionDeclaration &function);
std::optional<std::filesystem::path> file_uri_path(std::string_view uri);
std::optional<std::string>
inferred_literal_type(const std::vector<janus::frontend::Token> &items,
                      std::size_t initializer);

void collect_local_declarations(
    const std::vector<janus::ast::Statement> &statements,
    std::unordered_set<const janus::ast::ValueDeclaration *> &declarations) {
  for (const janus::ast::Statement &statement : statements) {
    if (const auto *declaration =
            std::get_if<janus::ast::ValueDeclaration>(&statement)) {
      declarations.insert(declaration);
    } else if (const auto *branch =
                   std::get_if<std::shared_ptr<janus::ast::IfStatement>>(
                       &statement)) {
      collect_local_declarations((*branch)->then_body, declarations);
      collect_local_declarations((*branch)->else_body, declarations);
    } else if (const auto *loop =
                   std::get_if<std::shared_ptr<janus::ast::WhileStatement>>(
                       &statement)) {
      collect_local_declarations((*loop)->body, declarations);
    } else if (const auto *loop =
                   std::get_if<std::shared_ptr<janus::ast::ForStatement>>(
                       &statement)) {
      collect_local_declarations((*loop)->body, declarations);
    }
  }
}

std::unordered_map<std::size_t, std::string>
analyze_local_types(
    std::string_view uri, std::string_view source,
    const std::vector<std::filesystem::path> &module_search_paths = {},
    const std::vector<std::filesystem::path> &workspace_search_paths = {},
    const std::unordered_map<std::string, std::string> *open_documents =
        nullptr) {
  janus::frontend::Parser syntax_parser{source};
  janus::ast::Program syntax = syntax_parser.parse_program();
  const std::optional<std::string> entry_module = syntax.module_name;

  janus::ast::Program program;
  if (const auto path = file_uri_path(uri); path.has_value()) {
    std::vector<std::filesystem::path> search_paths = module_search_paths;
    search_paths.insert(search_paths.end(), workspace_search_paths.begin(),
                        workspace_search_paths.end());
    search_paths.push_back(path->parent_path());
    janus::frontend::ModuleLoader loader{std::move(search_paths)};
    if (open_documents != nullptr)
      for (const auto &[open_uri, open_source] : *open_documents)
        if (const auto open_path = file_uri_path(open_uri);
            open_path.has_value())
          loader.set_source_override(*open_path, open_source);
    program = loader.load(*path, source);
  } else {
    program = std::move(syntax);
  }

  std::unordered_set<const janus::ast::ValueDeclaration *> entry_declarations;
  for (const janus::ast::FunctionDeclaration &function : program.functions)
    if (function.module_name == entry_module)
      collect_local_declarations(function.body, entry_declarations);
  for (const janus::ast::ClassDeclaration &declaration : program.classes)
    if (declaration.module_name == entry_module) {
      for (const janus::ast::FunctionDeclaration &method : declaration.methods)
        collect_local_declarations(method.body, entry_declarations);
      if (declaration.destructor.has_value())
        collect_local_declarations(declaration.destructor->body,
                                   entry_declarations);
    }

  janus::semantic::Analyzer analyzer;
  const janus::semantic::AnalysisResult analysis =
      analyzer.analyze(program, janus::semantic::AnalysisOptions{
                                    .require_entry_point = false, .target = {}});
  std::unordered_map<std::size_t, std::string> result;
  for (const auto &[declaration, type] : analysis.local_types)
    if (entry_declarations.contains(declaration) &&
        !declaration->declared_type.has_value())
      result.insert_or_assign(declaration->location.offset, type.name());
  return result;
}

std::vector<DocumentSymbol> symbols(
    std::string_view uri, std::string_view source,
    const std::vector<std::filesystem::path> &module_search_paths = {},
    const std::vector<std::filesystem::path> &workspace_search_paths = {},
    const std::unordered_map<std::string, std::string> *open_documents =
        nullptr) {
  using janus::frontend::Token;
  using janus::frontend::TokenKind;
  const std::vector<Token> document_tokens = tokens(source);
  std::vector<DocumentSymbol> result;
  std::optional<std::string> module_name;
  std::unordered_map<std::string, std::string> function_details;
  std::unordered_map<std::size_t, std::string> constant_details;
  std::unordered_map<std::size_t, std::string> analyzed_local_types;
  try {
    janus::frontend::Parser parser{source};
    const janus::ast::Program program = parser.parse_program();
    module_name = program.module_name;
    for (const janus::ast::FunctionDeclaration &function : program.functions)
      function_details.emplace(function.name,
                               "def " + function_signature(function));
    try {
      janus::semantic::Analyzer analyzer;
      const janus::semantic::AnalysisResult analysis =
          analyzer.analyze(program, janus::semantic::AnalysisOptions{
                                        .require_entry_point = false,
                                        .target = {}});
      for (const janus::ast::GlobalDeclaration &global : program.globals) {
      if (!global.declaration.is_constant)
        continue;
      const std::string key = global.module_name.has_value()
                                  ? *global.module_name + "." +
                                        global.declaration.name
                                  : global.declaration.name;
      const auto found = analysis.global_constant_values.find(key);
      if (found == analysis.global_constant_values.end())
        continue;
      std::string value;
      if (const auto *integer =
              std::get_if<std::uint64_t>(&found->second.data))
        value = found->second.type->is_signed()
                    ? std::to_string(static_cast<std::int64_t>(*integer))
                    : std::to_string(*integer);
      else if (const auto *floating = std::get_if<double>(&found->second.data))
        value = std::to_string(*floating);
      else if (const auto *boolean = std::get_if<bool>(&found->second.data))
        value = *boolean ? "true" : "false";
      else if (const auto *character =
                   std::get_if<char32_t>(&found->second.data))
        value = std::to_string(static_cast<std::uint32_t>(*character));
        else if (const auto *text =
                     std::get_if<std::string>(&found->second.data))
          value = "\"" + *text + "\"";
        if (!value.empty())
          constant_details.emplace(
              global.declaration.location.offset,
              " = " + value + "\n\norigin `" + key + "`");
      }
    } catch (const std::exception &) {
    }
    analyzed_local_types = analyze_local_types(
        uri, source, module_search_paths, workspace_search_paths,
        open_documents);
  } catch (const std::exception &) {
  }
  struct Scope {
    std::size_t start;
    std::size_t end;
    std::size_t depth;
  };
  std::vector<Scope> scopes;
  std::vector<std::pair<std::size_t, std::size_t>> scope_stack;
  for (const Token &token : document_tokens) {
    if (token.kind == TokenKind::LeftBrace) {
      scope_stack.emplace_back(token.location.offset, scope_stack.size() + 1);
    } else if (token.kind == TokenKind::RightBrace && !scope_stack.empty()) {
      const auto [start, depth] = scope_stack.back();
      scope_stack.pop_back();
      scopes.push_back(Scope{start, token.location.offset, depth});
    }
  }
  std::size_t brace_depth = 0;
  for (std::size_t index = 0; index + 1 < document_tokens.size(); ++index) {
    const Token &token = document_tokens[index];
    if (token.kind == TokenKind::LeftBrace) {
      ++brace_depth;
      continue;
    }
    if (token.kind == TokenKind::RightBrace) {
      if (brace_depth != 0)
        --brace_depth;
      continue;
    }
    const Token &name = document_tokens[index + 1];
    if (name.kind != TokenKind::Identifier)
      continue;

    std::string detail;
    bool is_parameter = false;
    if (token.kind == TokenKind::Const || token.kind == TokenKind::Val ||
        token.kind == TokenKind::Var) {
      detail = token.kind == TokenKind::Const
                   ? "const "
                   : (token.kind == TokenKind::Val ? "val " : "var ");
      detail += std::string{name.lexeme};
      if (index + 3 < document_tokens.size() &&
          document_tokens[index + 2].kind == TokenKind::Colon &&
          document_tokens[index + 3].kind == TokenKind::Identifier)
        detail += " : " + qualified_type_name(document_tokens, index + 3);
      else if (const auto inferred =
                   analyzed_local_types.find(token.location.offset);
               inferred != analyzed_local_types.end())
        detail += " : " + inferred->second;
      else if (index + 3 < document_tokens.size() &&
               document_tokens[index + 2].kind == TokenKind::Equal)
        if (const auto inferred =
                inferred_literal_type(document_tokens, index + 3))
          detail += " : " + *inferred;
      if (token.kind == TokenKind::Const)
        if (const auto constant = constant_details.find(token.location.offset);
            constant != constant_details.end())
          detail += constant->second;
    } else if (token.kind == TokenKind::Def) {
      const auto signature = function_details.find(std::string{name.lexeme});
      detail = signature == function_details.end()
                   ? "def " + std::string{name.lexeme}
                   : signature->second;
    } else if (token.kind == TokenKind::Class ||
               token.kind == TokenKind::Struct) {
      detail = "class " + std::string{name.lexeme};
    } else if (token.kind == TokenKind::Trait) {
      detail = "trait " + std::string{name.lexeme};
    } else if (token.kind == TokenKind::Enum) {
      detail = "enum " + std::string{name.lexeme};
    } else if ((token.kind == TokenKind::LeftParen ||
                token.kind == TokenKind::Comma) &&
               index + 2 < document_tokens.size() &&
               document_tokens[index + 2].kind == TokenKind::Colon) {
      std::size_t previous = index;
      while (previous != 0 &&
             document_tokens[previous].kind != TokenKind::Def &&
             document_tokens[previous].kind != TokenKind::LeftBrace &&
             document_tokens[previous].kind != TokenKind::RightBrace)
        --previous;
      if (document_tokens[previous].kind != TokenKind::Def)
        continue;
      detail = "parameter " + std::string{name.lexeme};
      if (index + 3 < document_tokens.size() &&
          document_tokens[index + 3].kind == TokenKind::Identifier)
        detail += " : " + qualified_type_name(document_tokens, index + 3);
      is_parameter = true;
    } else {
      continue;
    }
    const bool is_global = brace_depth == 0 && (token.kind == TokenKind::Const ||
                                                token.kind == TokenKind::Val ||
                                                token.kind == TokenKind::Var);
    const bool is_top_level = brace_depth == 0;
    const bool is_private =
        is_top_level && index != 0 &&
        document_tokens[index - 1].kind == TokenKind::Private;
    if (is_private)
      detail = "private " + detail;
    std::size_t scope_start = 0;
    std::size_t scope_end = source.size();
    std::size_t scope_depth = 0;
    for (const Scope &scope : scopes)
      if (name.location.offset > scope.start &&
          name.location.offset < scope.end && scope.depth >= scope_depth) {
        scope_start = scope.start;
        scope_end = scope.end;
        scope_depth = scope.depth;
      }
    if (is_parameter)
      for (const Token &candidate : document_tokens)
        if (candidate.kind == TokenKind::LeftBrace &&
            candidate.location.offset > name.location.offset) {
          for (const Scope &scope : scopes)
            if (scope.start == candidate.location.offset) {
              scope_start = scope.start;
              scope_end = scope.end;
              scope_depth = scope.depth;
              break;
            }
          break;
        }
    const bool symbol_top_level = is_top_level && !is_parameter;
    const std::string identity =
        symbol_top_level
            ? std::string{uri} + "#" + std::string{name.lexeme} + ":" +
                  detail.substr(0, detail.find(' '))
            : std::string{uri} + "#" + std::to_string(name.location.offset) +
                  ":" + std::string{name.lexeme};
    result.push_back(DocumentSymbol{
        identity, std::string{name.lexeme}, std::move(detail), name.location,
        scope_start, scope_end, scope_depth, is_global, symbol_top_level,
        is_private, symbol_top_level ? module_name : std::nullopt});
  }
  return result;
}

struct LocatedIdentifier {
  std::string name;
  janus::SourceLocation location;
  std::optional<std::string> qualifier;
};

LocatedIdentifier
located_identifier(const std::vector<janus::frontend::Token> &document_tokens,
                   std::size_t index) {
  using janus::frontend::TokenKind;
  const janus::frontend::Token &token = document_tokens[index];
  std::optional<std::string> qualifier;
  if (index >= 2 && document_tokens[index - 1].kind == TokenKind::Dot &&
      document_tokens[index - 2].kind == TokenKind::Identifier) {
    qualifier = std::string{document_tokens[index - 2].lexeme};
    std::size_t qualifier_index = index - 2;
    while (qualifier_index >= 2 &&
           document_tokens[qualifier_index - 1].kind == TokenKind::Dot &&
           document_tokens[qualifier_index - 2].kind == TokenKind::Identifier) {
      *qualifier = std::string{document_tokens[qualifier_index - 2].lexeme} +
                   "." + *qualifier;
      qualifier_index -= 2;
    }
  }
  return LocatedIdentifier{std::string{token.lexeme}, token.location,
                           std::move(qualifier)};
}

std::size_t utf8_sequence_length(unsigned char lead) {
  if ((lead & 0x80U) == 0)
    return 1;
  if ((lead & 0xE0U) == 0xC0U)
    return 2;
  if ((lead & 0xF0U) == 0xE0U)
    return 3;
  if ((lead & 0xF8U) == 0xF0U)
    return 4;
  return 1;
}

std::uint32_t utf16_length(std::string_view text) {
  std::uint32_t result = 0;
  for (std::size_t offset = 0; offset < text.size();) {
    const std::size_t length =
        std::min(utf8_sequence_length(static_cast<unsigned char>(text[offset])),
                 text.size() - offset);
    result += length == 4 ? 2 : 1;
    offset += length;
  }
  return result;
}

std::optional<std::size_t>
offset_from_position(std::string_view source, std::uint32_t requested_line,
                     std::uint32_t requested_character) {
  std::size_t offset = 0;
  std::uint32_t line = 0;
  while (line < requested_line) {
    const std::size_t newline = source.find('\n', offset);
    if (newline == std::string_view::npos) {
      if (line + 1 == requested_line && offset <= source.size() &&
          requested_character == 0)
        return source.size();
      return std::nullopt;
    }
    offset = newline + 1;
    ++line;
  }
  std::uint32_t character = 0;
  while (offset < source.size() && source[offset] != '\n' &&
         character < requested_character) {
    const std::size_t length = std::min(
        utf8_sequence_length(static_cast<unsigned char>(source[offset])),
        source.size() - offset);
    const std::uint32_t units = length == 4 ? 2 : 1;
    if (character + units > requested_character)
      return std::nullopt;
    character += units;
    offset += length;
  }
  if (character != requested_character)
    return std::nullopt;
  return offset;
}

llvm::json::Object position_at_offset(std::string_view source,
                                      std::size_t requested_offset) {
  requested_offset = std::min(requested_offset, source.size());
  std::uint32_t line = 0;
  std::size_t line_start = 0;
  for (std::size_t offset = 0; offset < requested_offset; ++offset)
    if (source[offset] == '\n') {
      ++line;
      line_start = offset + 1;
    }
  return position(line, utf16_length(source.substr(
                            line_start, requested_offset - line_start)));
}

std::optional<LocatedIdentifier> identifier_at(std::string_view source,
                                               std::uint32_t line,
                                               std::uint32_t column) {
  using janus::frontend::TokenKind;
  const std::optional<std::size_t> requested =
      offset_from_position(source, line, column);
  if (!requested)
    return std::nullopt;
  const std::vector<janus::frontend::Token> document_tokens = tokens(source);
  for (std::size_t index = 0; index < document_tokens.size(); ++index) {
    const janus::frontend::Token &token = document_tokens[index];
    if (token.kind != TokenKind::Identifier ||
        *requested < token.location.offset ||
        *requested >= token.location.offset + token.lexeme.size())
      continue;
    return located_identifier(document_tokens, index);
  }
  return std::nullopt;
}

llvm::json::Object range(std::string_view source,
                         const janus::SourceLocation &location,
                         std::size_t length) {
  return llvm::json::Object{
      {"start", position_at_offset(source, location.offset)},
      {"end", position_at_offset(source, location.offset + length)},
  };
}

bool valid_identifier(std::string_view name) {
  if (name.empty())
    return false;
  janus::frontend::Lexer lexer{name};
  const janus::frontend::Token token = lexer.next();
  return token.kind == janus::frontend::TokenKind::Identifier &&
         token.lexeme.size() == name.size() &&
         lexer.next().kind == janus::frontend::TokenKind::End;
}

std::string type_reference(const janus::ast::TypeReference &type) {
  std::string result = type.name;
  if (!type.type_arguments.empty()) {
    result += "[";
    for (std::size_t index = 0; index < type.type_arguments.size(); ++index) {
      if (index != 0)
        result += ", ";
      result += type_reference(type.type_arguments[index]);
    }
    result += "]";
  }
  return result;
}

std::string
function_signature(const janus::ast::FunctionDeclaration &function) {
  std::string result = function.name + "(";
  for (std::size_t index = 0; index < function.parameters.size(); ++index) {
    if (index != 0)
      result += ", ";
    if (function.parameters[index].ownership ==
        janus::ast::ParameterOwnership::Borrow)
      result += "borrow ";
    else if (function.parameters[index].ownership ==
             janus::ast::ParameterOwnership::BorrowMutable)
      result += "borrow var ";
    else if (function.parameters[index].ownership ==
             janus::ast::ParameterOwnership::Consume)
      result += "consume ";
    result += function.parameters[index].name + " : " +
              type_reference(function.parameters[index].type);
  }
  result += ") : ";
  if (function.return_ownership == janus::ast::ReturnOwnership::Borrow)
    result += "borrow ";
  else if (function.return_ownership == janus::ast::ReturnOwnership::Owned)
    result += "owned ";
  return result + type_reference(function.return_type);
}

struct CallSite {
  LocatedIdentifier callee;
  std::size_t active_parameter;
  std::optional<LocatedIdentifier> receiver;
};

std::optional<CallSite> call_at(std::string_view source,
                                std::uint32_t requested_line,
                                std::uint32_t requested_column) {
  const std::optional<std::size_t> requested =
      offset_from_position(source, requested_line, requested_column);
  if (!requested)
    return std::nullopt;
  const std::size_t cursor = *requested;
  const std::vector<janus::frontend::Token> document_tokens = tokens(source);
  std::size_t depth = 0;
  std::size_t commas = 0;
  for (std::size_t index = document_tokens.size(); index != 0;) {
    --index;
    const janus::frontend::Token &token = document_tokens[index];
    if (token.location.offset >= cursor)
      continue;
    if (token.kind == janus::frontend::TokenKind::RightParen) {
      ++depth;
    } else if (token.kind == janus::frontend::TokenKind::LeftParen) {
      if (depth != 0) {
        --depth;
        continue;
      }
      if (index == 0 || document_tokens[index - 1].kind !=
                            janus::frontend::TokenKind::Identifier)
        return std::nullopt;
      const std::size_t callee_index = index - 1;
      const LocatedIdentifier callee =
          located_identifier(document_tokens, callee_index);
      std::optional<LocatedIdentifier> receiver;
      if (callee_index >= 2 &&
          document_tokens[callee_index - 1].kind ==
              janus::frontend::TokenKind::Dot &&
          document_tokens[callee_index - 2].kind ==
              janus::frontend::TokenKind::Identifier)
        receiver = located_identifier(document_tokens, callee_index - 2);
      return CallSite{callee, commas, std::move(receiver)};
    } else if (token.kind == janus::frontend::TokenKind::Comma && depth == 0) {
      ++commas;
    }
  }
  return std::nullopt;
}

std::optional<std::string> expected_type_at(std::string_view source,
                                            std::size_t cursor) {
  const std::size_t line_start = source.rfind('\n', cursor) ==
                                         std::string_view::npos
                                     ? 0
                                     : source.rfind('\n', cursor) + 1;
  const std::string_view prefix = source.substr(line_start, cursor - line_start);
  const std::size_t equals = prefix.rfind('=');
  const std::size_t colon = equals == std::string_view::npos
                                ? std::string_view::npos
                                : prefix.rfind(':', equals);
  if (colon == std::string_view::npos)
    return std::nullopt;
  std::string type{prefix.substr(colon + 1, equals - colon - 1)};
  type.erase(type.begin(), std::find_if(type.begin(), type.end(), [](char c) {
               return !std::isspace(static_cast<unsigned char>(c));
             }));
  type.erase(std::find_if(type.rbegin(), type.rend(), [](char c) {
               return !std::isspace(static_cast<unsigned char>(c));
             }).base(),
             type.end());
  return type.empty() ? std::nullopt
                      : std::optional<std::string>{std::move(type)};
}

std::optional<std::size_t> argument_count_at(std::string_view source,
                                             std::size_t cursor) {
  while (cursor < source.size() &&
         (std::isalnum(static_cast<unsigned char>(source[cursor])) ||
          source[cursor] == '_'))
    ++cursor;
  if (cursor == source.size() || source[cursor] != '(')
    return std::nullopt;
  std::size_t depth = 0;
  std::size_t count = 0;
  bool has_argument = false;
  for (++cursor; cursor < source.size(); ++cursor) {
    const char character = source[cursor];
    if (character == '(' || character == '[' || character == '{')
      ++depth;
    else if (character == ')' && depth == 0)
      return has_argument ? count + 1 : 0;
    else if (character == ')' || character == ']' || character == '}')
      --depth;
    else if (character == ',' && depth == 0)
      ++count;
    else if (!std::isspace(static_cast<unsigned char>(character)))
      has_argument = true;
  }
  return std::nullopt;
}

std::optional<std::size_t> generic_argument_count_at(std::string_view source,
                                                     std::size_t cursor) {
  while (cursor < source.size() &&
         (std::isalnum(static_cast<unsigned char>(source[cursor])) ||
          source[cursor] == '_'))
    ++cursor;
  if (cursor == source.size() || source[cursor] != '[')
    return std::nullopt;
  std::size_t depth = 0;
  std::size_t count = 0;
  bool has_argument = false;
  for (++cursor; cursor < source.size(); ++cursor) {
    const char character = source[cursor];
    if (character == '[')
      ++depth;
    else if (character == ']' && depth == 0)
      return has_argument ? count + 1 : 0;
    else if (character == ']')
      --depth;
    else if (character == ',' && depth == 0)
      ++count;
    else if (!std::isspace(static_cast<unsigned char>(character)))
      has_argument = true;
  }
  return std::nullopt;
}

std::optional<std::string>
inferred_literal_type(const std::vector<janus::frontend::Token> &items,
                      std::size_t initializer) {
  using janus::frontend::TokenKind;
  if (initializer >= items.size())
    return std::nullopt;
  switch (items[initializer].kind) {
  case TokenKind::IntegerLiteral:
    return "int";
  case TokenKind::DoubleLiteral:
    return "double";
  case TokenKind::CharacterLiteral:
    return "char";
  case TokenKind::StringLiteral:
    return "string";
  case TokenKind::LeftBracket:
    return "Array";
  case TokenKind::True:
  case TokenKind::False:
    return "bool";
  case TokenKind::New:
    if (initializer + 1 < items.size() &&
        items[initializer + 1].kind == TokenKind::Identifier)
      return std::string{items[initializer + 1].lexeme};
    return std::nullopt;
  default:
    return std::nullopt;
  }
}

bool semantic_keyword(janus::frontend::TokenKind kind) {
  using janus::frontend::TokenKind;
  return kind >= TokenKind::Module && kind <= TokenKind::False &&
         kind != TokenKind::Identifier && kind != TokenKind::IntegerLiteral &&
         kind != TokenKind::DoubleLiteral &&
         kind != TokenKind::CharacterLiteral &&
         kind != TokenKind::StringLiteral &&
         kind != TokenKind::DocumentationComment;
}

bool semantic_operator(janus::frontend::TokenKind kind) {
  using janus::frontend::TokenKind;
  switch (kind) {
  case TokenKind::EqualEqual: case TokenKind::Bang: case TokenKind::BangEqual:
  case TokenKind::Plus: case TokenKind::Minus: case TokenKind::Star:
  case TokenKind::Slash: case TokenKind::Percent: case TokenKind::Less:
  case TokenKind::LessEqual: case TokenKind::Greater: case TokenKind::GreaterEqual:
  case TokenKind::Ampersand: case TokenKind::Pipe: case TokenKind::Caret:
  case TokenKind::ShiftLeft: case TokenKind::ShiftRight: case TokenKind::AmpAmp:
  case TokenKind::PipePipe: case TokenKind::Question:
    return true;
  default:
    return false;
  }
}

std::optional<char> character_before(std::string_view source,
                                     std::uint32_t requested_line,
                                     std::uint32_t requested_column) {
  const auto offset =
      offset_from_position(source, requested_line, requested_column);
  return !offset || *offset == 0 ? std::nullopt
                                 : std::optional<char>{source[*offset - 1]};
}

std::optional<std::string>
member_qualifier_before(std::string_view source, std::uint32_t requested_line,
                        std::uint32_t requested_column) {
  const auto requested =
      offset_from_position(source, requested_line, requested_column);
  if (!requested)
    return std::nullopt;
  const std::size_t cursor = *requested;
  if (cursor == 0 || cursor > source.size() || source[cursor - 1] != '.')
    return std::nullopt;
  std::size_t start = cursor - 1;
  while (start != 0) {
    const char character = source[start - 1];
    const bool identifier = (character >= 'a' && character <= 'z') ||
                            (character >= 'A' && character <= 'Z') ||
                            (character >= '0' && character <= '9') ||
                            character == '_' || character == '.';
    if (!identifier)
      break;
    --start;
  }
  if (start == cursor - 1)
    return std::nullopt;
  return std::string{source.substr(start, cursor - 1 - start)};
}

std::optional<std::filesystem::path> file_uri_path(std::string_view uri) {
  constexpr std::string_view prefix = "file://";
  if (!uri.starts_with(prefix))
    return std::nullopt;

  const std::string_view remainder = uri.substr(prefix.size());
  const std::size_t path_start = remainder.find('/');
  const std::string_view encoded_authority = remainder.substr(0, path_start);
  const std::string_view encoded_path =
      path_start == std::string_view::npos ? std::string_view{}
                                           : remainder.substr(path_start);
  const auto hex_value = [](char character) -> std::optional<unsigned char> {
    if (character >= '0' && character <= '9')
      return static_cast<unsigned char>(character - '0');
    if (character >= 'a' && character <= 'f')
      return static_cast<unsigned char>(character - 'a' + 10);
    if (character >= 'A' && character <= 'F')
      return static_cast<unsigned char>(character - 'A' + 10);
    return std::nullopt;
  };
  const auto decode = [&](std::string_view encoded)
      -> std::optional<std::string> {
    std::string decoded;
    decoded.reserve(encoded.size());
    for (std::size_t index = 0; index < encoded.size(); ++index) {
      if (encoded[index] == '%' && index + 2 < encoded.size()) {
        const std::optional<unsigned char> high = hex_value(encoded[index + 1]);
        const std::optional<unsigned char> low = hex_value(encoded[index + 2]);
        if (high && low) {
          const unsigned char value = (*high << 4U) | *low;
          if (value == 0)
            return std::nullopt;
          decoded.push_back(static_cast<char>(value));
          index += 2;
          continue;
        }
      }
      if (encoded[index] == '%')
        return std::nullopt;
      decoded.push_back(encoded[index]);
    }
    return decoded;
  };
  const std::optional<std::string> authority = decode(encoded_authority);
  const std::optional<std::string> path = decode(encoded_path);
  if (!authority || !path)
    return std::nullopt;
#ifdef _WIN32
  if (!authority->empty() && *authority != "localhost")
    return path->empty() ? std::nullopt
                         : std::optional<std::filesystem::path>{
                               std::filesystem::path{"//" + *authority + *path}};
  std::string decoded = *path;
  if (decoded.size() >= 3 && decoded[0] == '/' &&
      ((decoded[1] >= 'a' && decoded[1] <= 'z') ||
       (decoded[1] >= 'A' && decoded[1] <= 'Z')) &&
      decoded[2] == ':')
    decoded.erase(decoded.begin());
  return decoded.empty() ? std::nullopt
                         : std::optional<std::filesystem::path>{decoded};
#else
  if (!authority->empty() && *authority != "localhost")
    return std::nullopt;
  if (path->empty() || path->front() != '/')
    return std::nullopt;
  return std::filesystem::path{*path};
#endif
}

bool is_canonical_descendant(const std::filesystem::path &path,
                             const std::filesystem::path &root) {
  const auto components_equal = [](const std::filesystem::path &left,
                                   const std::filesystem::path &right) {
#ifdef _WIN32
    std::string left_text = left.generic_string();
    std::string right_text = right.generic_string();
    std::transform(left_text.begin(), left_text.end(), left_text.begin(),
                   [](unsigned char character) {
                     return static_cast<char>(std::tolower(character));
                   });
    std::transform(right_text.begin(), right_text.end(), right_text.begin(),
                   [](unsigned char character) {
                     return static_cast<char>(std::tolower(character));
                   });
    return left_text == right_text;
#else
    return left == right;
#endif
  };
  auto path_component = path.begin();
  for (auto root_component = root.begin(); root_component != root.end();
       ++root_component, ++path_component)
    if (path_component == path.end() ||
        !components_equal(*path_component, *root_component))
      return false;
  return true;
}

std::optional<std::filesystem::path> canonical_janus_file_under_roots(
    const std::filesystem::path &path,
    const std::vector<std::filesystem::path> &roots) {
  if (path.extension() != ".janus")
    return std::nullopt;
  std::error_code error;
  const std::filesystem::path canonical_path =
      std::filesystem::canonical(path, error);
  if (error || canonical_path.extension() != ".janus" ||
      !std::filesystem::is_regular_file(canonical_path, error) || error)
    return std::nullopt;
  for (const std::filesystem::path &root : roots) {
    error.clear();
    const std::filesystem::path canonical_root =
        std::filesystem::canonical(root, error);
    if (!error && std::filesystem::is_directory(canonical_root, error) &&
        !error && is_canonical_descendant(canonical_path, canonical_root))
      return canonical_path;
  }
  return std::nullopt;
}

struct IndexedDocument {
  std::string uri;
  const janus::lsp::DocumentIndex *index;
  std::optional<std::string> module_name;
  std::vector<janus::ast::ImportDeclaration> imports;
  std::unordered_map<std::string, std::string> resolved_import_uris;
  bool file_backed{};
};

bool imports_module(const IndexedDocument &document, std::string_view module,
                    std::string_view uri) {
  const auto resolved = document.resolved_import_uris.find(std::string{module});
  if (resolved != document.resolved_import_uris.end())
    return resolved->second == uri;
  if (document.file_backed)
    return false;
  return std::any_of(document.imports.begin(), document.imports.end(),
                     [&](const janus::ast::ImportDeclaration &import) {
                       return import.module_name == module;
                     });
}

bool qualifier_imports_module(const IndexedDocument &document,
                              std::string_view qualifier,
                              std::string_view module, std::string_view uri) {
  if (!imports_module(document, module, uri))
    return false;
  return std::any_of(document.imports.begin(), document.imports.end(),
                     [&](const janus::ast::ImportDeclaration &import) {
                       return import.module_name == module &&
                              (qualifier == module ||
                               import.module_alias == qualifier);
                     });
}

bool import_exposes(const IndexedDocument &document, std::string_view module,
                    std::string_view symbol,
                    const std::optional<std::string> &qualifier,
                    std::string_view local_name, std::string_view uri) {
  if (!imports_module(document, module, uri))
    return false;
  for (const janus::ast::ImportDeclaration &import : document.imports) {
    if (import.module_name != module)
      continue;
    if (qualifier.has_value())
      return (*qualifier == import.module_name ||
              import.module_alias == qualifier) &&
             symbol == local_name;
    if (!import.is_qualified() && !import.is_selective() &&
        symbol == local_name)
      return true;
    for (const janus::ast::ImportDeclaration::Symbol &item : import.symbols)
      if (item.name == symbol && item.alias.value_or(item.name) == local_name)
        return true;
  }
  return false;
}

struct SemanticIndex {
  std::vector<IndexedDocument> documents;
};

std::string file_uri(const std::filesystem::path &path) {
  std::error_code error;
  std::filesystem::path normalized_path = std::filesystem::weakly_canonical(
      std::filesystem::absolute(path), error);
  if (error)
    normalized_path = std::filesystem::absolute(path).lexically_normal();
  std::string normalized = normalized_path.generic_string();
#ifdef _WIN32
  if (normalized.starts_with("//"))
    normalized.erase(0, 2);
  else if (normalized.size() >= 2 && normalized[1] == ':')
    normalized.insert(normalized.begin(), '/');
#endif
  constexpr char hex[] = "0123456789ABCDEF";
  std::string result = "file://";
  for (const unsigned char character : normalized) {
    const bool unreserved = (character >= 'a' && character <= 'z') ||
                            (character >= 'A' && character <= 'Z') ||
                            (character >= '0' && character <= '9') ||
                            character == '-' || character == '.' ||
                            character == '_' || character == '~' ||
                            character == '/' || character == ':';
    if (unreserved) {
      result.push_back(static_cast<char>(character));
    } else {
      result.push_back('%');
      result.push_back(hex[character >> 4U]);
      result.push_back(hex[character & 0x0FU]);
    }
  }
  return result;
}

bool path_is_within(const std::filesystem::path &path,
                    const std::filesystem::path &directory) {
  const std::filesystem::path normalized_path =
      std::filesystem::absolute(path).lexically_normal();
  const std::filesystem::path normalized_directory =
      std::filesystem::absolute(directory).lexically_normal();
  auto path_part = normalized_path.begin();
  for (auto directory_part = normalized_directory.begin();
       directory_part != normalized_directory.end();
       ++directory_part, ++path_part) {
    if (path_part == normalized_path.end() || *path_part != *directory_part)
      return false;
  }
  return true;
}

std::filesystem::path module_relative_path(std::string_view module) {
  std::filesystem::path relative;
  std::size_t start = 0;
  while (start < module.size()) {
    const std::size_t separator = module.find('.', start);
    relative /= module.substr(start, separator == std::string_view::npos
                                         ? module.size() - start
                                         : separator - start);
    if (separator == std::string_view::npos)
      break;
    start = separator + 1;
  }
  relative += ".janus";
  return relative;
}

bool uri_paths_equal(const std::filesystem::path &left,
                     const std::filesystem::path &right) {
  std::string normalized_left = left.lexically_normal().generic_string();
  std::string normalized_right = right.lexically_normal().generic_string();
#ifdef _WIN32
  const auto lowercase = [](std::string &path) {
    std::transform(path.begin(), path.end(), path.begin(), [](char character) {
      return static_cast<char>(
          std::tolower(static_cast<unsigned char>(character)));
    });
  };
  lowercase(normalized_left);
  lowercase(normalized_right);
#endif
  return normalized_left == normalized_right;
}

std::optional<std::string> resolve_import_uri(
    std::string_view module, const std::filesystem::path &root,
    const std::vector<std::filesystem::path> &search_paths,
    const std::vector<std::string> &buffered_uris) {
  const std::filesystem::path relative = module_relative_path(module);
  std::vector<std::filesystem::path> roots{root};
  roots.insert(roots.end(), search_paths.begin(), search_paths.end());
  for (const std::filesystem::path &candidate_root : roots) {
    const std::filesystem::path candidate_path = candidate_root / relative;
    for (const std::string &buffered_uri : buffered_uris) {
      const std::optional<std::filesystem::path> buffered_path =
          file_uri_path(buffered_uri);
      if (buffered_path && uri_paths_equal(*buffered_path, candidate_path))
        return buffered_uri;
    }
    const std::string candidate_uri = file_uri(candidate_path);
    if (std::filesystem::is_regular_file(candidate_path))
      return candidate_uri;
  }
  return std::nullopt;
}

SemanticIndex build_semantic_index(
    std::string_view active_uri, std::string_view active_source,
    const std::unordered_map<std::string, std::string> &open_documents,
    const std::unordered_set<std::string> &workspace_uris,
    const std::vector<std::filesystem::path> &search_paths,
    std::unordered_map<std::string, janus::lsp::DocumentIndex> &cache) {
  std::unordered_set<std::string> indexed_uris;
  std::vector<std::string> ordered_uris;
  const auto add_document = [&](std::string uri, std::string source) {
    if (!indexed_uris.insert(uri).second)
      return false;
    auto cached = cache.find(uri);
    if (cached == cache.end() || cached->second.source != source) {
      janus::lsp::DocumentIndex document{std::move(source), {}};
      document.symbols = symbols(uri, document.source);
      cache.insert_or_assign(uri, std::move(document));
    }
    ordered_uris.push_back(std::move(uri));
    return true;
  };
  add_document(std::string{active_uri}, std::string{active_source});
  const std::optional<std::filesystem::path> active_path =
      file_uri_path(active_uri);
  const std::optional<std::filesystem::path> project_root =
      active_path ? std::optional<std::filesystem::path>{active_path->parent_path()}
                  : std::nullopt;
  std::vector<std::string> available_uris;
  available_uris.reserve(open_documents.size() + workspace_uris.size());
  for (const auto &[uri, ignored] : open_documents)
    available_uris.push_back(uri);
  std::vector<std::string> buffered_uris = available_uris;
  std::sort(buffered_uris.begin(), buffered_uris.end());
  for (const std::string &uri : workspace_uris)
    available_uris.push_back(uri);
  std::sort(available_uris.begin(), available_uris.end());
  available_uris.erase(
      std::unique(available_uris.begin(), available_uris.end()),
      available_uris.end());
  for (const std::string &uri : available_uris) {
    if (const auto open = open_documents.find(uri);
        open != open_documents.end()) {
      add_document(uri, open->second);
      continue;
    }
    if (const auto cached = cache.find(uri); cached != cache.end())
      add_document(uri, cached->second.source);
  }

  for (std::size_t document_index = 0; document_index < ordered_uris.size();
       ++document_index) {
    const std::string &document_uri = ordered_uris[document_index];
    const auto cached_document = cache.find(document_uri);
    if (cached_document == cache.end())
      continue;
    const std::optional<std::filesystem::path> document_path =
        file_uri_path(document_uri);
    if (!document_path.has_value() || !project_root.has_value())
      continue;
    janus::ast::Program program;
    try {
      janus::frontend::Parser parser{cached_document->second.source};
      program = parser.parse_program();
    } catch (const std::exception &) {
      continue;
    }
    for (const janus::ast::ImportDeclaration &import : program.imports) {
      const std::optional<std::string> imported_uri = resolve_import_uri(
          import.module_name, *project_root, search_paths, buffered_uris);
      if (!imported_uri.has_value())
        continue;
      const std::string &uri = *imported_uri;
      if (indexed_uris.contains(uri))
        continue;
      const auto imported_path = file_uri_path(uri);
      if (!imported_path)
        continue;
      std::ifstream input{*imported_path, std::ios::binary};
      if (!input)
        continue;
      std::string source{std::istreambuf_iterator<char>{input},
                         std::istreambuf_iterator<char>{}};
      add_document(uri, std::move(source));
    }
  }

  SemanticIndex index;
  index.documents.reserve(ordered_uris.size());
  for (std::string &uri : ordered_uris) {
    if (const auto cached = cache.find(uri); cached != cache.end()) {
      std::optional<std::string> module_name;
      std::vector<janus::ast::ImportDeclaration> imports;
      std::unordered_map<std::string, std::string> resolved_import_uris;
      const bool file_backed = file_uri_path(uri).has_value();
      try {
        janus::frontend::Parser parser{cached->second.source};
        janus::ast::Program program = parser.parse_program();
        module_name = std::move(program.module_name);
        imports = std::move(program.imports);
        if (file_backed && project_root)
          for (const janus::ast::ImportDeclaration &import : imports)
            if (const auto imported = resolve_import_uri(
                    import.module_name, *project_root, search_paths,
                    buffered_uris))
              resolved_import_uris.insert_or_assign(import.module_name,
                                                     *imported);
      } catch (const std::exception &) {
      }
      index.documents.push_back(IndexedDocument{std::move(uri), &cached->second,
                                                std::move(module_name),
                                                std::move(imports),
                                                std::move(resolved_import_uris),
                                                file_backed});
    }
  }
  return index;
}

} // namespace

namespace janus::lsp {

Server::Server(std::vector<std::filesystem::path> module_search_paths,
               std::vector<std::filesystem::path> api_index_paths)
    : module_search_paths_{std::move(module_search_paths)} {
  for (const std::filesystem::path &path : api_index_paths)
    if (std::filesystem::is_regular_file(path))
      configured_api_indexes_.push_back(driver::load_api_index(path));
}

void Server::update_api_index(std::string_view uri, std::string_view source) {
  try {
    frontend::Parser parser{source};
    std::vector<ast::Program> programs;
    programs.push_back(parser.parse_program());
    driver::ApiIndex index =
        driver::build_api_index(programs, {"workspace", {}});
    std::erase_if(index.symbols, [](const driver::ApiSymbol &symbol) {
      return symbol.qualified_name != symbol.module + "." + symbol.simple_name;
    });
    document_api_indexes_.insert_or_assign(std::string{uri}, std::move(index));
  } catch (const std::exception &) {
    document_api_indexes_.erase(std::string{uri});
  }
}

driver::ApiIndex
Server::combined_api_index(std::optional<std::string_view> excluded_uri) const {
  std::vector<driver::ApiIndex> indexes = configured_api_indexes_;
  indexes.insert(indexes.end(), dependency_api_indexes_.begin(),
                 dependency_api_indexes_.end());
  for (const auto &[uri, index] : document_api_indexes_)
    if (!excluded_uri || uri != *excluded_uri)
      indexes.push_back(index);
  return driver::merge_api_indexes(indexes);
}

void Server::refresh_workspace_metrics(std::uint64_t startup_milliseconds) {
  WorkspaceIndexMetrics metrics;
  metrics.startup_milliseconds = startup_milliseconds == 0
                                     ? workspace_metrics_.startup_milliseconds
                                     : startup_milliseconds;
  for (const std::string &uri : workspace_uris_) {
    const auto document = index_cache_.find(uri);
    if (document == index_cache_.end())
      continue;
    ++metrics.files;
    metrics.source_bytes += document->second.source.size();
    metrics.estimated_memory_bytes +=
        sizeof(DocumentIndex) + document->second.source.size();
    for (const IndexedSymbol &symbol : document->second.symbols) {
      ++metrics.symbols;
      metrics.estimated_memory_bytes += sizeof(IndexedSymbol) +
                                        symbol.id.size() + symbol.name.size() +
                                        symbol.detail.size();
      if (symbol.module_name.has_value())
        metrics.estimated_memory_bytes += symbol.module_name->size();
    }
  }
  workspace_metrics_ = metrics;
}

void Server::index_workspace_file(const std::filesystem::path &path,
                                  bool dependency) {
  if (path.extension() != ".janus" || !std::filesystem::is_regular_file(path))
    return;
  const std::string uri = file_uri(path);
  std::string source;
  if (const auto open = documents_.find(uri); open != documents_.end()) {
    source = open->second;
  } else {
    std::ifstream input{path, std::ios::binary};
    if (!input)
      return;
    source.assign(std::istreambuf_iterator<char>{input},
                  std::istreambuf_iterator<char>{});
  }
  DocumentIndex index{std::move(source), {}};
  index.symbols = symbols(uri, index.source, module_search_paths_,
                          workspace_search_paths_, &documents_);
  update_api_index(uri, index.source);
  index_cache_.insert_or_assign(uri, std::move(index));
  workspace_uris_.insert(uri);
  if (dependency)
    dependency_uris_.insert(uri);
  else
    dependency_uris_.erase(uri);
}

void Server::remove_workspace_file(std::string_view uri) {
  workspace_uris_.erase(std::string{uri});
  dependency_uris_.erase(std::string{uri});
  if (!documents_.contains(std::string{uri}))
    index_cache_.erase(std::string{uri});
  document_api_indexes_.erase(std::string{uri});
  refresh_workspace_metrics();
}

void Server::initialize_workspace(
    const std::vector<std::filesystem::path> &roots) {
  const auto started = std::chrono::steady_clock::now();
  for (const std::string &uri : workspace_uris_)
    if (!documents_.contains(uri))
      index_cache_.erase(uri);
  workspace_uris_.clear();
  dependency_uris_.clear();
  workspace_roots_.clear();
  dependency_roots_.clear();
  dependency_api_indexes_.clear();
  document_api_indexes_.clear();
  workspace_search_paths_.clear();

  const auto add_search_path = [&](const std::filesystem::path &path) {
    const std::filesystem::path normalized =
        std::filesystem::absolute(path).lexically_normal();
    if (std::filesystem::is_directory(normalized) &&
        std::find(workspace_search_paths_.begin(),
                  workspace_search_paths_.end(),
                  normalized) == workspace_search_paths_.end())
      workspace_search_paths_.push_back(normalized);
  };
  const auto scan_directory = [&](const std::filesystem::path &directory,
                                  bool dependency) {
    if (!std::filesystem::is_directory(directory))
      return;
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator
             iterator{directory,
                      std::filesystem::directory_options::
                          skip_permission_denied,
                      error},
         end;
         iterator != end; iterator.increment(error)) {
      if (error) {
        error.clear();
        continue;
      }
      if (iterator->is_regular_file(error))
        index_workspace_file(iterator->path(), dependency);
      error.clear();
    }
  };

  std::unordered_set<std::string> visited_manifests;
  const auto scan_package = [&](const auto &self,
                                const std::filesystem::path &root,
                                bool dependency) -> void {
    const std::filesystem::path normalized =
        std::filesystem::absolute(root).lexically_normal();
    const std::filesystem::path manifest_path = normalized / "janus.toml";
    const std::string identity = manifest_path.generic_string();
    if (!visited_manifests.insert(identity).second)
      return;
    if (dependency)
      dependency_roots_.push_back(normalized);
    add_search_path(normalized / "src");
    bool has_dependency_index = false;
    if (dependency) {
      for (const std::filesystem::path &candidate :
           {normalized / "target/doc/api-index.json",
            normalized / "api-index.json", normalized / "docs/api-index.json"})
        if (std::filesystem::is_regular_file(candidate)) {
          try {
            dependency_api_indexes_.push_back(driver::load_api_index(candidate));
            has_dependency_index = true;
          } catch (const std::exception &) {
          }
          break;
        }
    }
    if (!has_dependency_index)
      scan_directory(normalized / "src", dependency);
    if (!dependency)
      scan_directory(normalized / "tests", false);
    if (!std::filesystem::is_regular_file(manifest_path))
      return;
    try {
      const driver::Manifest manifest = driver::load_manifest(manifest_path);
      if (!has_dependency_index)
        index_workspace_file(manifest.entry_path(), dependency);
      for (const driver::Dependency &child : manifest.dependencies)
        if (!child.path.empty())
          self(self, (manifest.root() / child.path).lexically_normal(), true);
      if (std::filesystem::is_regular_file(manifest.root() / "janus.lock")) {
        try {
          for (const std::filesystem::path &search_path :
               driver::resolve_dependencies(
                   manifest, driver::DependencyOptions{true, true})) {
            add_search_path(search_path);
            const std::filesystem::path dependency_root = search_path.parent_path();
            bool loaded = false;
            for (const std::filesystem::path &candidate :
                 {dependency_root / "target/doc/api-index.json",
                  dependency_root / "api-index.json",
                  dependency_root / "docs/api-index.json"})
              if (std::filesystem::is_regular_file(candidate)) {
                try {
                  dependency_api_indexes_.push_back(
                      driver::load_api_index(candidate));
                  loaded = true;
                } catch (const std::exception &) {
                }
                break;
              }
            if (!loaded)
              scan_directory(search_path, true);
            dependency_roots_.push_back(dependency_root);
          }
        } catch (const std::exception &) {
        }
      }
    } catch (const std::exception &) {
    }
  };

  for (const std::filesystem::path &root : roots) {
    const std::filesystem::path normalized =
        std::filesystem::absolute(root).lexically_normal();
    if (!std::filesystem::is_directory(normalized))
      continue;
    workspace_roots_.push_back(normalized);
    scan_package(scan_package, normalized, false);
  }
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started);
  refresh_workspace_metrics(static_cast<std::uint64_t>(elapsed.count()) +
                            UINT64_C(1));
}

std::string Server::diagnostics(std::string_view uri,
                                std::string_view source) const {
  llvm::json::Array items;
  for (const Diagnostic &diagnostic : analyze_document(uri, source)) {
    const SourceLocation location = diagnostic.primary_location;
    const int severity = diagnostic.severity == DiagnosticSeverity::Error ? 1
                         : diagnostic.severity == DiagnosticSeverity::Warning
                             ? 2
                             : 3;
    std::string message = diagnostic.message;
    for (const std::string &note : diagnostic.notes)
      message += "\n\nnote: " + note;

    llvm::json::Array related_information;
    for (const DiagnosticLocation &secondary : diagnostic.secondary_locations) {
      related_information.emplace_back(llvm::json::Object{
          {"location",
           llvm::json::Object{
               {"uri", std::string{uri}},
               {"range", range(source, secondary.location, 1)},
           }},
          {"message", secondary.label},
      });
    }

    llvm::json::Array actions;
    for (const DiagnosticSuggestion &suggestion : diagnostic.suggestions) {
      actions.emplace_back(llvm::json::Object{
          {"title", suggestion.message},
          {"range",
           llvm::json::Object{
               {"start",
                position_at_offset(source, suggestion.range.start.offset)},
               {"end", position_at_offset(source, suggestion.range.end.offset)},
           }},
          {"newText", suggestion.replacement},
          {"isPreferred", true},
      });
    }

    llvm::json::Object item{
        {"range",
         llvm::json::Object{
             {"start", position_at_offset(source, location.offset)},
             {"end", position_at_offset(
                         source, location.offset < source.size()
                                     ? location.offset +
                                           utf8_sequence_length(
                                               static_cast<unsigned char>(
                                                   source[location.offset]))
                                     : location.offset)},
         }},
        {"severity", severity},
        {"code", std::string{diagnostic_code_name(diagnostic.code)}},
        {"source", "janus"},
        {"message", std::move(message)},
    };
    if (!related_information.empty())
      item.insert({"relatedInformation", std::move(related_information)});
    if (!actions.empty())
      item.insert(
          {"data", llvm::json::Object{{"actions", std::move(actions)}}});
    items.emplace_back(std::move(item));
  }

  return publish_diagnostics(uri, std::move(items));
}

std::vector<Diagnostic>
Server::analyze_document(std::string_view uri, std::string_view source) const {
  try {
    ast::Program program;
    if (const std::optional<std::filesystem::path> path = file_uri_path(uri)) {
      std::vector<std::filesystem::path> search_paths = module_search_paths_;
      search_paths.insert(search_paths.end(), workspace_search_paths_.begin(),
                          workspace_search_paths_.end());
      frontend::ModuleLoader loader{std::move(search_paths)};
      for (const auto &[open_uri, open_source] : documents_)
        if (const auto open_path = file_uri_path(open_uri);
            open_path.has_value())
          loader.set_source_override(*open_path, open_source);
      program = loader.load(*path, source);
    } else {
      frontend::Parser parser{source};
      program = parser.parse_program();
    }
    const bool is_module = program.module_name.has_value();
    return semantic::Analyzer{}
        .analyze(program, semantic::AnalysisOptions{
                              .require_entry_point = !is_module, .target = {}})
        .diagnostics;
  } catch (const CompileError &error) {
    return error.diagnostics();
  } catch (const std::exception &error) {
    return {Diagnostic{
        DiagnosticSeverity::Error,
        DiagnosticCode::Unclassified,
        error.what(),
        SourceLocation{},
        {},
        {},
        {},
    }};
  }
  return {};
}

namespace {

std::size_t import_insertion_offset(std::string_view source) {
  std::size_t insertion = 0;
  std::size_t line_start = 0;
  while (line_start < source.size()) {
    const std::size_t newline = source.find('\n', line_start);
    const std::size_t line_end =
        newline == std::string_view::npos ? source.size() : newline;
    std::string_view line = source.substr(line_start, line_end - line_start);
    while (!line.empty() &&
           std::isspace(static_cast<unsigned char>(line.front())))
      line.remove_prefix(1);
    if (line.starts_with("module ") || line.starts_with("import "))
      insertion = newline == std::string_view::npos ? line_end : newline + 1;
    else if (!line.empty())
      break;
    line_start =
        newline == std::string_view::npos ? source.size() : newline + 1;
  }
  return insertion;
}

std::optional<std::size_t> matching_match_brace(std::string_view source,
                                                std::size_t match_offset) {
  using janus::frontend::TokenKind;
  bool saw_match = false;
  bool saw_brace = false;
  std::size_t depth = 0;
  for (const janus::frontend::Token &token : tokens(source)) {
    if (!saw_match) {
      saw_match = token.kind == TokenKind::Match &&
                  token.location.offset == match_offset;
      continue;
    }
    if (token.kind == TokenKind::LeftBrace) {
      saw_brace = true;
      ++depth;
    } else if (token.kind == TokenKind::RightBrace && saw_brace) {
      if (--depth == 0)
        return token.location.offset;
    }
  }
  return std::nullopt;
}

llvm::json::Object workspace_edit(std::string_view uri,
                                  llvm::json::Object edit_range,
                                  std::string new_text) {
  llvm::json::Array edits;
  edits.emplace_back(llvm::json::Object{
      {"range", std::move(edit_range)},
      {"newText", std::move(new_text)},
  });
  llvm::json::Object changes;
  changes.insert({std::string{uri}, std::move(edits)});
  return llvm::json::Object{{"changes", std::move(changes)}};
}

} // namespace

std::vector<std::string> Server::handle(std::string_view message) {
  llvm::Expected<llvm::json::Value> parsed = llvm::json::parse(message);
  if (!parsed)
    return {error_response(nullptr, -32700, "Parse error")};
  llvm::json::Object *request = parsed->getAsObject();
  if (request == nullptr)
    return {error_response(nullptr, -32600, "Invalid Request")};

  const std::optional<llvm::StringRef> method = request->getString("method");
  if (!method && request->get("id") != nullptr &&
      (request->get("result") != nullptr || request->get("error") != nullptr))
    return {};
  if (!method)
    return {error_response(request_id(*request), -32600, "Invalid Request")};
  const llvm::json::Object *params = request->getObject("params");

  if (*method == "initialize") {
    std::vector<std::filesystem::path> roots;
    if (params != nullptr) {
      if (const llvm::json::Array *folders =
              params->getArray("workspaceFolders")) {
        for (const llvm::json::Value &folder : *folders)
          if (const llvm::json::Object *entry = folder.getAsObject())
            if (const std::optional<llvm::StringRef> folder_uri =
                    entry->getString("uri"))
              if (const auto path = file_uri_path(*folder_uri))
                roots.push_back(*path);
      }
      if (roots.empty())
        if (const std::optional<llvm::StringRef> root_uri =
                params->getString("rootUri"))
          if (const auto path = file_uri_path(*root_uri))
            roots.push_back(*path);
      if (roots.empty())
        if (const std::optional<llvm::StringRef> root_path =
                params->getString("rootPath"))
          roots.emplace_back(root_path->str());
    }
    initialize_workspace(roots);
    return {response(
        request_id(*request),
        llvm::json::Object{
            {"capabilities",
             llvm::json::Object{
                 {"textDocumentSync", llvm::json::Object{{"openClose", true},
                                                         {"change", 1},
                                                         {"save", true}}},
                 {"hoverProvider", true},
                 {"definitionProvider", true},
                 {"referencesProvider", true},
                 {"renameProvider",
                  llvm::json::Object{{"prepareProvider", true}}},
                 {"signatureHelpProvider",
                  llvm::json::Object{
                      {"triggerCharacters", llvm::json::Array{"(", ","}},
                      {"retriggerCharacters", llvm::json::Array{","}},
                  }},
                 {"semanticTokensProvider",
                  llvm::json::Object{
                      {"legend",
                       llvm::json::Object{
                           {"tokenTypes",
                            llvm::json::Array{"namespace", "type", "class",
                                              "enum", "interface", "function",
                                              "method", "variable", "parameter",
                                              "property", "keyword", "string",
                                              "number", "operator"}},
                           {"tokenModifiers",
                            llvm::json::Array{"declaration", "readonly",
                                              "static"}},
                       }},
                      {"full", true},
                      {"range", false},
                  }},
                 {"inlayHintProvider", true},
                 {"implementationProvider", true},
                 {"codeActionProvider",
                  llvm::json::Object{
                      {"codeActionKinds", llvm::json::Array{"quickfix"}},
                  }},
                 {"workspaceSymbolProvider", true},
                 {"completionProvider",
                  llvm::json::Object{
                      {"triggerCharacters", llvm::json::Array{".", ":"}},
                  }},
                 {"documentFormattingProvider", true},
                 {"workspace",
                  llvm::json::Object{
                      {"workspaceFolders",
                       llvm::json::Object{{"supported", true},
                                          {"changeNotifications", true}}},
                  }},
             }},
            {"serverInfo", llvm::json::Object{{"name", "janus-lsp"},
                                              {"version", janus::build::display_version}}},
        })};
  }
  if (*method == "shutdown") {
    shutdown_ = true;
    return {response(request_id(*request), nullptr)};
  }
  if (*method == "exit")
    return {};

  if (*method == "initialized") {
    std::vector<std::string> notifications{serialize(llvm::json::Object{
        {"jsonrpc", "2.0"},
        {"id", "janus-watch-files"},
        {"method", "client/registerCapability"},
        {"params",
         llvm::json::Object{
             {"registrations",
              llvm::json::Array{llvm::json::Object{
                  {"id", "janus-workspace-files"},
                  {"method", "workspace/didChangeWatchedFiles"},
                  {"registerOptions",
                   llvm::json::Object{
                       {"watchers",
                        llvm::json::Array{
                            llvm::json::Object{{"globPattern", "**/*.janus"},
                                               {"kind", 7}},
                            llvm::json::Object{{"globPattern", "**/janus.toml"},
                                               {"kind", 7}},
                        }},
                   }},
              }}},
         }},
    })};
    for (const std::string &indexed_uri : workspace_uris_) {
      if (dependency_uris_.contains(indexed_uri))
        continue;
      const auto indexed = index_cache_.find(indexed_uri);
      if (indexed != index_cache_.end() &&
          !analyze_document(indexed_uri, indexed->second.source).empty())
        notifications.push_back(
            diagnostics(indexed_uri, indexed->second.source));
    }
    return notifications;
  }

  if (*method == "workspace/didChangeConfiguration") {
    if (params != nullptr)
      if (const llvm::json::Object *settings = params->getObject("settings"))
        if (const llvm::json::Object *janus = settings->getObject("janus"))
          if (const llvm::json::Object *hints = janus->getObject("inlayHints"))
            if (const std::optional<bool> enabled =
                    hints->getBoolean("inferredTypes"))
              inferred_type_hints_ = *enabled;
    return {};
  }

  if (*method == "workspace/didChangeWorkspaceFolders" && params != nullptr) {
    std::vector<std::filesystem::path> roots = workspace_roots_;
    if (const llvm::json::Object *event = params->getObject("event")) {
      if (const llvm::json::Array *removed = event->getArray("removed"))
        for (const llvm::json::Value &folder : *removed)
          if (const llvm::json::Object *entry = folder.getAsObject())
            if (const std::optional<llvm::StringRef> folder_uri =
                    entry->getString("uri"))
              if (const auto path = file_uri_path(*folder_uri)) {
                const std::filesystem::path normalized =
                    std::filesystem::absolute(*path).lexically_normal();
                std::erase(roots, normalized);
              }
      if (const llvm::json::Array *added = event->getArray("added"))
        for (const llvm::json::Value &folder : *added)
          if (const llvm::json::Object *entry = folder.getAsObject())
            if (const std::optional<llvm::StringRef> folder_uri =
                    entry->getString("uri"))
              if (const auto path = file_uri_path(*folder_uri)) {
                const std::filesystem::path normalized =
                    std::filesystem::absolute(*path).lexically_normal();
                if (std::find(roots.begin(), roots.end(), normalized) ==
                    roots.end())
                  roots.push_back(normalized);
              }
    }
    initialize_workspace(roots);
    return {};
  }

  if (*method == "workspace/didChangeWatchedFiles" && params != nullptr) {
    bool rebuild = false;
    std::vector<std::string> notifications;
    if (const llvm::json::Array *changes = params->getArray("changes")) {
      for (const llvm::json::Value &change : *changes) {
        const llvm::json::Object *entry = change.getAsObject();
        if (entry == nullptr)
          continue;
        const std::optional<llvm::StringRef> changed_uri =
            entry->getString("uri");
        const std::optional<std::int64_t> kind = entry->getInteger("type");
        if (!changed_uri || !kind)
          continue;
        const auto path = file_uri_path(*changed_uri);
        if (!path)
          continue;
        const bool project =
            std::any_of(workspace_roots_.begin(), workspace_roots_.end(),
                        [&](const std::filesystem::path &root) {
                          return path_is_within(*path, root);
                        });
        const bool dependency =
            std::any_of(dependency_roots_.begin(), dependency_roots_.end(),
                        [&](const std::filesystem::path &root) {
                          return path_is_within(*path, root);
                        });
        if (!project && !dependency)
          continue;
        if (path->filename() == "janus.toml") {
          rebuild = true;
          continue;
        }
        if (path->extension() != ".janus")
          continue;
        if (*kind == 3) {
          remove_workspace_file(file_uri(*path));
          notifications.push_back(publish_diagnostics(file_uri(*path), {}));
          continue;
        }
        index_workspace_file(*path, dependency);
        const std::string changed_uri_normalized = file_uri(*path);
        if (!dependency)
          if (const auto indexed = index_cache_.find(changed_uri_normalized);
              indexed != index_cache_.end())
            notifications.push_back(
                diagnostics(changed_uri_normalized, indexed->second.source));
      }
    }
    if (rebuild) {
      const std::vector<std::filesystem::path> roots = workspace_roots_;
      initialize_workspace(roots);
    } else {
      refresh_workspace_metrics();
    }
    return notifications;
  }

  const llvm::json::Object *text_document =
      params == nullptr ? nullptr : params->getObject("textDocument");
  std::optional<std::string> uri;
  if (text_document != nullptr)
    if (const std::optional<llvm::StringRef> requested_uri =
            text_document->getString("uri")) {
      if (const auto path = file_uri_path(*requested_uri);
          path && path->is_absolute())
        uri = file_uri(*path);
      else
        uri = requested_uri->str();
    }

  const auto refresh_open_document_symbols =
      [&](std::string_view changed_uri) {
        for (const auto &[open_uri, open_source] : documents_) {
          if (open_uri == changed_uri)
            continue;
          const auto cached = index_cache_.find(open_uri);
          if (cached != index_cache_.end())
            cached->second.symbols =
                symbols(open_uri, open_source, module_search_paths_,
                        workspace_search_paths_, &documents_);
        }
      };

  if (*method == "textDocument/didOpen" && uri) {
    const std::optional<llvm::StringRef> text =
        text_document->getString("text");
    if (text) {
      documents_.insert_or_assign(*uri, text->str());
      if (const std::optional<std::int64_t> version =
              text_document->getInteger("version"))
        document_versions_.insert_or_assign(*uri, *version);
      DocumentIndex index{text->str(), {}};
      index.symbols = symbols(*uri, index.source, module_search_paths_,
                                workspace_search_paths_, &documents_);
      update_api_index(*uri, index.source);
      index_cache_.insert_or_assign(*uri, std::move(index));
      refresh_open_document_symbols(*uri);
      if (workspace_uris_.contains(*uri))
        refresh_workspace_metrics();
      return {diagnostics(*uri, *text)};
    }
    return {};
  }
  if (*method == "textDocument/didChange" && uri && params != nullptr) {
    const llvm::json::Array *changes = params->getArray("contentChanges");
    if (changes != nullptr && !changes->empty()) {
      const llvm::json::Object *change = changes->back().getAsObject();
      const std::optional<llvm::StringRef> text =
          change == nullptr ? std::nullopt : change->getString("text");
      if (text) {
        documents_.insert_or_assign(*uri, text->str());
        if (const std::optional<std::int64_t> version =
                text_document->getInteger("version"))
          document_versions_.insert_or_assign(*uri, *version);
        DocumentIndex index{text->str(), {}};
        index.symbols = symbols(*uri, index.source, module_search_paths_,
                                workspace_search_paths_, &documents_);
        update_api_index(*uri, index.source);
        index_cache_.insert_or_assign(*uri, std::move(index));
        refresh_open_document_symbols(*uri);
        if (workspace_uris_.contains(*uri))
          refresh_workspace_metrics();
        return {diagnostics(*uri, *text)};
      }
    }
    return {};
  }
  if (*method == "textDocument/didSave" && uri) {
    if (params != nullptr)
      if (const std::optional<llvm::StringRef> text = params->getString("text"))
        documents_.insert_or_assign(*uri, text->str());
    if (const auto path = file_uri_path(*uri)) {
      const bool dependency = dependency_uris_.contains(*uri);
      index_workspace_file(*path, dependency);
      refresh_workspace_metrics();
    }
    return {};
  }
  if (*method == "textDocument/didClose" && uri) {
    documents_.erase(*uri);
    document_versions_.erase(*uri);
    if (workspace_uris_.contains(*uri)) {
      if (const auto path = file_uri_path(*uri))
        index_workspace_file(*path, dependency_uris_.contains(*uri));
      refresh_workspace_metrics();
    } else {
      index_cache_.erase(*uri);
      document_api_indexes_.erase(*uri);
    }
    refresh_open_document_symbols(*uri);
    if (const auto indexed = index_cache_.find(*uri);
        indexed != index_cache_.end())
      return {diagnostics(*uri, indexed->second.source)};
    return {publish_diagnostics(*uri, {})};
  }

  if (*method == "janus/workspaceIndexStats") {
    return {response(
        request_id(*request),
        llvm::json::Object{
            {"files", static_cast<std::int64_t>(workspace_metrics_.files)},
            {"symbols", static_cast<std::int64_t>(workspace_metrics_.symbols)},
            {"sourceBytes",
             static_cast<std::int64_t>(workspace_metrics_.source_bytes)},
            {"estimatedMemoryBytes",
             static_cast<std::int64_t>(
                 workspace_metrics_.estimated_memory_bytes)},
            {"startupMilliseconds",
             static_cast<std::int64_t>(
                 workspace_metrics_.startup_milliseconds)},
        })};
  }

  if (*method == "workspace/symbol") {
    std::string query;
    if (params != nullptr)
      if (const std::optional<llvm::StringRef> requested =
              params->getString("query"))
        query = requested->lower();
    struct WorkspaceSymbol {
      std::string uri;
      DocumentSymbol symbol;
    };
    std::vector<WorkspaceSymbol> matches;
    for (const std::string &indexed_uri : workspace_uris_) {
      const auto document = index_cache_.find(indexed_uri);
      if (document == index_cache_.end())
        continue;
      for (const DocumentSymbol &symbol : document->second.symbols) {
        std::string name = symbol.name;
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char character) {
                         return static_cast<char>(std::tolower(character));
                       });
        if (symbol.is_top_level && !symbol.is_private &&
            (query.empty() || name.find(query) != std::string::npos))
          matches.push_back(WorkspaceSymbol{indexed_uri, symbol});
      }
    }
    std::sort(matches.begin(), matches.end(),
              [](const WorkspaceSymbol &left, const WorkspaceSymbol &right) {
                if (left.symbol.name != right.symbol.name)
                  return left.symbol.name < right.symbol.name;
                return left.uri < right.uri;
              });
    llvm::json::Array result;
    for (const WorkspaceSymbol &match : matches) {
      const std::int64_t kind =
          match.symbol.detail.starts_with("def ") ? 12 : 13;
      result.emplace_back(llvm::json::Object{
          {"name", match.symbol.name},
          {"kind", kind},
          {"location",
           llvm::json::Object{
               {"uri", match.uri},
               {"range",
                range(index_cache_.at(match.uri).source, match.symbol.location,
                      match.symbol.name.size())},
           }},
      });
    }
    return {response(request_id(*request), std::move(result))};
  }

  const auto open_document = uri ? documents_.find(*uri) : documents_.end();
  const bool source_request =
      *method == "textDocument/codeAction" ||
      *method == "textDocument/formatting" ||
      *method == "textDocument/semanticTokens/full" ||
      *method == "textDocument/inlayHint";
  std::string disk_source;
  const std::string *document_source = nullptr;
  if (source_request && uri) {
    if (open_document != documents_.end()) {
      document_source = &open_document->second;
    } else {
      std::vector<std::filesystem::path> allowed_roots = workspace_roots_;
      allowed_roots.insert(allowed_roots.end(), dependency_roots_.begin(),
                           dependency_roots_.end());
      allowed_roots.insert(allowed_roots.end(), module_search_paths_.begin(),
                           module_search_paths_.end());
      allowed_roots.insert(allowed_roots.end(),
                           workspace_search_paths_.begin(),
                           workspace_search_paths_.end());
      const auto path = file_uri_path(*uri);
      const auto canonical_path =
          path ? canonical_janus_file_under_roots(*path, allowed_roots)
               : std::nullopt;
      if (canonical_path) {
        if (const auto indexed = index_cache_.find(*uri);
            indexed != index_cache_.end()) {
          document_source = &indexed->second.source;
        } else {
          std::ifstream input{*canonical_path, std::ios::binary};
          if (input) {
            disk_source.assign(std::istreambuf_iterator<char>{input},
                               std::istreambuf_iterator<char>{});
            document_source = &disk_source;
          }
        }
      }
    }
  }
  if (*method == "textDocument/codeAction" && uri) {
    llvm::json::Array actions;
    if (document_source == nullptr)
      return {response(request_id(*request), std::move(actions))};
    const std::string_view source = *document_source;
    const auto add_action = [&](std::string title,
                                llvm::json::Object edit_range,
                                std::string new_text, bool preferred) {
      actions.emplace_back(llvm::json::Object{
          {"title", std::move(title)},
          {"kind", "quickfix"},
          {"isPreferred", preferred},
          {"edit",
           workspace_edit(*uri, std::move(edit_range), std::move(new_text))},
      });
    };

    for (const Diagnostic &diagnostic : analyze_document(*uri, source)) {
      for (const DiagnosticSuggestion &suggestion : diagnostic.suggestions) {
        add_action(suggestion.message,
                   llvm::json::Object{
                       {"start", position_at_offset(
                                     source, suggestion.range.start.offset)},
                       {"end", position_at_offset(source,
                                                  suggestion.range.end.offset)},
                   },
                   suggestion.replacement, true);
      }

      if (diagnostic.code == DiagnosticCode::AnalyzerUnknownValue ||
          diagnostic.message.starts_with("unknown function '")) {
        std::optional<std::string> unknown_name;
        for (const frontend::Token &token : tokens(source))
          if (token.kind == frontend::TokenKind::Identifier &&
              token.location.offset == diagnostic.primary_location.offset) {
            unknown_name = std::string{token.lexeme};
            break;
          }
        std::unordered_map<std::string, std::string> origins;
        const janus::driver::ApiIndex action_index = combined_api_index(*uri);
        if (unknown_name)
          for (const auto &candidate : janus::driver::search_api(
                   action_index, {*unknown_name}))
            if (candidate.score >= 9000)
              origins.emplace(candidate.symbol->package + "\n" +
                                  candidate.symbol->required_import,
                              candidate.symbol->required_import);
        if (origins.size() == 1) {
          const std::string &module = origins.begin()->second;
          bool exposed = false;
          bool edited_selective_import = false;
          bool has_module_import = false;
          try {
            frontend::Parser parser{source};
            const ast::Program program = parser.parse_program();
            for (const ast::ImportDeclaration &import : program.imports) {
              if (import.module_name != module)
                continue;
              has_module_import = true;
              if (import.is_qualified()) {
                exposed = true;
                break;
              }
              if (!import.is_qualified() && !import.is_selective()) {
                exposed = true;
                break;
              }
              if (import.is_selective()) {
                const auto existing = std::find_if(
                    import.symbols.begin(), import.symbols.end(),
                    [&](const ast::ImportDeclaration::Symbol &symbol) {
                      return symbol.name == *unknown_name;
                    });
                if (existing != import.symbols.end()) {
                  exposed = !existing->alias.has_value();
                  break;
                }
                const std::size_t line_start =
                    source.rfind('\n', import.location.offset) ==
                            std::string_view::npos
                        ? 0
                        : source.rfind('\n', import.location.offset) + 1;
                const std::size_t line_end =
                    source.find('\n', import.location.offset);
                const std::size_t closing =
                    source.find('}', import.location.offset);
                if (closing != std::string_view::npos &&
                    (line_end == std::string_view::npos ||
                     closing < line_end)) {
                  const std::size_t end =
                      line_end == std::string_view::npos ? source.size()
                                                         : line_end;
                  add_action(
                      "Import `" + *unknown_name + "` from `" + module + "`",
                      llvm::json::Object{
                          {"start", position_at_offset(source, line_start)},
                          {"end", position_at_offset(source, end)},
                      },
                      std::string{source.substr(line_start,
                                                closing - line_start)} +
                          ", " + *unknown_name + "}",
                      false);
                  edited_selective_import = true;
                }
                break;
              }
            }
          } catch (const std::exception &) {
          }
          if (!exposed && !edited_selective_import) {
            const std::size_t insertion = import_insertion_offset(source);
            add_action("Import module `" + module + "`",
                       llvm::json::Object{
                           {"start", position_at_offset(source, insertion)},
                           {"end", position_at_offset(source, insertion)},
                       },
                       has_module_import
                           ? "import " + module + ".{" + *unknown_name + "}\n"
                           : "import " + module + "\n",
                       false);
          }
        }
      }

      constexpr std::string_view missing_marker = ": missing case(s): ";
      if (const std::size_t marker = diagnostic.message.find(missing_marker);
          marker != std::string::npos) {
        const std::optional<std::size_t> insertion =
            matching_match_brace(source, diagnostic.primary_location.offset);
        if (!insertion)
          continue;
        const std::string cases =
            diagnostic.message.substr(marker + missing_marker.size());
        std::string replacement;
        std::size_t start = 0;
        while (start < cases.size()) {
          const std::size_t comma = cases.find(", ", start);
          const std::string name =
              cases.substr(start, comma == std::string::npos ? std::string::npos
                                                             : comma - start);
          if (!replacement.empty())
            replacement += ", ";
          replacement += name + " => 0";
          if (comma == std::string::npos)
            break;
          start = comma + 2;
        }
        if (!replacement.empty()) {
          const std::size_t previous =
              source.substr(0, *insertion).find_last_not_of(" \t\r\n");
          const bool needs_separator =
              previous != std::string_view::npos && source[previous] != '{';
          add_action("Add missing match branches",
                     llvm::json::Object{
                         {"start", position_at_offset(source, *insertion)},
                         {"end", position_at_offset(source, *insertion)},
                     },
                     (needs_separator ? ", " : "") + replacement + " ", false);
        }
      }
    }
    return {response(request_id(*request), std::move(actions))};
  }
  if (*method == "textDocument/formatting" && uri) {
    if (document_source == nullptr)
      return {response(request_id(*request), llvm::json::Array{})};
    driver::FormatOptions options;
    if (params != nullptr) {
      if (const llvm::json::Object *formatting_options =
              params->getObject("options")) {
        if (const std::optional<std::int64_t> tab_size =
                formatting_options->getInteger("tabSize");
            tab_size && *tab_size > 0 && *tab_size <= 16)
          options.indent_width = static_cast<std::size_t>(*tab_size);
      }
    }
    const std::string formatted =
        driver::format_source(*document_source, options);
    const std::int64_t line_count = static_cast<std::int64_t>(
        std::count(document_source->begin(), document_source->end(), '\n') +
        1);
    return {response(
        request_id(*request),
        llvm::json::Array{llvm::json::Object{
            {"range",
             llvm::json::Object{
                 {"start", position(0, 0)},
                 {"end", position(static_cast<std::uint32_t>(line_count), 0)},
             }},
            {"newText", formatted},
        }})};
  }

  if (*method == "textDocument/semanticTokens/full" && uri) {
    llvm::json::Array data;
    if (document_source == nullptr)
      return {response(request_id(*request),
                       llvm::json::Object{{"data", std::move(data)}})};
    std::uint32_t previous_line = 0;
    std::uint32_t previous_column = 0;
    bool first = true;
    const std::vector<frontend::Token> document_tokens =
        tokens(*document_source);
    std::unordered_map<std::size_t, std::pair<std::int64_t, std::int64_t>>
        declaration_kinds;
    std::unordered_map<std::string, std::int64_t> type_kinds;
    std::vector<std::vector<std::string>> imported_paths;
    std::unordered_map<std::size_t, std::int64_t> namespace_offsets;
    for (std::size_t index = 0; index + 1 < document_tokens.size(); ++index) {
      const bool is_import =
          document_tokens[index].kind == frontend::TokenKind::Import;
      const bool is_module =
          document_tokens[index].kind == frontend::TokenKind::Module;
      if (!is_import && !is_module)
        continue;
      std::vector<std::string> import_path;
      std::size_t component = index + 1;
      while (component < document_tokens.size() &&
             document_tokens[component].kind ==
                 frontend::TokenKind::Identifier) {
        namespace_offsets.insert_or_assign(
            document_tokens[component].location.offset, is_module ? 1 : 0);
        if (is_import)
          import_path.emplace_back(document_tokens[component].lexeme);
        if (component + 2 >= document_tokens.size() ||
            document_tokens[component + 1].kind != frontend::TokenKind::Dot)
          break;
        component += 2;
      }
      if (!import_path.empty())
        imported_paths.push_back(std::move(import_path));
    }
    const auto semantic_kind = [](const DocumentSymbol &symbol) {
      std::string_view detail = symbol.detail;
      if (detail.starts_with("private "))
        detail.remove_prefix(std::string_view{"private "}.size());
      if (detail.starts_with("class "))
        return std::int64_t{2};
      if (detail.starts_with("trait "))
        return std::int64_t{4};
      if (detail.starts_with("enum "))
        return std::int64_t{3};
      if (detail.starts_with("def "))
        return symbol.is_top_level ? std::int64_t{5} : std::int64_t{6};
      if (detail.starts_with("parameter "))
        return std::int64_t{8};
      return std::int64_t{7};
    };
    const std::vector<DocumentSymbol> semantic_symbols = symbols(
        *uri, *document_source, module_search_paths_, workspace_search_paths_,
        &documents_);
    for (const DocumentSymbol &symbol : semantic_symbols) {
      const std::int64_t kind = semantic_kind(symbol);
      std::int64_t modifiers = 1;
      if (symbol.detail.starts_with("val ") ||
          symbol.detail.starts_with("private val "))
        modifiers |= 2;
      declaration_kinds.insert_or_assign(
          symbol.location.offset,
          std::pair<std::int64_t, std::int64_t>{kind, modifiers});
      if (kind == 2 || kind == 3 || kind == 4)
        type_kinds.try_emplace(symbol.name, kind);
    }
    const auto resolved_kind =
        [&](std::string_view name,
            std::size_t offset) -> std::optional<std::int64_t> {
      const DocumentSymbol *best = nullptr;
      for (const DocumentSymbol &candidate : semantic_symbols) {
        if (candidate.name != name || offset < candidate.scope_start ||
            offset > candidate.scope_end)
          continue;
        const std::int64_t kind = semantic_kind(candidate);
        const bool forward_visible =
            candidate.is_top_level && kind >= 2 && kind <= 6;
        if (candidate.location.offset > offset && !forward_visible)
          continue;
        if (best == nullptr || candidate.scope_depth > best->scope_depth ||
            (candidate.scope_depth == best->scope_depth &&
             candidate.location.offset > best->location.offset &&
             candidate.location.offset <= offset))
          best = &candidate;
      }
      if (best == nullptr)
        return std::nullopt;
      return semantic_kind(*best);
    };
    std::function<bool(std::size_t)> is_type_position;
    is_type_position = [&](std::size_t index) {
      while (index >= 2 &&
             document_tokens[index - 1].kind == frontend::TokenKind::Dot &&
             document_tokens[index - 2].kind == frontend::TokenKind::Identifier)
        index -= 2;
      if (index == 0)
        return false;
      const frontend::TokenKind previous = document_tokens[index - 1].kind;
      if (previous == frontend::TokenKind::Colon ||
          previous == frontend::TokenKind::New ||
          previous == frontend::TokenKind::Extends)
        return true;
      if (previous != frontend::TokenKind::LeftBracket &&
          previous != frontend::TokenKind::Comma)
        return false;

      std::size_t depth = 0;
      for (std::size_t cursor = index; cursor-- > 0;) {
        const frontend::TokenKind kind = document_tokens[cursor].kind;
        if (kind == frontend::TokenKind::RightBracket) {
          ++depth;
        } else if (kind == frontend::TokenKind::LeftBracket) {
          if (depth != 0) {
            --depth;
            continue;
          }
          const bool enclosing_type =
              cursor != 0 &&
              document_tokens[cursor - 1].kind ==
                  frontend::TokenKind::Identifier &&
              is_type_position(cursor - 1);
          if (enclosing_type)
            return true;

          std::size_t forward_depth = 0;
          for (std::size_t forward = cursor; forward < document_tokens.size();
               ++forward) {
            const frontend::TokenKind forward_kind =
                document_tokens[forward].kind;
            if (forward_kind == frontend::TokenKind::LeftBracket) {
              ++forward_depth;
            } else if (forward_kind == frontend::TokenKind::RightBracket &&
                       --forward_depth == 0) {
              return forward + 1 < document_tokens.size() &&
                     document_tokens[forward + 1].kind ==
                         frontend::TokenKind::LeftParen;
            }
          }
          return false;
        }
      }
      return false;
    };
    const auto chain_start = [&](std::size_t index) {
      while (index >= 2 &&
             document_tokens[index - 1].kind == frontend::TokenKind::Dot &&
             document_tokens[index - 2].kind == frontend::TokenKind::Identifier)
        index -= 2;
      return index;
    };
    const auto matches_import = [&](std::size_t index, bool exact) {
      const std::size_t start = chain_start(index);
      if (resolved_kind(document_tokens[start].lexeme,
                        document_tokens[start].location.offset)
              .has_value())
        return false;
      std::vector<std::string_view> components;
      for (std::size_t component = start; component <= index; component += 2)
        components.push_back(document_tokens[component].lexeme);
      for (const std::vector<std::string> &path : imported_paths) {
        if ((exact && path.size() != components.size()) ||
            (!exact && path.size() < components.size()))
          continue;
        bool matches = true;
        for (std::size_t component = 0; component < components.size();
             ++component)
          if (path[component] != components[component]) {
            matches = false;
            break;
          }
        if (matches)
          return true;
      }
      return false;
    };
    const auto is_builtin_type = [](std::string_view name) {
      return name == "bool" || name == "byte" || name == "char" ||
             name == "double" || name == "int" || name == "string" ||
             name == "unit" || name == "usize";
    };
    for (std::size_t index = 0; index < document_tokens.size(); ++index) {
      const frontend::Token &token = document_tokens[index];
      std::optional<std::int64_t> type;
      std::int64_t modifiers = 0;
      if (semantic_keyword(token.kind)) {
        type = 10;
      } else if (token.kind == frontend::TokenKind::StringLiteral ||
                 token.kind == frontend::TokenKind::CharacterLiteral) {
        type = 11;
      } else if (token.kind == frontend::TokenKind::IntegerLiteral ||
                 token.kind == frontend::TokenKind::DoubleLiteral) {
        type = 12;
      } else if (semantic_operator(token.kind)) {
        type = 13;
      } else if (token.kind == frontend::TokenKind::Identifier) {
        const frontend::TokenKind previous =
            index == 0 ? frontend::TokenKind::End
                       : document_tokens[index - 1].kind;
        if (const auto namespace_token =
                namespace_offsets.find(token.location.offset);
            namespace_token != namespace_offsets.end()) {
          type = 0;
          modifiers = namespace_token->second;
        } else if (const auto declaration =
                       declaration_kinds.find(token.location.offset);
                   declaration != declaration_kinds.end()) {
          type = declaration->second.first;
          modifiers = declaration->second.second;
        } else if (is_builtin_type(token.lexeme)) {
          type = 1;
        } else if (is_type_position(index)) {
          if (index + 1 < document_tokens.size() &&
              document_tokens[index + 1].kind == frontend::TokenKind::Dot) {
            type = matches_import(index, false) ? 0 : 1;
          } else {
            const auto known_type = type_kinds.find(std::string{token.lexeme});
            type = known_type == type_kinds.end() ? 1 : known_type->second;
          }
        } else if (index + 1 < document_tokens.size() &&
                   document_tokens[index + 1].kind ==
                       frontend::TokenKind::LeftParen) {
          bool module_function = false;
          if (index >= 2 && previous == frontend::TokenKind::Dot)
            module_function = matches_import(index - 2, true);
          type = previous == frontend::TokenKind::Dot
                     ? (module_function ? 5 : 6)
                     : resolved_kind(token.lexeme, token.location.offset)
                           .value_or(5);
        } else if (const std::optional<std::int64_t> known =
                       resolved_kind(token.lexeme, token.location.offset)) {
          type = *known;
        } else if (index + 1 < document_tokens.size() &&
                   document_tokens[index + 1].kind ==
                       frontend::TokenKind::Dot &&
                   matches_import(index, false)) {
          type = 0;
        } else if (index != 0 && document_tokens[index - 1].kind ==
                                     frontend::TokenKind::Dot) {
          type = 9;
        } else {
          type = 7;
        }
      }
      if (!type.has_value())
        continue;
      const llvm::json::Object token_position =
          position_at_offset(*document_source, token.location.offset);
      const std::uint32_t line =
          static_cast<std::uint32_t>(*token_position.getInteger("line"));
      const std::uint32_t column =
          static_cast<std::uint32_t>(*token_position.getInteger("character"));
      data.emplace_back(
          static_cast<std::int64_t>(first ? line : line - previous_line));
      data.emplace_back(static_cast<std::int64_t>(
          first || line != previous_line ? column : column - previous_column));
      data.emplace_back(static_cast<std::int64_t>(utf16_length(token.lexeme)));
      data.emplace_back(*type);
      data.emplace_back(modifiers);
      previous_line = line;
      previous_column = column;
      first = false;
    }
    return {response(request_id(*request),
                     llvm::json::Object{{"data", std::move(data)}})};
  }

  if (*method == "textDocument/inlayHint" && uri) {
    llvm::json::Array hints;
    if (document_source == nullptr)
      return {response(request_id(*request), std::move(hints))};
    std::optional<std::size_t> range_start;
    std::optional<std::size_t> range_end;
    if (params != nullptr)
      if (const llvm::json::Object *requested_range =
              params->getObject("range"))
        if (const llvm::json::Object *start =
                requested_range->getObject("start"))
          if (const llvm::json::Object *end =
                  requested_range->getObject("end")) {
            const auto start_line = start->getInteger("line");
            const auto start_character = start->getInteger("character");
            const auto end_line = end->getInteger("line");
            const auto end_character = end->getInteger("character");
            if (start_line && start_character && end_line && end_character &&
                *start_line >= 0 && *start_character >= 0 && *end_line >= 0 &&
                *end_character >= 0) {
              range_start = offset_from_position(
                  *document_source,
                  static_cast<std::uint32_t>(*start_line),
                  static_cast<std::uint32_t>(*start_character));
              range_end = offset_from_position(
                  *document_source, static_cast<std::uint32_t>(*end_line),
                  static_cast<std::uint32_t>(*end_character));
            }
          }
    if (inferred_type_hints_ && range_start && range_end &&
        *range_start <= *range_end) {
      const std::vector<frontend::Token> document_tokens =
          tokens(*document_source);
      std::unordered_map<std::size_t, std::string> analyzed_types;
      try {
        analyzed_types =
            analyze_local_types(*uri, *document_source, module_search_paths_,
                                workspace_search_paths_, &documents_);
      } catch (const std::exception &) {
        analyzed_types.clear();
      }
      for (std::size_t index = 0; index + 3 < document_tokens.size(); ++index) {
        if ((document_tokens[index].kind != frontend::TokenKind::Val &&
             document_tokens[index].kind != frontend::TokenKind::Var) ||
            document_tokens[index + 1].kind !=
                frontend::TokenKind::Identifier ||
            document_tokens[index + 2].kind != frontend::TokenKind::Equal)
          continue;
        const auto analyzed =
            analyzed_types.find(document_tokens[index].location.offset);
        const std::optional<std::string> fallback =
            analyzed == analyzed_types.end()
                ? inferred_literal_type(document_tokens, index + 3)
                : std::nullopt;
        if (analyzed == analyzed_types.end() && !fallback.has_value())
          continue;
        const frontend::Token &name = document_tokens[index + 1];
        const std::size_t hint_offset =
            name.location.offset + name.lexeme.size();
        if (hint_offset < *range_start || hint_offset >= *range_end)
          continue;
        hints.emplace_back(llvm::json::Object{
            {"position",
             position_at_offset(*document_source, hint_offset)},
            {"label", ": " + (analyzed != analyzed_types.end()
                                    ? analyzed->second
                                    : *fallback)},
            {"kind", 1},
            {"paddingLeft", false},
            {"paddingRight", true},
        });
      }
      for (const DocumentSymbol &symbol : symbols(
               *uri, *document_source, module_search_paths_,
               workspace_search_paths_, &documents_)) {
        if (!symbol.detail.starts_with("const "))
          continue;
        const std::size_t value_start = symbol.detail.find(" = ");
        const std::size_t origin_start = symbol.detail.find("\n\norigin `");
        if (value_start == std::string::npos ||
            origin_start == std::string::npos || origin_start <= value_start)
          continue;
        const std::size_t hint_offset =
            symbol.location.offset + symbol.name.size();
        if (hint_offset < *range_start || hint_offset >= *range_end)
          continue;
        hints.emplace_back(llvm::json::Object{
            {"position", position_at_offset(*document_source, hint_offset)},
            {"label", symbol.detail.substr(value_start,
                                            origin_start - value_start)},
            {"kind", 1},
            {"paddingLeft", false},
            {"paddingRight", true},
            {"tooltip", symbol.detail.substr(origin_start + 2)},
        });
      }
    }
    return {response(request_id(*request), std::move(hints))};
  }

  const llvm::json::Object *request_position =
      params == nullptr ? nullptr : params->getObject("position");
  const std::optional<std::int64_t> line =
      request_position == nullptr ? std::nullopt
                                  : request_position->getInteger("line");
  const std::optional<std::int64_t> character =
      request_position == nullptr ? std::nullopt
                                  : request_position->getInteger("character");
  const auto document = open_document;
  if (uri && line && character && document != documents_.end()) {
    const std::optional<LocatedIdentifier> identifier =
        identifier_at(document->second, static_cast<std::uint32_t>(*line),
                      static_cast<std::uint32_t>(*character));
    std::vector<std::filesystem::path> search_paths = module_search_paths_;
    search_paths.insert(search_paths.end(), workspace_search_paths_.begin(),
                        workspace_search_paths_.end());
    std::string semantic_source = document->second;
    if (*method == "textDocument/completion" && *character >= 1)
      if (const auto cursor = offset_from_position(
              semantic_source, static_cast<std::uint32_t>(*line),
              static_cast<std::uint32_t>(*character));
          cursor && *cursor != 0 && semantic_source[*cursor - 1] == '.')
        semantic_source[*cursor - 1] = ' ';
    const SemanticIndex semantic_index =
        build_semantic_index(*uri, semantic_source, documents_,
                             workspace_uris_, search_paths, index_cache_);
    const std::vector<DocumentSymbol> &document_symbols =
        semantic_index.documents.front().index->symbols;
    const auto bind_symbol = [&](const IndexedDocument &origin,
                                 const LocatedIdentifier &requested)
        -> std::optional<std::pair<std::string, DocumentSymbol>> {
      const DocumentSymbol *best = nullptr;
      if (!requested.qualifier)
        for (const DocumentSymbol &symbol : origin.index->symbols) {
          if (symbol.name != requested.name ||
              symbol.location.offset > requested.location.offset ||
              (requested.location.offset < symbol.scope_start &&
               requested.location.offset != symbol.location.offset) ||
              requested.location.offset > symbol.scope_end)
            continue;
          if (best == nullptr || symbol.scope_depth > best->scope_depth ||
              (symbol.scope_depth == best->scope_depth &&
               symbol.location.offset > best->location.offset))
            best = &symbol;
        }
      if (best != nullptr)
        return std::pair<std::string, DocumentSymbol>{origin.uri, *best};

      std::vector<std::pair<std::string, DocumentSymbol>> candidates;
      for (const IndexedDocument &candidate : semantic_index.documents) {
        bool visible_module = candidate.uri == origin.uri;
        if (!visible_module && candidate.module_name.has_value())
          visible_module =
              imports_module(origin, *candidate.module_name, candidate.uri);
        if (!visible_module)
          continue;
        for (const DocumentSymbol &symbol : candidate.index->symbols) {
          const bool exposed =
              candidate.uri == origin.uri ||
              !candidate.module_name.has_value() ||
              import_exposes(origin, *candidate.module_name, symbol.name,
                             requested.qualifier, requested.name,
                             candidate.uri);
          if (!exposed || !symbol.is_top_level ||
              (candidate.uri == origin.uri && symbol.name != requested.name) ||
              (symbol.is_private && candidate.uri != origin.uri))
            continue;
          candidates.emplace_back(candidate.uri, symbol);
        }
      }
      if (candidates.empty())
        return std::nullopt;
      const std::string identity = candidates.front().second.id;
      if (std::any_of(candidates.begin() + 1, candidates.end(),
                      [&](const auto &candidate) {
                        return candidate.second.id != identity;
                      }))
        return std::nullopt;
      return candidates.front();
    };
    const auto locate_symbol = [&](const LocatedIdentifier &requested) {
      return bind_symbol(semantic_index.documents.front(), requested);
    };
    if (*method == "textDocument/signatureHelp") {
      if (const auto call =
              call_at(document->second, static_cast<std::uint32_t>(*line),
                      static_cast<std::uint32_t>(*character))) {
        const auto callee =
            bind_symbol(semantic_index.documents.front(), call->callee);
        if (callee) {
          llvm::json::Array signatures;
          std::size_t active_parameter = 0;
          for (const IndexedDocument &indexed : semantic_index.documents) {
            if (indexed.uri != callee->first)
              continue;
            try {
              frontend::Parser parser{indexed.index->source};
              const ast::Program program = parser.parse_program();
              for (const ast::FunctionDeclaration &function :
                   program.functions) {
                if (function.name != call->callee.name)
                  continue;
                llvm::json::Array parameters;
                for (const ast::FunctionDeclaration::Parameter &parameter :
                     function.parameters)
                  parameters.emplace_back(llvm::json::Object{
                      {"label", parameter.name + " : " +
                                    type_reference(parameter.type)}});
                const std::size_t active =
                    function.parameters.empty()
                        ? 0
                        : std::min(call->active_parameter,
                                   function.parameters.size() - 1);
                if (signatures.empty())
                  active_parameter = active;
                signatures.emplace_back(llvm::json::Object{
                    {"label", function_signature(function)},
                    {"parameters", std::move(parameters)},
                });
              }
            } catch (const std::exception &) {
            }
          }
          if (!signatures.empty())
            return {response(request_id(*request),
                             llvm::json::Object{
                                 {"signatures", std::move(signatures)},
                                 {"activeSignature", 0},
                                 {"activeParameter",
                                  static_cast<std::int64_t>(active_parameter)},
                             })};
        } else {
          if (!call->receiver)
            return {response(request_id(*request), nullptr)};
          const auto bound_receiver =
              bind_symbol(semantic_index.documents.front(), *call->receiver);
          if (!bound_receiver)
            return {response(request_id(*request), nullptr)};
          const std::size_t type_separator =
              bound_receiver->second.detail.rfind(" : ");
          if (type_separator == std::string::npos)
            return {response(request_id(*request), nullptr)};
          const std::string receiver_type =
              bound_receiver->second.detail.substr(type_separator + 3);
          const std::size_t receiver_separator = receiver_type.rfind('.');
          const std::string receiver_class =
              receiver_separator == std::string::npos
                  ? receiver_type
                  : receiver_type.substr(receiver_separator + 1);
          const std::optional<std::string> receiver_module =
              receiver_separator == std::string::npos
                  ? std::nullopt
                  : std::optional<std::string>{
                        receiver_type.substr(0, receiver_separator)};
          llvm::json::Array signatures;
          std::optional<std::string> owning_class;
          std::size_t active_parameter = 0;
          bool ambiguous_owner = false;
          for (const IndexedDocument &indexed : semantic_index.documents) {
            bool visible = indexed.uri == *uri;
            if (receiver_module) {
              visible = indexed.module_name.has_value() &&
                        ((indexed.uri == *uri &&
                          *indexed.module_name == *receiver_module) ||
                         qualifier_imports_module(
                             semantic_index.documents.front(),
                             *receiver_module, *indexed.module_name,
                             indexed.uri));
            } else if (!visible && indexed.module_name) {
              visible = imports_module(semantic_index.documents.front(),
                                       *indexed.module_name, indexed.uri);
            }
            if (!visible)
              continue;
            try {
              frontend::Parser parser{indexed.index->source};
              const ast::Program program = parser.parse_program();
              for (const ast::ClassDeclaration &class_declaration :
                   program.classes) {
                if (class_declaration.name != receiver_class)
                  continue;
                const std::string owner =
                    indexed.uri + "#" + class_declaration.name;
                if (owning_class && *owning_class != owner)
                  ambiguous_owner = true;
                else
                  owning_class = owner;
                for (const ast::FunctionDeclaration &method :
                     class_declaration.methods) {
                  if (method.name != call->callee.name)
                    continue;
                  llvm::json::Array parameters;
                  for (const ast::FunctionDeclaration::Parameter &parameter :
                       method.parameters)
                    parameters.emplace_back(llvm::json::Object{
                        {"label", parameter.name + " : " +
                                      type_reference(parameter.type)}});
                  const std::size_t active =
                      method.parameters.empty()
                          ? 0
                          : std::min(call->active_parameter,
                                     method.parameters.size() - 1);
                  if (signatures.empty())
                    active_parameter = active;
                  signatures.emplace_back(llvm::json::Object{
                      {"label", function_signature(method)},
                      {"parameters", std::move(parameters)},
                  });
                }
              }
            } catch (const std::exception &) {
            }
          }
          if (!ambiguous_owner && !signatures.empty())
            return {response(request_id(*request),
                             llvm::json::Object{
                                 {"signatures", std::move(signatures)},
                                 {"activeSignature", 0},
                                 {"activeParameter",
                                  static_cast<std::int64_t>(active_parameter)},
                             })};
        }
      }
      return {response(request_id(*request), nullptr)};
    }
    if (*method == "textDocument/implementation") {
      if (identifier) {
        const auto target = locate_symbol(*identifier);
        if (target.has_value() &&
            target->second.detail.find("trait ") != std::string::npos) {
          llvm::json::Array locations;
          for (const IndexedDocument &indexed : semantic_index.documents) {
            if (target->second.is_private && indexed.uri != target->first)
              continue;
            try {
              frontend::Parser parser{indexed.index->source};
              const ast::Program program = parser.parse_program();
              for (const ast::ClassDeclaration &implementation :
                   program.classes) {
                const bool implements = std::any_of(
                    implementation.implemented_traits.begin(),
                    implementation.implemented_traits.end(),
                    [&](const ast::TypeReference &trait) {
                      const std::size_t separator = trait.name.rfind('.');
                      const std::string trait_name =
                          separator == std::string::npos
                              ? trait.name
                              : trait.name.substr(separator + 1);
                      const std::optional<std::string> qualifier =
                          separator == std::string::npos
                              ? std::nullopt
                              : std::optional<std::string>{
                                    trait.name.substr(0, separator)};
                      const auto bound_trait = bind_symbol(
                          indexed, LocatedIdentifier{trait_name, trait.location,
                                                     qualifier});
                      return bound_trait.has_value() &&
                             bound_trait->second.id == target->second.id;
                    });
                if (implements)
                  for (const DocumentSymbol &symbol : indexed.index->symbols)
                    if (symbol.name == implementation.name &&
                        symbol.detail.starts_with("class ")) {
                      locations.emplace_back(llvm::json::Object{
                          {"uri", indexed.uri},
                          {"range", range(indexed.index->source,
                                          symbol.location, symbol.name.size())},
                      });
                      break;
                    }
              }
            } catch (const std::exception &) {
            }
          }
          return {response(request_id(*request), std::move(locations))};
        }
      }
      return {response(request_id(*request), nullptr)};
    }
    if (*method == "textDocument/prepareRename") {
      if (identifier && locate_symbol(*identifier).has_value())
        return {
            response(request_id(*request),
                     llvm::json::Object{
                         {"range", range(document->second, identifier->location,
                                         identifier->name.size())},
                         {"placeholder", identifier->name},
                     })};
      return {response(request_id(*request), nullptr)};
    }
    if (*method == "textDocument/rename") {
      const std::optional<llvm::StringRef> requested_name =
          params == nullptr ? std::nullopt : params->getString("newName");
      if (!identifier || !requested_name || !valid_identifier(*requested_name))
        return {error_response(request_id(*request), -32602,
                               "Rename requires a valid identifier")};
      const auto target = locate_symbol(*identifier);
      if (!target.has_value())
        return {error_response(request_id(*request), -32602,
                               "No symbol can be renamed here")};
      const IndexedDocument &active_document = semantic_index.documents.front();
      bool explicit_local_alias = false;
      for (const ast::ImportDeclaration &import : active_document.imports)
        for (const ast::ImportDeclaration::Symbol &symbol : import.symbols)
          if (symbol.alias == identifier->name)
            explicit_local_alias = true;
      if (explicit_local_alias) {
        const std::string new_name = requested_name->str();
        for (const DocumentSymbol &symbol : active_document.index->symbols)
          if (symbol.name == new_name)
            return {error_response(request_id(*request), -32602,
                                   "Rename would collide with a local symbol")};
        llvm::json::Array alias_edits;
        const std::vector<frontend::Token> document_tokens =
            tokens(active_document.index->source);
        for (std::size_t token_index = 0; token_index < document_tokens.size();
             ++token_index) {
          const frontend::Token &token = document_tokens[token_index];
          if (token.kind != frontend::TokenKind::Identifier ||
              token.lexeme != identifier->name)
            continue;
          const bool declaration =
              token_index != 0 &&
              document_tokens[token_index - 1].kind == frontend::TokenKind::As;
          const auto bound =
              bind_symbol(active_document,
                          located_identifier(document_tokens, token_index));
          if (!declaration &&
              (!bound.has_value() || bound->second.id != target->second.id))
            continue;
          alias_edits.emplace_back(llvm::json::Object{
              {"range", range(active_document.index->source, token.location,
                              token.lexeme.size())},
              {"newText", new_name},
          });
        }
        return {response(
            request_id(*request),
            llvm::json::Object{
                {"changes", llvm::json::Object{{active_document.uri,
                                                std::move(alias_edits)}}}})};
      }
      if (target->second.name == *requested_name)
        return {
            response(request_id(*request),
                     llvm::json::Object{{"changes", llvm::json::Object{}}})};
      const std::string new_name = requested_name->str();

      struct RenameOccurrence {
        const IndexedDocument *document;
        frontend::Token token;
        LocatedIdentifier identifier;
      };
      std::vector<RenameOccurrence> occurrences;
      for (const IndexedDocument &indexed : semantic_index.documents) {
        if (target->second.is_private && indexed.uri != target->first)
          continue;
        const std::vector<frontend::Token> document_tokens =
            tokens(indexed.index->source);
        for (std::size_t token_index = 0; token_index < document_tokens.size();
             ++token_index) {
          const frontend::Token &token = document_tokens[token_index];
          if (token.kind != frontend::TokenKind::Identifier ||
              token.lexeme != target->second.name)
            continue;
          LocatedIdentifier occurrence_identifier =
              located_identifier(document_tokens, token_index);
          const auto bound = bind_symbol(indexed, occurrence_identifier);
          if (!bound.has_value() || bound->second.id != target->second.id)
            continue;
          occurrences.push_back(RenameOccurrence{
              &indexed, token, std::move(occurrence_identifier)});
        }
      }

      const auto rename_is_safe = [&](const RenameOccurrence &occurrence) {
        const DocumentSymbol *best_local = nullptr;
        for (const DocumentSymbol &candidate :
             occurrence.document->index->symbols) {
          const bool renamed_target = candidate.id == target->second.id;
          const std::string_view effective_name =
              renamed_target ? std::string_view{new_name} : candidate.name;
          if (effective_name != new_name ||
              candidate.location.offset > occurrence.token.location.offset ||
              (occurrence.token.location.offset < candidate.scope_start &&
               occurrence.token.location.offset != candidate.location.offset) ||
              occurrence.token.location.offset > candidate.scope_end)
            continue;
          if (best_local == nullptr ||
              candidate.scope_depth > best_local->scope_depth ||
              (candidate.scope_depth == best_local->scope_depth &&
               candidate.location.offset > best_local->location.offset))
            best_local = &candidate;
        }
        if (best_local != nullptr)
          return best_local->id == target->second.id;

        std::unordered_set<std::string> identities;
        for (const IndexedDocument &candidate_document :
             semantic_index.documents) {
          bool visible = candidate_document.uri == occurrence.document->uri;
          if (occurrence.identifier.qualifier) {
            visible = candidate_document.module_name.has_value() &&
                      qualifier_imports_module(*occurrence.document,
                                               *occurrence.identifier.qualifier,
                                               *candidate_document.module_name,
                                               candidate_document.uri);
          } else if (!visible && candidate_document.module_name) {
            visible = imports_module(*occurrence.document,
                                     *candidate_document.module_name,
                                     candidate_document.uri);
          }
          if (!visible)
            continue;
          for (const DocumentSymbol &candidate :
               candidate_document.index->symbols) {
            const bool renamed_target = candidate.id == target->second.id;
            const std::string_view effective_name =
                renamed_target ? std::string_view{new_name} : candidate.name;
            if (!candidate.is_top_level || effective_name != new_name ||
                (candidate.is_private &&
                 candidate_document.uri != occurrence.document->uri))
              continue;
            identities.insert(candidate.id);
          }
        }
        return identities.size() == 1 && identities.contains(target->second.id);
      };
      if (std::any_of(occurrences.begin(), occurrences.end(),
                      [&](const RenameOccurrence &occurrence) {
                        return !rename_is_safe(occurrence);
                      }))
        return {error_response(request_id(*request), -32602,
                               "Rename would change or ambiguate symbol "
                               "resolution")};

      std::unordered_map<std::string, llvm::json::Array> edits;
      for (const RenameOccurrence &occurrence : occurrences)
        edits[occurrence.document->uri].emplace_back(llvm::json::Object{
            {"range",
             range(occurrence.document->index->source,
                   occurrence.token.location, occurrence.token.lexeme.size())},
            {"newText", new_name},
        });
      llvm::json::Array document_changes;
      std::vector<std::string> edited_uris;
      edited_uris.reserve(edits.size());
      for (const auto &[edited_uri, ignored] : edits)
        edited_uris.push_back(edited_uri);
      std::sort(edited_uris.begin(), edited_uris.end());
      for (const std::string &edited_uri : edited_uris) {
        llvm::json::Value version = nullptr;
        if (const auto current = document_versions_.find(edited_uri);
            current != document_versions_.end())
          version = current->second;
        document_changes.emplace_back(llvm::json::Object{
            {"textDocument",
             llvm::json::Object{{"uri", edited_uri},
                                {"version", std::move(version)}}},
            {"edits", std::move(edits.at(edited_uri))},
        });
      }
      return {response(request_id(*request),
                       llvm::json::Object{
                           {"documentChanges", std::move(document_changes)}})};
    }
    if (*method == "textDocument/hover") {
      if (identifier) {
        if (const auto located = locate_symbol(*identifier)) {
          const DocumentSymbol &symbol = located->second;
          std::string detail = symbol.detail;
          if ((identifier->name != symbol.name || identifier->qualifier) &&
              symbol.module_name.has_value()) {
            const std::string local =
                identifier->qualifier.has_value()
                    ? *identifier->qualifier + "." + identifier->name
                    : identifier->name;
            detail = local + " (alias of " + *symbol.module_name + "." +
                     symbol.name + ")\n" + detail;
          }
          if (symbol.is_global && symbol.module_name.has_value())
            detail += "\n\nmodule `" + *symbol.module_name + "`";
          return {response(
              request_id(*request),
              llvm::json::Object{
                  {"contents",
                   llvm::json::Object{
                       {"kind", "markdown"},
                       {"value", "```janus\n" + detail + "\n```"}}},
                  {"range", range(document->second, identifier->location,
                                  identifier->name.size())},
              })};
        }
      }
      return {response(request_id(*request), nullptr)};
    }
    if (*method == "textDocument/definition") {
      if (identifier) {
        if (const auto located = locate_symbol(*identifier)) {
          const DocumentSymbol &symbol = located->second;
          return {response(
              request_id(*request),
              llvm::json::Object{
                  {"uri", located->first},
                  {"range", range(index_cache_.at(located->first).source,
                                  symbol.location, symbol.name.size())},
              })};
        }
      }
      return {response(request_id(*request), nullptr)};
    }
    if (*method == "textDocument/references") {
      if (identifier) {
        llvm::json::Array references;
        const auto target = locate_symbol(*identifier);
        bool include_declaration = true;
        if (params != nullptr)
          if (const llvm::json::Object *context = params->getObject("context"))
            if (const std::optional<bool> requested =
                    context->getBoolean("includeDeclaration"))
              include_declaration = *requested;
        for (const IndexedDocument &indexed : semantic_index.documents) {
          const std::vector<frontend::Token> document_tokens =
              tokens(indexed.index->source);
          for (std::size_t token_index = 0;
               token_index < document_tokens.size(); ++token_index) {
            const frontend::Token &token = document_tokens[token_index];
            if (token.kind == frontend::TokenKind::Identifier &&
                token.lexeme == identifier->name) {
              const auto bound = bind_symbol(
                  indexed, located_identifier(document_tokens, token_index));
              if (!target.has_value() || !bound.has_value() ||
                  bound->second.id != target->second.id)
                continue;
              if (!include_declaration && indexed.uri == target->first &&
                  token.location.offset == target->second.location.offset)
                continue;
              references.emplace_back(llvm::json::Object{
                  {"uri", indexed.uri},
                  {"range", range(indexed.index->source, token.location,
                                  token.lexeme.size())},
              });
            }
          }
        }
        return {response(request_id(*request), std::move(references))};
      }
      return {response(request_id(*request), nullptr)};
    }
    if (*method == "textDocument/completion") {
      const bool member_context =
          character_before(document->second, static_cast<std::uint32_t>(*line),
                           static_cast<std::uint32_t>(*character)) == '.';
      const std::optional<std::string> member_qualifier =
          member_qualifier_before(document->second,
                                  static_cast<std::uint32_t>(*line),
                                  static_cast<std::uint32_t>(*character));
      llvm::json::Array items;
      std::vector<std::string> labels;
      const auto add_item = [&](std::string label, std::string detail,
                                std::int64_t kind,
                                std::optional<std::string> import_module =
                                    std::nullopt) {
        const std::string import_symbol = label;
        const std::string key = label + "\n" + detail;
        if (std::find(labels.begin(), labels.end(), key) != labels.end())
          return;
        labels.push_back(key);
        llvm::json::Object item{
            {"label", std::move(label)},
            {"kind", kind},
            {"detail", std::move(detail)},
        };
        if (import_module) {
          std::size_t insertion = import_insertion_offset(document->second);
          std::size_t end = insertion;
          std::string new_text = "import " + *import_module + "\n";
          for (const ast::ImportDeclaration &import :
               semantic_index.documents.front().imports) {
            if (import.module_name != *import_module || !import.is_selective())
              continue;
            const std::size_t line_start =
                document->second.rfind('\n', import.location.offset) ==
                        std::string::npos
                    ? 0
                    : document->second.rfind('\n', import.location.offset) + 1;
            const std::size_t line_end =
                document->second.find('\n', import.location.offset);
            const std::size_t closing =
                document->second.find('}', import.location.offset);
            if (closing != std::string::npos &&
                (line_end == std::string::npos || closing < line_end)) {
              insertion = line_start;
              end = line_end == std::string::npos ? document->second.size()
                                                   : line_end;
              new_text = document->second.substr(line_start, closing - line_start) +
                         ", " + import_symbol + "}";
            }
            break;
          }
          llvm::json::Object edit_range{
              {"start", position_at_offset(document->second, insertion)},
              {"end", position_at_offset(document->second, end)}};
          llvm::json::Object edit{
              {"range", std::move(edit_range)},
              {"newText", std::move(new_text)}};
          llvm::json::Array edits;
          edits.emplace_back(std::move(edit));
          item["additionalTextEdits"] = std::move(edits);
        }
        items.emplace_back(std::move(item));
      };

      const janus::driver::ApiIndex completion_index =
          combined_api_index(*uri);

      bool module_member_context = false;
      if (member_qualifier.has_value()) {
        for (const IndexedDocument &candidate : semantic_index.documents) {
          const bool matching_module =
              candidate.module_name.has_value() &&
              (*candidate.module_name == *member_qualifier ||
               qualifier_imports_module(semantic_index.documents.front(),
                                        *member_qualifier,
                                        *candidate.module_name,
                                        candidate.uri));
          if (!matching_module)
            continue;
          for (const DocumentSymbol &symbol : candidate.index->symbols) {
            if (symbol.is_top_level && !symbol.is_private &&
                symbol.module_name == candidate.module_name) {
              module_member_context = true;
              const std::int64_t kind =
                  symbol.detail.starts_with("def ") ? 3
                  : symbol.detail.starts_with("class ") ||
                          symbol.detail.starts_with("trait ") ||
                          symbol.detail.starts_with("enum ")
                      ? 7
                      : 6;
              add_item(symbol.name, symbol.detail, kind);
            }
          }
        }
        for (const janus::driver::ApiSymbol &symbol : completion_index.symbols) {
          if (symbol.qualified_name !=
              symbol.module + "." + symbol.simple_name)
            continue;
          bool matching_module = symbol.module == *member_qualifier;
          for (const ast::ImportDeclaration &import :
               semantic_index.documents.front().imports)
            if (import.module_name == symbol.required_import &&
                import.module_alias == member_qualifier)
              matching_module = true;
          if (!matching_module)
            continue;
          module_member_context = true;
          const std::int64_t kind =
              symbol.kind == "function" ? 3
              : symbol.kind == "class"  ? 7
              : symbol.kind == "trait"  ? 8
              : symbol.kind == "enum"   ? 13
                                         : 6;
          add_item(symbol.simple_name, symbol.signature, kind);
        }
      }

      bool typed_member_context = false;
      if (member_context && !module_member_context && *character >= 2) {
        const std::optional<LocatedIdentifier> receiver = identifier_at(
            document->second, static_cast<std::uint32_t>(*line),
            static_cast<std::uint32_t>(*character - 2));
        const auto bound_receiver =
            receiver ? bind_symbol(semantic_index.documents.front(), *receiver)
                     : std::nullopt;
        if (bound_receiver) {
          const std::size_t type_separator =
              bound_receiver->second.detail.rfind(" : ");
          if (type_separator != std::string::npos) {
            typed_member_context = true;
            std::string receiver_type =
                bound_receiver->second.detail.substr(type_separator + 3);
            if (const std::size_t arguments = receiver_type.find('[');
                arguments != std::string::npos)
              receiver_type.resize(arguments);
            const std::size_t receiver_separator = receiver_type.rfind('.');
            const std::string receiver_class =
                receiver_separator == std::string::npos
                    ? receiver_type
                    : receiver_type.substr(receiver_separator + 1);
            const std::optional<std::string> receiver_module =
                receiver_separator == std::string::npos
                    ? std::nullopt
                    : std::optional<std::string>{
                          receiver_type.substr(0, receiver_separator)};

            const auto module_is_visible = [&](std::string_view module,
                                               std::string_view required_import) {
              if (receiver_module)
                return module == *receiver_module ||
                       std::any_of(
                           semantic_index.documents.front().imports.begin(),
                           semantic_index.documents.front().imports.end(),
                           [&](const ast::ImportDeclaration &import) {
                             return import.module_name == required_import &&
                                    import.module_alias == receiver_module;
                           });
              if (semantic_index.documents.front().module_name == module)
                return true;
              return std::any_of(
                  semantic_index.documents.front().imports.begin(),
                  semantic_index.documents.front().imports.end(),
                  [&](const ast::ImportDeclaration &import) {
                    if (import.module_name != required_import)
                      return false;
                    if (!import.is_selective())
                      return true;
                    return std::any_of(
                        import.symbols.begin(), import.symbols.end(),
                        [&](const ast::ImportDeclaration::Symbol &symbol) {
                          return symbol.name == receiver_class;
                        });
                  });
            };

            for (const janus::driver::ApiSymbol &symbol :
                 completion_index.symbols) {
              if (symbol.kind != "method" && symbol.kind != "field")
                continue;
              const std::size_t member_separator =
                  symbol.qualified_name.rfind('.');
              if (member_separator == std::string::npos)
                continue;
              const std::string_view owner = std::string_view{
                  symbol.qualified_name}.substr(0, member_separator);
              const std::size_t owner_separator = owner.rfind('.');
              const std::string_view owner_name =
                  owner_separator == std::string_view::npos
                      ? owner
                      : owner.substr(owner_separator + 1);
              if (owner_name != receiver_class ||
                  !module_is_visible(symbol.module, symbol.required_import))
                continue;
              add_item(symbol.simple_name, symbol.signature,
                       symbol.kind == "method" ? 2 : 5);
            }

            for (const IndexedDocument &indexed : semantic_index.documents) {
              bool visible = indexed.uri == *uri;
              if (!visible && indexed.module_name)
                visible = module_is_visible(*indexed.module_name,
                                            *indexed.module_name);
              if (!visible)
                continue;
              try {
                std::string parse_source = indexed.index->source;
                if (indexed.uri == *uri)
                  if (const auto cursor = offset_from_position(
                          parse_source, static_cast<std::uint32_t>(*line),
                          static_cast<std::uint32_t>(*character));
                      cursor && *cursor != 0 && parse_source[*cursor - 1] == '.')
                    parse_source[*cursor - 1] = ' ';
                frontend::Parser parser{parse_source};
                const ast::Program program = parser.parse_program();
                for (const ast::ClassDeclaration &declaration :
                     program.classes) {
                  if (declaration.name != receiver_class ||
                      (declaration.is_private && indexed.uri != *uri))
                    continue;
                  for (const ast::ValueDeclaration &field :
                       declaration.constructor_fields)
                    if (indexed.uri == *uri ||
                        (!field.is_private && !field.is_internal))
                      add_item(field.name, "field " + field.name, 5);
                  for (const ast::ValueDeclaration &field : declaration.fields)
                    if (indexed.uri == *uri ||
                        (!field.is_private && !field.is_internal))
                      add_item(field.name, "field " + field.name, 5);
                  for (const ast::FunctionDeclaration &member :
                       declaration.methods)
                    if (indexed.uri == *uri ||
                        (!member.is_private && !member.is_internal))
                      add_item(member.name, function_signature(member), 2);
                }
              } catch (const std::exception &) {
              }
            }
          }
        }
      }

      for (const DocumentSymbol &symbol : document_symbols) {
        const bool member = symbol.detail.find("def ") != std::string::npos ||
                            symbol.detail.find("val ") != std::string::npos ||
                            symbol.detail.find("var ") != std::string::npos;
        if (!module_member_context && !typed_member_context &&
            (!member_context || member)) {
          const std::int64_t kind =
              symbol.detail.starts_with("def ") ? 3
              : symbol.detail.starts_with("class ") ||
                      symbol.detail.starts_with("trait ") ||
                      symbol.detail.starts_with("enum ")
                  ? 7
                  : 6;
          add_item(symbol.name, symbol.detail, kind);
        }
      }
      if (!member_context) {
        const std::optional<std::size_t> completion_offset = offset_from_position(
            document->second, static_cast<std::uint32_t>(*line),
            static_cast<std::uint32_t>(*character));
        const std::optional<std::string> expected_type =
            completion_offset
                ? expected_type_at(document->second, *completion_offset)
                : std::nullopt;
        const std::optional<std::size_t> argument_count =
            completion_offset
                ? argument_count_at(document->second, *completion_offset)
                : std::nullopt;
        const std::optional<std::size_t> generic_argument_count =
            completion_offset
                ? generic_argument_count_at(document->second, *completion_offset)
                : std::nullopt;
        std::vector<std::string> imported_modules;
        for (const ast::ImportDeclaration &import :
             semantic_index.documents.front().imports)
          imported_modules.push_back(import.module_name);
        const auto ranked = janus::driver::search_api(
            completion_index,
            {"", {}, {}, {}, expected_type, argument_count,
             std::move(imported_modules), generic_argument_count});
        std::unordered_map<std::string, std::unordered_set<std::string>> origins;
        for (const auto &symbol : completion_index.symbols)
          origins[symbol.simple_name].insert(symbol.package + "\n" +
                                             symbol.required_import);
        for (const auto &result : ranked) {
          const auto &symbol = *result.symbol;
          if (symbol.qualified_name !=
              symbol.module + "." + symbol.simple_name)
            continue;
          const std::int64_t kind =
              symbol.kind == "function" ? 3
              : symbol.kind == "class"  ? 7
              : symbol.kind == "trait"  ? 8
              : symbol.kind == "enum"   ? 13
                                         : 6;
          bool imported = false;
          bool renamed = false;
          bool qualified = false;
          for (const ast::ImportDeclaration &value :
               semantic_index.documents.front().imports) {
            if (value.module_name != symbol.required_import)
              continue;
            qualified = value.is_qualified();
            if (!value.is_qualified() && !value.is_selective())
              imported = true;
            for (const ast::ImportDeclaration::Symbol &item : value.symbols)
              if (item.name == symbol.simple_name) {
                imported = !item.alias.has_value();
                renamed = item.alias.has_value();
              }
          }
          const bool ambiguous = origins[symbol.simple_name].size() > 1;
          if (renamed || qualified)
            continue;
          add_item(symbol.simple_name,
                   symbol.signature + " — from " + symbol.module + " (" +
                       symbol.package + ")",
                   kind,
                   imported || ambiguous || symbol.module.empty()
                       ? std::nullopt
                       : std::optional<std::string>{symbol.required_import});
        }
        for (const std::string_view type : {"int", "double", "byte", "char",
                                            "bool", "string", "unit", "usize"})
          add_item(std::string{type}, "built-in type", 7);
        for (const std::string_view keyword :
             {"const",   "staticAssert", "val", "var", "tailrec", "def", "class", "struct", "trait",
              "enum",    "new",    "move",   "borrow", "consume", "owned",
              "derives", "delete", "defer",  "if",     "else",    "match",
              "for",     "while",  "return", "true",   "false"})
          add_item(std::string{keyword}, "Janus keyword", 14);
      }
      return {response(request_id(*request),
                       llvm::json::Object{{"isIncomplete", false},
                                          {"items", std::move(items)}})};
    }
  }

  if (*method == "textDocument/hover" || *method == "textDocument/definition" ||
      *method == "textDocument/references" ||
      *method == "textDocument/signatureHelp" ||
      *method == "textDocument/implementation" ||
      *method == "textDocument/prepareRename")
    return {response(request_id(*request), nullptr)};

  if (request->get("id") != nullptr)
    return {error_response(request_id(*request), -32601, "Method not found")};
  return {};
}

} // namespace janus::lsp
