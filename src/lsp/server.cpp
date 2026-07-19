#include "janus/lsp/server.hpp"

#include "janus/diagnostics/compile_error.hpp"
#include "janus/frontend/parser.hpp"
#include "janus/semantic/analyzer.hpp"

#include <llvm/Support/JSON.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <string>
#include <utility>

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

} // namespace

namespace janus::lsp {

std::string Server::diagnostics(std::string_view uri,
                                std::string_view source) const {
  llvm::json::Array items;
  try {
    frontend::Parser parser{source};
    const ast::Program program = parser.parse_program();
    static_cast<void>(semantic::Analyzer{}.analyze(program));
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
        {"message", error.what()},
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
        {"message", error.what()},
    });
  }

  return serialize(llvm::json::Object{
      {"jsonrpc", "2.0"},
      {"method", "textDocument/publishDiagnostics"},
      {"params",
       llvm::json::Object{
           {"uri", std::string{uri}},
           {"diagnostics", std::move(items)},
       }},
  });
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
    return {diagnostics(*uri, "")};
  }

  if (request->get("id") != nullptr)
    return {error_response(request_id(*request), -32601, "Method not found")};
  return {};
}

} // namespace janus::lsp
