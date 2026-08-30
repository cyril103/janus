#include "janus/driver/incremental_cache.hpp"
#include "janus/driver/output_publication_lock.hpp"

#include "../frontend/module_resolution.hpp"
#include "janus/ast/ast.hpp"
#include "janus/constant/evaluator.hpp"
#include "janus/frontend/parser.hpp"
#include "janus/semantic/analyzer.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <random>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <tuple>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <unistd.h>
#endif

namespace janus::driver {
namespace {

std::string read_file(const std::filesystem::path &path) {
  std::ifstream input{path, std::ios::binary};
  if (!input)
    throw std::runtime_error{"cannot open module source '" + path.string() +
                             "'"};
  return {std::istreambuf_iterator<char>{input},
          std::istreambuf_iterator<char>{}};
}

void append_field(std::string &result, std::string_view name,
                  std::string_view value) {
  result += name;
  result += ':';
  result += std::to_string(value.size());
  result += ':';
  result += value;
  result += ';';
}

std::string canonical_identity(const BuildFingerprintInput &input,
                               bool include_implementation) {
  std::string result;
  append_field(result, "version", input.janus_version);
  append_field(result, "target", input.target);
  std::vector<std::string> sorted_options = input.options;
  std::sort(sorted_options.begin(), sorted_options.end());
  for (const std::string &option : sorted_options)
    append_field(result, "option", option);
  append_field(result, "source-path", input.source_path);
  append_field(result, "source", input.source);
  std::vector<const DependencyFingerprintInput *> dependencies;
  dependencies.reserve(input.dependencies.size());
  for (const DependencyFingerprintInput &dependency : input.dependencies)
    dependencies.push_back(&dependency);
  std::sort(dependencies.begin(), dependencies.end(),
            [](const auto *left, const auto *right) {
              return std::tie(left->name, left->import_name) <
                     std::tie(right->name, right->import_name);
            });
  for (const DependencyFingerprintInput *dependency : dependencies) {
    append_field(result, "dependency", dependency->name);
    append_field(result, "import-name", dependency->import_name);
    append_field(result, "interface", dependency->public_interface);
    if (include_implementation)
      append_field(result, "implementation", dependency->implementation);
  }
  return result;
}

std::string hex_identity(std::string_view canonical) {
  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (const unsigned char byte : canonical)
    output << std::setw(2) << static_cast<unsigned>(byte);
  return output.str();
}

void append_type(std::string &output, const ast::TypeReference &type) {
  output += type.name;
  if (!type.type_arguments.empty()) {
    output += '[';
    for (std::size_t index = 0; index < type.type_arguments.size(); ++index) {
      if (index != 0)
        output += ',';
      append_type(output, type.type_arguments[index]);
    }
    output += ']';
  }
}

void append_type(std::string &output,
                 const std::optional<ast::TypeReference> &type) {
  if (type)
    append_type(output, *type);
}

void append_constraints(std::string &output,
                        const std::vector<ast::TypeConstraint> &constraints) {
  for (const auto &constraint : constraints) {
    output += '|';
    output += constraint.parameter;
    output += "<:";
    append_type(output, constraint.trait);
  }
}

void append_type_parameters(std::string &output,
                            const std::vector<std::string> &parameters) {
  output += '[';
  for (const std::string &parameter : parameters) {
    output += parameter;
    output += ',';
  }
  output += ']';
}

void append_derivations(std::string &output,
                        const std::vector<ast::Derivation> &derivations) {
  output += ":derive[";
  for (const auto &derivation : derivations) {
    output += std::to_string(static_cast<int>(derivation.kind));
    output += ',';
  }
  output += ']';
}

void append_function(std::string &output,
                     const ast::FunctionDeclaration &function) {
  if (function.is_private || function.is_internal)
    return;
  output += "fn:";
  output += function.is_constant ? "const:" : "runtime:";
  output += function.name;
  append_type_parameters(output, function.type_parameters);
  output += '(';
  for (const auto &parameter : function.parameters) {
    output += parameter.is_scoped ? "scoped:" : "escaping:";
    output += std::to_string(static_cast<int>(parameter.ownership));
    output += ':';
    output += parameter.name;
    output += ':';
    append_type(output, parameter.type);
    output += ',';
  }
  output += ")->";
  output += std::to_string(static_cast<int>(function.return_ownership));
  output += ':';
  append_type(output, function.return_type);
  output += function.is_consuming ? ":consuming" : "";
  output += function.is_borrowing ? ":borrowing" : "";
  output += function.is_tailrec ? ":tailrec" : "";
  output += function.is_external ? ":external" : "";
  if (function.external_symbol.has_value()) {
    output += ":symbol=";
    output += *function.external_symbol;
  }
  output += function.is_variadic ? ":variadic" : "";
  append_constraints(output, function.type_constraints);
  output += ';';
}

std::string_view declaration_source(std::string_view source,
                                    std::size_t offset) {
  if (offset >= source.size())
    return {};
  const std::size_t opening = source.find('{', offset);
  const std::size_t terminator = source.find(';', offset);
  if (terminator != std::string_view::npos &&
      (opening == std::string_view::npos || terminator < opening))
    return source.substr(offset, terminator - offset + 1);
  if (opening == std::string_view::npos)
    return source.substr(offset);
  std::size_t depth = 0;
  bool quoted = false;
  bool character = false;
  bool escaped = false;
  bool line_comment = false;
  bool block_comment = false;
  for (std::size_t index = opening; index < source.size(); ++index) {
    const char current = source[index];
    const char next = index + 1 < source.size() ? source[index + 1] : '\0';
    if (line_comment) {
      line_comment = current != '\n';
      continue;
    }
    if (block_comment) {
      if (current == '*' && next == '/') {
        block_comment = false;
        ++index;
      }
      continue;
    }
    if (quoted || character) {
      if (escaped)
        escaped = false;
      else if (current == '\\')
        escaped = true;
      else if ((quoted && current == '"') || (character && current == '\'')) {
        quoted = false;
        character = false;
      }
      continue;
    }
    if (current == '/' && next == '/') {
      line_comment = true;
      ++index;
    } else if (current == '/' && next == '*') {
      block_comment = true;
      ++index;
    } else if (current == '"') {
      quoted = true;
    } else if (current == '\'') {
      character = true;
    } else if (current == '{') {
      ++depth;
    } else if (current == '}' && --depth == 0) {
      return source.substr(offset, index - offset + 1);
    }
  }
  return source.substr(offset);
}

std::string public_interface(std::string_view source) {
  frontend::Parser parser{source};
  const ast::Program program = parser.parse_program();
  semantic::AnalysisResult analysis;
  const bool has_public_constant =
      std::any_of(program.globals.begin(), program.globals.end(),
                  [](const ast::GlobalDeclaration &global) {
                    return global.declaration.is_constant &&
                           !global.declaration.is_private &&
                           !global.declaration.is_internal;
                  });
  if (has_public_constant && program.imports.empty()) {
    semantic::Analyzer analyzer;
    analysis = analyzer.analyze(
        program,
        semantic::AnalysisOptions{.require_entry_point = false, .target = {}});
  }
  std::string output;
  if (program.module_name)
    append_field(output, "module", *program.module_name);
  for (const auto &global : program.globals) {
    if (global.declaration.is_private || global.declaration.is_internal)
      continue;
    output += "global:";
    output += global.declaration.name;
    output += ':';
    append_type(output, global.declaration.declared_type);
    output += global.declaration.is_mutable ? ":mutable;" : ":immutable;";
    if (global.declaration.is_constant) {
      const std::string key =
          global.module_name.has_value()
              ? *global.module_name + "." + global.declaration.name
              : global.declaration.name;
      if (const auto value = analysis.global_constant_values.find(key);
          value != analysis.global_constant_values.end()) {
        output += ":const-value:";
        output += constant::canonical_serialize(value->second);
      } else {
        output += ":const-expression:";
        output +=
            declaration_source(source, global.declaration.location.offset);
      }
      output += ':';
      output += constant::evaluator_version;
      output += ":target=pointer64;";
    }
  }
  for (const auto &trait : program.traits) {
    if (trait.is_private)
      continue;
    output += "trait:";
    output += trait.name;
    append_type_parameters(output, trait.type_parameters);
    append_constraints(output, trait.type_constraints);
    output += '{';
    for (const auto &associated : trait.associated_types) {
      output += "type:";
      output += associated.name;
      output += ';';
    }
    for (const auto &method : trait.methods)
      append_function(output, method);
    output += '}';
  }
  for (const auto &enumeration : program.enums) {
    if (enumeration.is_private)
      continue;
    output += "enum:";
    output += enumeration.name;
    append_type_parameters(output, enumeration.type_parameters);
    append_derivations(output, enumeration.derivations);
    output += '{';
    for (const auto &entry : enumeration.cases) {
      output += entry.name;
      output += '=';
      output += std::to_string(entry.value);
      for (const auto &payload : entry.payload_types) {
        output += ':';
        append_type(output, payload);
      }
      output += ';';
    }
    output += '}';
  }
  for (const auto &class_declaration : program.classes) {
    if (class_declaration.is_private)
      continue;
    output += class_declaration.is_value_type ? "struct:" : "class:";
    output += class_declaration.name;
    append_type_parameters(output, class_declaration.type_parameters);
    append_constraints(output, class_declaration.type_constraints);
    append_derivations(output, class_declaration.derivations);
    output += class_declaration.is_constructor_internal
                  ? ":constructor-internal"
                  : ":constructor-public";
    output += ":implements[";
    for (const auto &trait : class_declaration.implemented_traits) {
      append_type(output, trait);
      output += ',';
    }
    output += ']';
    for (const auto &associated : class_declaration.associated_types) {
      output += ":associated:";
      output += associated.name;
      output += '=';
      append_type(output, associated.definition);
    }
    output += '(';
    for (const auto &parameter : class_declaration.constructor_parameters) {
      output += parameter.name;
      output += ':';
      append_type(output, parameter.type);
      output += ',';
    }
    for (const auto &field : class_declaration.constructor_fields) {
      output += "layout-field:";
      output += field.name;
      output += ':';
      append_type(output, field.declared_type);
      output += field.is_mutable ? ":mutable;" : ":immutable;";
      output += field.is_borrowed ? ":borrowed;" : ":owned;";
      if (!field.is_private && !field.is_internal) {
        output += "field-api:";
        output += field.name;
        output += ':';
        append_type(output, field.declared_type);
        output += field.is_mutable ? ":mutable;" : ":immutable;";
      }
    }
    output += "){";
    for (const auto &field : class_declaration.fields) {
      output += "layout-field:";
      output += field.name;
      output += ':';
      append_type(output, field.declared_type);
      output += field.is_mutable ? ":mutable;" : ":immutable;";
      output += field.is_borrowed ? ":borrowed;" : ":owned;";
      if (!field.is_private && !field.is_internal) {
        output += "field-api:";
        output += field.name;
        output += ':';
        append_type(output, field.declared_type);
        output += field.is_mutable ? ":mutable;" : ":immutable;";
      }
    }
    for (const auto &method : class_declaration.methods)
      append_function(output, method);
    output += '}';
  }
  for (const auto &extension : program.extensions) {
    if (extension.is_private)
      continue;
    output += "extension:";
    append_type(output, extension.target_type);
    append_type_parameters(output, extension.type_parameters);
    output += '{';
    for (std::size_t index = 0; index < extension.methods.size(); ++index) {
      output += ":receiver=" + std::to_string(static_cast<int>(
                                   extension.receiver_ownerships[index]));
      append_function(output, extension.methods[index]);
    }
    output += '}';
  }
  for (const auto &function : program.functions)
    append_function(output, function);
  // Generic bodies are compiled at their use sites. Include exactly those
  // declarations, not unrelated private source from the same module.
  for (const auto &function : program.functions)
    if (!function.is_private && !function.is_internal &&
        (function.is_constant || !function.type_parameters.empty()))
      append_field(output,
                   function.is_constant ? "constant-implementation"
                                        : "generic-implementation",
                   declaration_source(source, function.location.offset));
  for (const auto &class_declaration : program.classes) {
    if (class_declaration.is_private)
      continue;
    if (!class_declaration.type_parameters.empty()) {
      append_field(
          output, "generic-class-implementation",
          declaration_source(source, class_declaration.location.offset));
      continue;
    }
    for (const auto &method : class_declaration.methods)
      if (!method.is_private && !method.is_internal &&
          !method.type_parameters.empty())
        append_field(output, "generic-method-implementation",
                     declaration_source(source, method.location.offset));
  }
  for (const auto &extension : program.extensions)
    if (!extension.is_private &&
        (!extension.type_parameters.empty() ||
         std::any_of(extension.methods.begin(), extension.methods.end(),
                     [](const ast::FunctionDeclaration &method) {
                       return !method.type_parameters.empty();
                     })))
      append_field(output, "generic-extension-implementation",
                   declaration_source(source, extension.location.offset));
  return output;
}

void inspect_dependency(const std::filesystem::path &path,
                        std::string import_name,
                        const std::filesystem::path &project_root,
                        const std::vector<std::filesystem::path> &search_paths,
                        std::vector<std::filesystem::path> &visited,
                        std::vector<DependencyFingerprintInput> &dependencies) {
  const auto normalized = std::filesystem::absolute(path).lexically_normal();
  if (std::find(visited.begin(), visited.end(), normalized) != visited.end())
    return;
  visited.push_back(normalized);
  const std::string source = read_file(normalized);
  frontend::Parser parser{source};
  const ast::Program program = parser.parse_program();
  for (const ast::ImportDeclaration &import : program.imports)
    inspect_dependency(frontend::detail::resolve_module_import(
                           import.module_name, project_root, search_paths),
                       import.module_name, project_root, search_paths, visited,
                       dependencies);
  dependencies.push_back(
      {program.module_name.value_or(normalized.generic_string()),
       stable_digest(public_interface(source)), stable_digest(source), source,
       std::move(import_name)});
}

std::string file_digest(const std::filesystem::path &path) {
  return stable_digest(read_file(path));
}

std::filesystem::path
unique_temporary_path(const std::filesystem::path &destination) {
  static std::atomic<std::uint64_t> sequence{};
  static thread_local std::mt19937_64 random{std::random_device{}()};
  if (!destination.parent_path().empty())
    std::filesystem::create_directories(destination.parent_path());
  for (int attempt = 0; attempt < 100; ++attempt) {
    const auto nonce =
        random() ^ sequence.fetch_add(1, std::memory_order_relaxed) ^
        static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path directory =
        destination.string() + ".tmp-" + std::to_string(nonce);
    std::error_code error;
    if (std::filesystem::create_directory(directory, error))
      return directory / "payload";
    if (error && error != std::errc::file_exists)
      throw std::runtime_error{"cannot reserve cache temporary: " +
                               error.message()};
  }
  throw std::runtime_error{"cannot reserve a unique cache temporary"};
}

void publish_immutable(const std::filesystem::path &temporary,
                       const std::filesystem::path &destination,
                       std::string_view description,
                       bool accept_existing = false) {
  std::error_code error;
#ifdef _WIN32
  if (!MoveFileExW(temporary.c_str(), destination.c_str(),
                   MOVEFILE_WRITE_THROUGH))
    error = std::error_code{static_cast<int>(GetLastError()),
                            std::system_category()};
#else
  if (::link(temporary.c_str(), destination.c_str()) != 0) {
    error = std::error_code{errno, std::generic_category()};
  } else if (::unlink(temporary.c_str()) != 0) {
    error = std::error_code{errno, std::generic_category()};
  }
#endif
  if (error && std::filesystem::is_regular_file(destination)) {
    const bool identical = file_digest(temporary) == file_digest(destination);
    if (identical || accept_existing) {
      error.clear();
      std::error_code remove_error;
      std::filesystem::remove(temporary, remove_error);
      if (remove_error)
        error = remove_error;
    }
  }
  const std::filesystem::path reservation = temporary.parent_path();
  std::error_code cleanup_error;
  std::filesystem::remove_all(reservation, cleanup_error);
  if (error)
    throw std::runtime_error{"cannot publish " + std::string{description} +
                             " '" + destination.string() +
                             "': " + error.message()};
}

void replace_temporary(const std::filesystem::path &temporary,
                       const std::filesystem::path &destination,
                       std::string_view description) {
  std::error_code error;
#ifdef _WIN32
  if (!MoveFileExW(temporary.c_str(), destination.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    error = std::error_code{static_cast<int>(GetLastError()),
                            std::system_category()};
#else
  std::filesystem::rename(temporary, destination, error);
#endif
  const std::filesystem::path reservation = temporary.parent_path();
  std::error_code cleanup_error;
  std::filesystem::remove_all(reservation, cleanup_error);
  if (error)
    throw std::runtime_error{"cannot replace " + std::string{description} +
                             " '" + destination.string() +
                             "': " + error.message()};
}

std::string atomic_copy_immutable(const std::filesystem::path &source,
                                  const std::filesystem::path &destination) {
  const auto temporary = unique_temporary_path(destination);
  try {
    std::filesystem::copy_file(source, temporary);
    publish_immutable(temporary, destination, "cached artifact", true);
    return file_digest(destination);
  } catch (...) {
    std::error_code ignored;
    std::filesystem::remove_all(temporary.parent_path(), ignored);
    throw;
  }
}

bool atomic_replace_copy_if_digest(const std::filesystem::path &source,
                                   const std::filesystem::path &destination,
                                   std::string_view expected_digest) {
  const auto temporary = unique_temporary_path(destination);
  try {
    std::filesystem::copy_file(source, temporary);
    if (file_digest(temporary) != expected_digest) {
      std::error_code ignored;
      std::filesystem::remove_all(temporary.parent_path(), ignored);
      return false;
    }
    OutputPublicationLock publication_lock{destination};
    replace_temporary(temporary, destination, "output");
    return true;
  } catch (...) {
    std::error_code ignored;
    std::filesystem::remove_all(temporary.parent_path(), ignored);
    throw;
  }
}

void atomic_write(const std::filesystem::path &destination,
                  std::string_view contents) {
  const auto temporary = unique_temporary_path(destination);
  try {
    {
      std::ofstream output{temporary, std::ios::binary};
      if (!output)
        throw std::runtime_error{"cannot create cache metadata"};
      output << contents;
      if (!output)
        throw std::runtime_error{"cannot write cache metadata"};
    }
    publish_immutable(temporary, destination, "cache metadata");
  } catch (...) {
    std::error_code ignored;
    std::filesystem::remove_all(temporary.parent_path(), ignored);
    throw;
  }
}

std::string metadata_value(std::string_view metadata, std::string_view name) {
  const std::string prefix = std::string{name} + '=';
  const std::size_t start = metadata.find(prefix);
  if (start == std::string_view::npos ||
      (start != 0 && metadata[start - 1] != '\n'))
    return {};
  const std::size_t value_start = start + prefix.size();
  const std::size_t end = metadata.find('\n', value_start);
  return std::string{metadata.substr(value_start, end - value_start)};
}

std::vector<std::string> metadata_values(std::string_view metadata,
                                         std::string_view name) {
  std::vector<std::string> values;
  const std::string prefix = std::string{name} + '=';
  std::size_t start = 0;
  while ((start = metadata.find(prefix, start)) != std::string_view::npos) {
    if (start == 0 || metadata[start - 1] == '\n') {
      const std::size_t value_start = start + prefix.size();
      const std::size_t end = metadata.find('\n', value_start);
      values.emplace_back(metadata.substr(value_start, end - value_start));
    }
    start += prefix.size();
  }
  return values;
}

void validate_digest_key(std::string_view key) {
  if (key.size() != 64 ||
      !std::all_of(key.begin(), key.end(), [](const unsigned char character) {
        return std::isdigit(character) != 0 ||
               (character >= 'a' && character <= 'f');
      }))
    throw std::invalid_argument{"invalid incremental-cache key"};
}

} // namespace

std::string stable_digest(std::string_view input) {
  static constexpr std::array<std::uint32_t, 64> constants{
      0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU,
      0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U,
      0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U,
      0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
      0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U,
      0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
      0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
      0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
      0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U,
      0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U, 0x1e376c08U,
      0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU,
      0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
      0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};
  auto rotate_right = [](std::uint32_t value, unsigned count) {
    return (value >> count) | (value << (32U - count));
  };

  std::vector<std::uint8_t> message(input.begin(), input.end());
  const std::uint64_t bit_length =
      static_cast<std::uint64_t>(message.size()) * 8U;
  message.push_back(0x80U);
  while (message.size() % 64U != 56U)
    message.push_back(0U);
  for (int shift = 56; shift >= 0; shift -= 8)
    message.push_back(static_cast<std::uint8_t>(bit_length >> shift));

  std::array<std::uint32_t, 8> hash{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U,
                                    0xa54ff53aU, 0x510e527fU, 0x9b05688cU,
                                    0x1f83d9abU, 0x5be0cd19U};
  for (std::size_t offset = 0; offset < message.size(); offset += 64U) {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t index = 0; index < 16U; ++index) {
      const std::size_t byte = offset + index * 4U;
      words[index] = static_cast<std::uint32_t>(message[byte]) << 24U |
                     static_cast<std::uint32_t>(message[byte + 1U]) << 16U |
                     static_cast<std::uint32_t>(message[byte + 2U]) << 8U |
                     static_cast<std::uint32_t>(message[byte + 3U]);
    }
    for (std::size_t index = 16U; index < words.size(); ++index) {
      const std::uint32_t s0 = rotate_right(words[index - 15U], 7U) ^
                               rotate_right(words[index - 15U], 18U) ^
                               (words[index - 15U] >> 3U);
      const std::uint32_t s1 = rotate_right(words[index - 2U], 17U) ^
                               rotate_right(words[index - 2U], 19U) ^
                               (words[index - 2U] >> 10U);
      words[index] = words[index - 16U] + s0 + words[index - 7U] + s1;
    }

    std::uint32_t a = hash[0];
    std::uint32_t b = hash[1];
    std::uint32_t c = hash[2];
    std::uint32_t d = hash[3];
    std::uint32_t e = hash[4];
    std::uint32_t f = hash[5];
    std::uint32_t g = hash[6];
    std::uint32_t h = hash[7];
    for (std::size_t index = 0; index < words.size(); ++index) {
      const std::uint32_t sum1 =
          rotate_right(e, 6U) ^ rotate_right(e, 11U) ^ rotate_right(e, 25U);
      const std::uint32_t choice = (e & f) ^ (~e & g);
      const std::uint32_t temporary1 =
          h + sum1 + choice + constants[index] + words[index];
      const std::uint32_t sum0 =
          rotate_right(a, 2U) ^ rotate_right(a, 13U) ^ rotate_right(a, 22U);
      const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
      const std::uint32_t temporary2 = sum0 + majority;
      h = g;
      g = f;
      f = e;
      e = d + temporary1;
      d = c;
      c = b;
      b = a;
      a = temporary1 + temporary2;
    }
    hash[0] += a;
    hash[1] += b;
    hash[2] += c;
    hash[3] += d;
    hash[4] += e;
    hash[5] += f;
    hash[6] += g;
    hash[7] += h;
  }

  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (const std::uint32_t word : hash)
    output << std::setw(8) << word;
  return output.str();
}

std::string build_identity(const BuildFingerprintInput &input) {
  return hex_identity(canonical_identity(input, true));
}

std::string consumer_identity(const BuildFingerprintInput &input) {
  return hex_identity(canonical_identity(input, false));
}

std::string build_fingerprint(const BuildFingerprintInput &input) {
  return stable_digest(canonical_identity(input, true));
}

std::string consumer_fingerprint(const BuildFingerprintInput &input) {
  return stable_digest(canonical_identity(input, false));
}

std::string public_interface_fingerprint(std::string_view source) {
  return stable_digest(public_interface(source));
}

BuildFingerprintInput
inspect_build_inputs(const std::filesystem::path &entry,
                     const std::vector<std::filesystem::path> &search_paths,
                     std::string janus_version, std::string target,
                     std::vector<std::string> options) {
  const auto normalized = std::filesystem::absolute(entry).lexically_normal();
  const std::string source = read_file(normalized);
  frontend::Parser parser{source};
  const ast::Program program = parser.parse_program();
  std::vector<std::filesystem::path> visited{normalized};
  std::vector<DependencyFingerprintInput> dependencies;
  for (const ast::ImportDeclaration &import : program.imports)
    inspect_dependency(
        frontend::detail::resolve_module_import(
            import.module_name, normalized.parent_path(), search_paths),
        import.module_name, normalized.parent_path(), search_paths, visited,
        dependencies);
  std::sort(dependencies.begin(), dependencies.end(),
            [](const auto &left, const auto &right) {
              return left.name < right.name;
            });
  return {std::move(janus_version),
          std::move(target),
          std::move(options),
          normalized.generic_string(),
          source,
          std::move(dependencies)};
}

IncrementalCache::IncrementalCache(std::filesystem::path root)
    : root_{std::move(root)} {}

std::filesystem::path IncrementalCache::entry_path(std::string_view key) const {
  validate_digest_key(key);
  return root_ / "entries" / (std::string{key} + ".entry");
}

std::filesystem::path
IncrementalCache::artifact_path(std::string_view key) const {
  validate_digest_key(key);
  return root_ / "artifacts" / (std::string{key} + ".bin");
}

std::filesystem::path
IncrementalCache::consumer_path(std::string_view key) const {
  validate_digest_key(key);
  return root_ / "consumers" / (std::string{key} + ".bc");
}

bool IncrementalCache::restore_consumer(
    std::string_view key, std::string_view identity,
    const std::filesystem::path &output,
    std::vector<std::string> *required_definitions) const {
  const auto source = consumer_path(key);
  const auto metadata_file =
      root_ / "consumers" / (std::string{key} + ".entry");
  if (!std::filesystem::is_regular_file(source) &&
      !std::filesystem::is_regular_file(metadata_file))
    return false;
  try {
    if (!std::filesystem::is_regular_file(source) ||
        !std::filesystem::is_regular_file(metadata_file)) {
      invalidate_consumer(key);
      return false;
    }
    const std::string metadata = read_file(metadata_file);
    const std::string digest = metadata_value(metadata, "digest");
    const std::vector<std::string> definitions =
        metadata_values(metadata, "definition");
    if (metadata_value(metadata, "schema") != "2" ||
        metadata_value(metadata, "identity") != identity ||
        definitions.empty() || digest != file_digest(source) ||
        !atomic_replace_copy_if_digest(source, output, digest)) {
      invalidate_consumer(key);
      return false;
    }
    if (required_definitions != nullptr)
      *required_definitions = definitions;
    return true;
  } catch (const std::exception &) {
    invalidate_consumer(key);
    return false;
  }
}

void IncrementalCache::store_consumer(
    std::string_view key, std::string_view identity,
    const std::filesystem::path &artifact,
    const std::vector<std::string> &required_definitions) const {
  if (required_definitions.empty() ||
      std::any_of(required_definitions.begin(), required_definitions.end(),
                  [](const std::string &name) {
                    return name.empty() ||
                           name.find_first_of("\r\n") != std::string::npos;
                  }))
    throw std::invalid_argument{"invalid consumer definition manifest"};
  const auto destination = consumer_path(key);
  const std::string digest = atomic_copy_immutable(artifact, destination);
  std::string metadata = "schema=2\nidentity=" + std::string{identity} +
                         "\ndigest=" + digest + "\n";
  for (const std::string &definition : required_definitions)
    metadata += "definition=" + definition + "\n";
  atomic_write(root_ / "consumers" / (std::string{key} + ".entry"), metadata);
}

void IncrementalCache::invalidate_consumer(
    std::string_view key) const noexcept {
  try {
    const auto artifact = consumer_path(key);
    const auto metadata = root_ / "consumers" / (std::string{key} + ".entry");
    const auto legacy_digest =
        root_ / "consumers" / (std::string{key} + ".digest");
    std::error_code ignored;
    std::filesystem::remove(artifact, ignored);
    std::filesystem::remove(metadata, ignored);
    std::filesystem::remove(legacy_digest, ignored);
  } catch (...) {
  }
}

CacheLookup
IncrementalCache::restore(std::string_view key, std::string_view identity,
                          const std::filesystem::path &output) const {
  const auto entry = entry_path(key);
  if (!std::filesystem::is_regular_file(entry))
    return CacheLookup::Miss;
  const std::string metadata = read_file(entry);
  const std::string stored_identity = metadata_value(metadata, "identity");
  const std::string digest = metadata_value(metadata, "digest");
  if (stored_identity != identity)
    return CacheLookup::Miss;
  const std::filesystem::path artifact_file = artifact_path(key);
  if (!std::filesystem::is_regular_file(artifact_file) ||
      file_digest(artifact_file) != digest ||
      !atomic_replace_copy_if_digest(artifact_file, output, digest)) {
    std::error_code ignored;
    std::filesystem::remove(entry, ignored);
    std::filesystem::remove(artifact_file, ignored);
    return CacheLookup::Corrupt;
  }
  return CacheLookup::Hit;
}

void IncrementalCache::store(std::string_view key, std::string_view identity,
                             const std::filesystem::path &artifact,
                             std::string_view consumer) const {
  validate_digest_key(consumer);
  const auto cached_artifact = artifact_path(key);
  const std::string digest = atomic_copy_immutable(artifact, cached_artifact);
  if (file_digest(artifact) != digest &&
      !atomic_replace_copy_if_digest(cached_artifact, artifact, digest))
    throw std::runtime_error{"cannot converge output on cached artifact"};
  std::string metadata = "schema=1\nidentity=" + std::string{identity} +
                         "\ndigest=" + digest + "\n";
  atomic_write(entry_path(key), metadata);
}

} // namespace janus::driver
