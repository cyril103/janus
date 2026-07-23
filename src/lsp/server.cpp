#include "janus/lsp/server.hpp"

#include "janus/diagnostics/compile_error.hpp"
#include "janus/driver/formatter.hpp"
#include "janus/frontend/lexer.hpp"
#include "janus/frontend/module_loader.hpp"
#include "janus/frontend/parser.hpp"
#include "janus/semantic/analyzer.hpp"

#include <llvm/Support/JSON.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
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
      {"error", llvm::json::Object{{"code", code},
                                    {"message", std::move(message)}}},
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

struct DocumentSymbol {
  std::string name;
  std::string detail;
  janus::SourceLocation location;
  bool is_global{};
  bool is_private{};
  std::optional<std::string> module_name;
};

std::vector<janus::frontend::Token> tokens(std::string_view source) {
  std::vector<janus::frontend::Token> result;
  janus::frontend::Lexer lexer{source};
  while (true) {
    const janus::frontend::Token token = lexer.next();
    result.push_back(token);
    if (token.kind == janus::frontend::TokenKind::End)
      return result;
  }
}

std::vector<DocumentSymbol> symbols(std::string_view source) {
  using janus::frontend::Token;
  using janus::frontend::TokenKind;
  const std::vector<Token> document_tokens = tokens(source);
  std::vector<DocumentSymbol> result;
  std::optional<std::string> module_name;
  try {
    janus::frontend::Parser parser{source};
    module_name = parser.parse_program().module_name;
  } catch (const std::exception &) {
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
    if (token.kind == TokenKind::Val || token.kind == TokenKind::Var) {
      detail = token.kind == TokenKind::Val ? "val " : "var ";
      detail += std::string{name.lexeme};
      if (index + 3 < document_tokens.size() &&
          document_tokens[index + 2].kind == TokenKind::Colon &&
          document_tokens[index + 3].kind == TokenKind::Identifier)
        detail += " : " + std::string{document_tokens[index + 3].lexeme};
    } else if (token.kind == TokenKind::Def) {
      detail = "def " + std::string{name.lexeme};
    } else if (token.kind == TokenKind::Class ||
               token.kind == TokenKind::Struct) {
      detail = "class " + std::string{name.lexeme};
    } else if (token.kind == TokenKind::Trait) {
      detail = "trait " + std::string{name.lexeme};
    } else if (token.kind == TokenKind::Enum) {
      detail = "enum " + std::string{name.lexeme};
    } else {
      continue;
    }
    const bool is_global =
        brace_depth == 0 &&
        (token.kind == TokenKind::Val || token.kind == TokenKind::Var);
    const bool is_private =
        is_global && index != 0 &&
        document_tokens[index - 1].kind == TokenKind::Private;
    if (is_private)
      detail = "private " + detail;
    result.push_back(DocumentSymbol{std::string{name.lexeme}, std::move(detail),
                                    name.location, is_global, is_private,
                                    is_global ? module_name : std::nullopt});
  }
  return result;
}

std::optional<std::string> identifier_at(std::string_view source,
                                         std::uint32_t line,
                                         std::uint32_t column) {
  using janus::frontend::TokenKind;
  for (const janus::frontend::Token &token : tokens(source)) {
    if (token.kind == TokenKind::Identifier &&
        token.location.line - 1 == line) {
      const std::uint32_t start = token.location.column - 1;
      const std::uint32_t end =
          start + static_cast<std::uint32_t>(token.lexeme.size());
      if (column >= start && column <= end)
        return std::string{token.lexeme};
    }
  }
  return std::nullopt;
}

llvm::json::Object range(const janus::SourceLocation &location,
                         std::size_t length) {
  const std::uint32_t line = location.line > 0 ? location.line - 1 : 0;
  const std::uint32_t column = location.column > 0 ? location.column - 1 : 0;
  return llvm::json::Object{
      {"start", position(line, column)},
      {"end", position(line, column + static_cast<std::uint32_t>(length))},
  };
}

std::optional<char> character_before(std::string_view source,
                                     std::uint32_t requested_line,
                                     std::uint32_t requested_column) {
  std::uint32_t line = 0;
  std::uint32_t column = 0;
  for (std::size_t index = 0; index < source.size(); ++index) {
    if (line == requested_line && column == requested_column)
      return index == 0 ? std::nullopt : std::optional<char>{source[index - 1]};
    if (source[index] == '\n') {
      ++line;
      column = 0;
    } else {
      ++column;
    }
  }
  if (line == requested_line && column == requested_column && !source.empty())
    return source.back();
  return std::nullopt;
}

std::optional<std::string>
member_qualifier_before(std::string_view source, std::uint32_t requested_line,
                        std::uint32_t requested_column) {
  std::size_t line_start = 0;
  for (std::uint32_t line = 0; line < requested_line; ++line) {
    line_start = source.find('\n', line_start);
    if (line_start == std::string_view::npos)
      return std::nullopt;
    ++line_start;
  }
  const std::size_t cursor = line_start + requested_column;
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

  std::string decoded;
  const std::string_view encoded = uri.substr(prefix.size());
  decoded.reserve(encoded.size());
  const auto hex_value = [](char character) -> std::optional<unsigned char> {
    if (character >= '0' && character <= '9')
      return static_cast<unsigned char>(character - '0');
    if (character >= 'a' && character <= 'f')
      return static_cast<unsigned char>(character - 'a' + 10);
    if (character >= 'A' && character <= 'F')
      return static_cast<unsigned char>(character - 'A' + 10);
    return std::nullopt;
  };
  for (std::size_t index = 0; index < encoded.size(); ++index) {
    if (encoded[index] == '%' && index + 2 < encoded.size()) {
      const std::optional<unsigned char> high = hex_value(encoded[index + 1]);
      const std::optional<unsigned char> low = hex_value(encoded[index + 2]);
      if (high && low) {
        decoded.push_back(static_cast<char>((*high << 4U) | *low));
        index += 2;
        continue;
      }
    }
    decoded.push_back(encoded[index]);
  }
#ifdef _WIN32
  if (decoded.size() >= 3 && decoded[0] == '/' && decoded[2] == ':')
    decoded.erase(decoded.begin());
#endif
  return std::filesystem::path{decoded};
}

} // namespace

namespace janus::lsp {

Server::Server(std::vector<std::filesystem::path> module_search_paths)
    : module_search_paths_{std::move(module_search_paths)} {}

std::string Server::diagnostics(std::string_view uri,
                                std::string_view source) const {
  llvm::json::Array items;
  try {
    ast::Program program;
    if (const std::optional<std::filesystem::path> path = file_uri_path(uri)) {
      frontend::ModuleLoader loader{module_search_paths_};
      program = loader.load(*path, source);
    } else {
      frontend::Parser parser{source};
      program = parser.parse_program();
    }
    const bool is_module = program.module_name.has_value();
    static_cast<void>(semantic::Analyzer{}.analyze(
        program, semantic::AnalysisOptions{!is_module}));
  } catch (const CompileError &error) {
    const SourceLocation location = error.location();
    const std::uint32_t line = location.line > 0 ? location.line - 1 : 0;
    const std::uint32_t column = location.column > 0 ? location.column - 1 : 0;
    items.emplace_back(llvm::json::Object{
        {"range",
         llvm::json::Object{
             {"start", position(line, column)},
             {"end", position(line, column + 1)},
         }},
        {"severity", 1},
        {"source", "janus"},
        {"message", std::string{error.what()}},
    });
  } catch (const std::exception &error) {
    items.emplace_back(llvm::json::Object{
        {"range",
         llvm::json::Object{
             {"start", position(0, 0)},
             {"end", position(0, 1)},
         }},
        {"severity", 1},
        {"source", "janus"},
        {"message", std::string{error.what()}},
    });
  }

  return publish_diagnostics(uri, std::move(items));
}

std::vector<std::string> Server::handle(std::string_view message) {
  llvm::Expected<llvm::json::Value> parsed = llvm::json::parse(message);
  if (!parsed)
    return {error_response(nullptr, -32700, "Parse error")};
  llvm::json::Object *request = parsed->getAsObject();
  if (request == nullptr)
    return {error_response(nullptr, -32600, "Invalid Request")};

  const std::optional<llvm::StringRef> method = request->getString("method");
  if (!method)
    return {error_response(request_id(*request), -32600, "Invalid Request")};

  if (*method == "initialize") {
    return {response(
        request_id(*request),
        llvm::json::Object{
            {"capabilities",
             llvm::json::Object{
                 {"textDocumentSync",
                  llvm::json::Object{{"openClose", true}, {"change", 1}}},
                 {"hoverProvider", true},
                 {"definitionProvider", true},
                 {"referencesProvider", true},
                 {"completionProvider",
                  llvm::json::Object{
                      {"triggerCharacters", llvm::json::Array{".", ":"}},
                  }},
                 {"documentFormattingProvider", true},
             }},
            {"serverInfo",
             llvm::json::Object{{"name", "janus-lsp"},
                                {"version", JANUS_VERSION}}},
        })};
  }
  if (*method == "shutdown") {
    shutdown_ = true;
    return {response(request_id(*request), nullptr)};
  }
  if (*method == "exit")
    return {};

  const llvm::json::Object *params = request->getObject("params");
  const llvm::json::Object *text_document =
      params == nullptr ? nullptr : params->getObject("textDocument");
  const std::optional<llvm::StringRef> uri =
      text_document == nullptr ? std::nullopt
                               : text_document->getString("uri");

  if (*method == "textDocument/didOpen" && uri) {
    const std::optional<llvm::StringRef> text =
        text_document->getString("text");
    if (text) {
      documents_.insert_or_assign(uri->str(), text->str());
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
        documents_.insert_or_assign(uri->str(), text->str());
        return {diagnostics(*uri, *text)};
      }
    }
    return {};
  }
  if (*method == "textDocument/didClose" && uri) {
    documents_.erase(uri->str());
    return {publish_diagnostics(*uri, {})};
  }

  const auto open_document =
      uri ? documents_.find(uri->str()) : documents_.end();
  if (*method == "textDocument/formatting" && uri &&
      open_document != documents_.end()) {
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
        driver::format_source(open_document->second, options);
    const std::int64_t line_count = static_cast<std::int64_t>(
        std::count(open_document->second.begin(), open_document->second.end(),
                   '\n') +
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
    const std::optional<std::string> identifier = identifier_at(
        document->second, static_cast<std::uint32_t>(*line),
        static_cast<std::uint32_t>(*character));
    const std::vector<DocumentSymbol> document_symbols =
        symbols(document->second);
    const auto locate_symbol =
        [&](std::string_view name)
        -> std::optional<std::pair<std::string, DocumentSymbol>> {
      for (const DocumentSymbol &symbol : document_symbols)
        if (symbol.name == name)
          return std::pair<std::string, DocumentSymbol>{uri->str(), symbol};
      for (const auto &[candidate_uri, candidate_source] : documents_) {
        if (candidate_uri == uri->str())
          continue;
        for (const DocumentSymbol &symbol : symbols(candidate_source))
          if (symbol.is_global && !symbol.is_private && symbol.name == name)
            return std::pair<std::string, DocumentSymbol>{candidate_uri,
                                                          symbol};
      }
      return std::nullopt;
    };
    if (*method == "textDocument/hover") {
      if (identifier) {
        if (const auto located = locate_symbol(*identifier)) {
          const DocumentSymbol &symbol = located->second;
          std::string detail = symbol.detail;
          if (symbol.is_global && symbol.module_name.has_value())
            detail += "\n\nmodule `" + *symbol.module_name + "`";
          return {response(
              request_id(*request),
              llvm::json::Object{
                  {"contents",
                   llvm::json::Object{
                       {"kind", "markdown"},
                       {"value", "```janus\n" + detail + "\n```"}}},
                  {"range", range(symbol.location, symbol.name.size())},
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
                  {"range", range(symbol.location, symbol.name.size())},
              })};
        }
      }
      return {response(request_id(*request), nullptr)};
    }
    if (*method == "textDocument/references") {
      if (identifier) {
        llvm::json::Array references;
        for (const auto &[document_uri, document_source] : documents_) {
          for (const frontend::Token &token : tokens(document_source)) {
            if (token.kind == frontend::TokenKind::Identifier &&
                token.lexeme == *identifier) {
              references.emplace_back(llvm::json::Object{
                  {"uri", document_uri},
                  {"range", range(token.location, token.lexeme.size())},
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
                                std::int64_t kind) {
        if (std::find(labels.begin(), labels.end(), label) != labels.end())
          return;
        labels.push_back(label);
        items.emplace_back(llvm::json::Object{
            {"label", std::move(label)},
            {"kind", kind},
            {"detail", std::move(detail)},
        });
      };

      bool module_member_context = false;
      if (member_qualifier.has_value()) {
        for (const auto &[candidate_uri, candidate_source] : documents_) {
          static_cast<void>(candidate_uri);
          for (const DocumentSymbol &symbol : symbols(candidate_source)) {
            if (symbol.is_global && !symbol.is_private &&
                symbol.module_name == member_qualifier) {
              module_member_context = true;
              add_item(symbol.name, symbol.detail, 6);
            }
          }
        }
      }

      for (const DocumentSymbol &symbol : document_symbols) {
        const bool member =
            symbol.detail.find("def ") != std::string::npos ||
            symbol.detail.find("val ") != std::string::npos ||
            symbol.detail.find("var ") != std::string::npos;
        if (!module_member_context && (!member_context || member)) {
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
        for (const auto &[candidate_uri, candidate_source] : documents_) {
          if (candidate_uri == uri->str())
            continue;
          for (const DocumentSymbol &symbol : symbols(candidate_source))
            if (symbol.is_global && !symbol.is_private)
              add_item(symbol.name, symbol.detail, 6);
        }
        for (const std::string_view type :
             {"int", "double", "byte", "char", "bool", "string", "unit",
              "usize"})
          add_item(std::string{type}, "built-in type", 7);
        for (const std::string_view keyword :
             {"val", "var", "def", "class", "struct", "trait", "enum", "new",
              "delete", "defer", "if", "else", "match", "for", "while",
              "return", "true", "false"})
          add_item(std::string{keyword}, "Janus keyword", 14);
      }
      return {response(
          request_id(*request),
          llvm::json::Object{{"isIncomplete", false},
                             {"items", std::move(items)}})};
    }
  }

  if (*method == "textDocument/hover" ||
      *method == "textDocument/definition" ||
      *method == "textDocument/references")
    return {response(request_id(*request), nullptr)};

  if (request->get("id") != nullptr)
    return {error_response(request_id(*request), -32601, "Method not found")};
  return {};
}

} // namespace janus::lsp
