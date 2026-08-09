#include "janus/driver/remote_registry.hpp"

#include "janus/driver/semver.hpp"
#include "janus/driver/temporary_directory.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include <llvm/ADT/ArrayRef.h>
#include <llvm/Support/JSON.h>
#include <llvm/Support/SHA256.h>
#include <llvm/Support/raw_ostream.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

constexpr std::string_view media_type =
    "application/vnd.janus.registry.v1+json";
constexpr std::uintmax_t maximum_archive_size = 128U * 1024U * 1024U;
constexpr std::uintmax_t maximum_file_size = 32U * 1024U * 1024U;
constexpr std::uintmax_t maximum_extracted_size = 256U * 1024U * 1024U;
constexpr std::size_t maximum_entries = 10000;
#ifdef _WIN32
constexpr std::string_view tar_force_local = " --force-local";
#else
constexpr std::string_view tar_force_local = "";
#endif

struct ArchiveEntry {
  std::string path;
  std::string sha256;
  std::uintmax_t size{};
};

struct SelectedRelease {
  std::string version;
  std::string metadata_url;
  std::string metadata_sha256;
};

struct ReleaseMetadata {
  std::string archive_url;
  std::string archive_sha256;
  std::string manifest_sha256;
  std::uintmax_t archive_size{};
};

std::string shell_quote_string(std::string_view value) {
#ifdef _WIN32
  std::string quoted{"\""};
  for (const char character : value) {
    if (character == '"')
      quoted += '\\';
    quoted += character;
  }
  return quoted + '"';
#else
  std::string quoted{"'"};
  for (const char character : value) {
    if (character == '\'')
      quoted += "'\\''";
    else
      quoted += character;
  }
  return quoted + '\'';
#endif
}

std::string shell_quote(const std::filesystem::path &path) {
  return shell_quote_string(path.generic_string());
}

int command_status(int status) {
  if (status == -1)
    return 1;
#ifdef _WIN32
  return status;
#else
  if (WIFEXITED(status))
    return WEXITSTATUS(status);
  return 1;
#endif
}

void run_command(const std::string &command, std::string_view description) {
  if (command_status(std::system(command.c_str())) != 0)
    throw std::runtime_error{std::string{description}};
}

std::string read_file(const std::filesystem::path &path,
                      std::uintmax_t maximum = 8U * 1024U * 1024U) {
  std::error_code error;
  const std::uintmax_t size = std::filesystem::file_size(path, error);
  if (error || size > maximum)
    throw std::runtime_error{"registry response is missing or too large"};
  std::ifstream input{path, std::ios::binary};
  if (!input)
    throw std::runtime_error{"cannot read registry response"};
  return {std::istreambuf_iterator<char>{input},
          std::istreambuf_iterator<char>{}};
}

void write_file(const std::filesystem::path &path, std::string_view contents) {
  std::ofstream output{path, std::ios::binary};
  if (!output)
    throw std::runtime_error{"cannot prepare registry request"};
  output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  if (!output)
    throw std::runtime_error{"cannot prepare registry request"};
}

std::string sha256_text(std::string_view contents) {
  llvm::SHA256 hash;
  hash.update(llvm::StringRef{contents.data(), contents.size()});
  std::ostringstream result;
  for (const std::uint8_t byte : hash.final())
    result << std::hex << std::setw(2) << std::setfill('0')
           << static_cast<unsigned>(byte);
  return result.str();
}

std::string sha256_file(const std::filesystem::path &path) {
  std::ifstream input{path, std::ios::binary};
  if (!input)
    throw std::runtime_error{"cannot read downloaded registry artifact"};
  llvm::SHA256 hash;
  std::array<char, 64 * 1024> buffer{};
  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto size = static_cast<std::size_t>(input.gcount());
    hash.update(llvm::ArrayRef<std::uint8_t>{
        reinterpret_cast<const std::uint8_t *>(buffer.data()), size});
  }
  std::ostringstream result;
  for (const std::uint8_t byte : hash.final())
    result << std::hex << std::setw(2) << std::setfill('0')
           << static_cast<unsigned>(byte);
  return result.str();
}

std::string json_quote(std::string_view value) {
  std::string result;
  llvm::raw_string_ostream output{result};
  output << llvm::json::Value(std::string{value});
  output.flush();
  return result;
}

llvm::json::Object parse_object(std::string_view contents,
                                std::string_view description) {
  llvm::Expected<llvm::json::Value> parsed =
      llvm::json::parse(llvm::StringRef{contents.data(), contents.size()});
  if (!parsed)
    throw std::runtime_error{"invalid " + std::string{description} + ": " +
                             llvm::toString(parsed.takeError())};
  llvm::json::Object *object = parsed->getAsObject();
  if (object == nullptr)
    throw std::runtime_error{"invalid " + std::string{description} +
                             ": expected an object"};
  return std::move(*object);
}

std::string required_string(const llvm::json::Object &object,
                            llvm::StringRef key, std::string_view description) {
  const std::optional<llvm::StringRef> value = object.getString(key);
  if (!value)
    throw std::runtime_error{"invalid " + std::string{description} +
                             ": missing " + key.str()};
  return value->str();
}

std::uintmax_t required_size(const llvm::json::Object &object,
                             llvm::StringRef key,
                             std::string_view description) {
  const std::optional<std::int64_t> value = object.getInteger(key);
  if (!value || *value < 0)
    throw std::runtime_error{"invalid " + std::string{description} +
                             ": missing " + key.str()};
  return static_cast<std::uintmax_t>(*value);
}

void require_protocol(const llvm::json::Object &object,
                      std::string_view description) {
  if (required_string(object, "protocolVersion", description) != "1")
    throw std::runtime_error{"unsupported registry protocol version"};
}

bool allow_test_http() {
  const char *value = std::getenv("JANUS_REGISTRY_ALLOW_HTTP");
  return value != nullptr && std::string_view{value} == "1";
}

std::string validate_registry(std::string registry) {
  if (registry.ends_with('/'))
    throw std::runtime_error{"registry URL must not end with '/'"};
  static const std::regex https{
      R"(^https://(?:[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?\.)*[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?(?:/(?!\.{1,2}(?:/|$))[A-Za-z0-9._~+-]+)*$)"};
  static const std::regex loopback{
      R"(^http://(?:127\.0\.0\.1|localhost):[1-9][0-9]*(?:/[A-Za-z0-9._~+-]+)*$)"};
  if (!std::regex_match(registry, https) &&
      !(allow_test_http() && std::regex_match(registry, loopback)))
    throw std::runtime_error{
        "registry must be a canonical HTTPS URL without credentials, query, "
        "fragment, explicit port, or traversal"};
  return registry;
}

void require_registry_url(std::string_view url, std::string_view registry,
                          std::string_view description) {
  if (!url.starts_with(std::string{registry} + "/"))
    throw std::runtime_error{std::string{description} +
                             " crosses the configured registry origin"};
  if (url.find("/../") != std::string_view::npos ||
      url.find("/./") != std::string_view::npos ||
      url.find_first_of("?#") != std::string_view::npos)
    throw std::runtime_error{"invalid " + std::string{description}};
}

std::string token() {
  const char *configured = std::getenv("JANUS_REGISTRY_TOKEN");
  if (configured == nullptr || std::string_view{configured}.empty())
    throw std::runtime_error{
        "remote publication requires JANUS_REGISTRY_TOKEN"};
  const std::string value{configured};
  if (value.find_first_of("\"\r\n") != std::string::npos)
    throw std::runtime_error{"invalid registry authentication token"};
  return value;
}

void curl_request(const std::string &url,
                  const std::filesystem::path &destination,
                  const std::optional<std::string> &authorization = {},
                  const std::optional<std::filesystem::path> &upload = {},
                  std::string_view upload_content_type = {}) {
  const std::filesystem::path config = destination.string() + ".curl-config";
  const std::filesystem::path status = destination.string() + ".http-status";
  try {
    std::ofstream output{config, std::ios::binary};
    if (!output)
      throw std::runtime_error{"cannot prepare registry request"};
    output << "silent\nshow-error\nfail-with-body\nmax-redirs = 0\n"
              "connect-timeout = 15\nmax-time = 60\n";
    output << (allow_test_http() ? "proto = \"=http,https\"\n"
                                 : "proto = \"=https\"\ntlsv1.2\n");
    output << "header = \"Accept: " << media_type << "\"\n";
    if (authorization)
      output << "header = \"Authorization: Bearer " << *authorization << "\"\n";
    if (upload) {
      output << "request = \"PUT\"\n";
      output << "header = \"If-None-Match: *\"\n";
      output << "header = \"Content-Type: " << upload_content_type << "\"\n";
    }
    output.close();
    std::error_code permissions_error;
    std::filesystem::permissions(config,
                                 std::filesystem::perms::owner_read |
                                     std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::replace,
                                 permissions_error);
    if (permissions_error)
      throw std::runtime_error{
          "cannot restrict registry authentication file permissions"};
    std::string command = "curl --config " + shell_quote(config) +
                          " --output " + shell_quote(destination) +
                          " --write-out " + shell_quote_string("%{http_code}") +
                          ' ';
    if (upload)
      command +=
          "--data-binary " + shell_quote_string("@" + upload->string()) + ' ';
    command += shell_quote_string(url) + " >" + shell_quote(status);
    const int result = command_status(std::system(command.c_str()));
    std::string http_status;
    {
      std::ifstream input{status};
      input >> http_status;
    }
    std::filesystem::remove(config);
    std::filesystem::remove(status);
    if (result != 0)
      throw std::runtime_error{"registry request failed" +
                               (http_status.empty()
                                    ? std::string{}
                                    : " (HTTP " + http_status + ")")};
  } catch (...) {
    std::error_code ignored;
    std::filesystem::remove(config, ignored);
    std::filesystem::remove(status, ignored);
    throw;
  }
}

std::string get_api_base(const std::string &registry,
                         const std::filesystem::path &scratch) {
  const auto discovery_path = scratch / "discovery.json";
  curl_request(registry + "/.well-known/janus-registry", discovery_path);
  const llvm::json::Object discovery =
      parse_object(read_file(discovery_path), "registry discovery");
  require_protocol(discovery, "registry discovery");
  const llvm::json::Array *versions = discovery.getArray("versions");
  if (versions == nullptr)
    throw std::runtime_error{"invalid registry discovery: missing versions"};
  for (const llvm::json::Value &candidate : *versions) {
    const llvm::json::Object *object = candidate.getAsObject();
    if (object == nullptr || object->getString("protocolVersion") !=
                                 std::optional<llvm::StringRef>{"1"})
      continue;
    const std::string api_base =
        required_string(*object, "apiBase", "registry discovery");
    require_registry_url(api_base, registry, "registry API base");
    return api_base;
  }
  throw std::runtime_error{"registry does not support protocol v1"};
}

std::string encode_query(std::string_view query) {
  std::ostringstream result;
  for (const unsigned char character : query) {
    if (std::isalnum(character) != 0 || character == '-' || character == '_' ||
        character == '.' || character == '~')
      result << static_cast<char>(character);
    else
      result << '%' << std::uppercase << std::hex << std::setw(2)
             << std::setfill('0') << static_cast<unsigned>(character);
  }
  return result.str();
}

std::pair<std::string, std::string> split_package(std::string_view package) {
  static const std::regex canonical{
      R"(^[a-z][a-z0-9_-]{0,63}/[a-z][a-z0-9_-]{0,63}$)"};
  if (!std::regex_match(package.begin(), package.end(), canonical))
    throw std::runtime_error{
        "remote package names must use lowercase namespace/name"};
  const std::size_t slash = package.find('/');
  return {std::string{package.substr(0, slash)},
          std::string{package.substr(slash + 1)}};
}

SelectedRelease select_release(const llvm::json::Object &index,
                               const janus::driver::Dependency &dependency) {
  require_protocol(index, "registry index");
  if (required_string(index, "package", "registry index") != dependency.name)
    throw std::runtime_error{
        "registry index returned a different package identity"};
  const llvm::json::Array *releases = index.getArray("releases");
  if (releases == nullptr)
    throw std::runtime_error{"invalid registry index: missing releases"};
  bool found = false;
  janus::driver::SemanticVersion best;
  SelectedRelease selected;
  for (const llvm::json::Value &value : *releases) {
    const llvm::json::Object *release = value.getAsObject();
    if (release == nullptr)
      throw std::runtime_error{"invalid registry release"};
    if (release->getBoolean("yanked").value_or(true))
      continue;
    const std::string version =
        required_string(*release, "version", "registry release");
    const janus::driver::SemanticVersion candidate =
        janus::driver::parse_semantic_version(version);
    if (!janus::driver::matches_version(dependency.version_requirement,
                                        candidate))
      continue;
    if (!found || janus::driver::compare(candidate, best) > 0) {
      found = true;
      best = candidate;
      selected = {
          version, required_string(*release, "metadataUrl", "registry release"),
          required_string(*release, "metadataSha256", "registry release")};
    }
  }
  if (!found)
    throw std::runtime_error{"no version of package '" + dependency.name +
                             "' satisfies '" + dependency.version_requirement +
                             "'"};
  return selected;
}

ReleaseMetadata parse_metadata(std::string_view contents,
                               const std::string &package,
                               const std::string &version,
                               const std::string &registry) {
  const llvm::json::Object metadata =
      parse_object(contents, "registry metadata");
  require_protocol(metadata, "registry metadata");
  if (required_string(metadata, "package", "registry metadata") != package ||
      required_string(metadata, "version", "registry metadata") != version)
    throw std::runtime_error{
        "registry metadata returned a different package identity"};
  const llvm::json::Object *archive = metadata.getObject("archive");
  if (archive == nullptr)
    throw std::runtime_error{"invalid registry metadata: missing archive"};
  ReleaseMetadata result{
      required_string(*archive, "url", "registry metadata archive"),
      required_string(*archive, "sha256", "registry metadata archive"),
      required_string(*archive, "manifestSha256", "registry metadata archive"),
      required_size(*archive, "size", "registry metadata archive")};
  require_registry_url(result.archive_url, registry, "archive URL");
  if (result.archive_size == 0 || result.archive_size > maximum_archive_size)
    throw std::runtime_error{"registry archive exceeds the client limit"};
  return result;
}

std::vector<ArchiveEntry> parse_archive_manifest(std::string_view contents,
                                                 const std::string &package,
                                                 const std::string &version) {
  const llvm::json::Object manifest =
      parse_object(contents, "archive manifest");
  require_protocol(manifest, "archive manifest");
  if (required_string(manifest, "package", "archive manifest") != package ||
      required_string(manifest, "version", "archive manifest") != version)
    throw std::runtime_error{
        "archive manifest returned a different package identity"};
  const llvm::json::Array *entries = manifest.getArray("entries");
  if (entries == nullptr || entries->empty() ||
      entries->size() > maximum_entries)
    throw std::runtime_error{"invalid archive manifest entry count"};
  static const std::regex safe_path{
      R"(^(?:(?:src|tests|examples|docs)/(?!\.{1,2}(?:/|$))[A-Za-z0-9._-]+(?:/(?!\.{1,2}(?:/|$))[A-Za-z0-9._-]+)*|janus\.toml|README\.md|LICENSE|NOTICE)$)"};
  std::set<std::string> seen;
  std::uintmax_t total = 0;
  std::vector<ArchiveEntry> result;
  result.reserve(entries->size());
  for (const llvm::json::Value &value : *entries) {
    const llvm::json::Object *entry = value.getAsObject();
    if (entry == nullptr)
      throw std::runtime_error{"invalid archive manifest entry"};
    ArchiveEntry parsed{
        required_string(*entry, "path", "archive manifest entry"),
        required_string(*entry, "sha256", "archive manifest entry"),
        required_size(*entry, "size", "archive manifest entry")};
    if (!std::regex_match(parsed.path, safe_path) ||
        !std::regex_match(parsed.sha256, std::regex{"[0-9a-f]{64}"}) ||
        parsed.size > maximum_file_size || !seen.insert(parsed.path).second)
      throw std::runtime_error{"unsafe archive manifest entry"};
    total += parsed.size;
    if (total > maximum_extracted_size)
      throw std::runtime_error{"archive exceeds the extracted size limit"};
    result.push_back(std::move(parsed));
  }
  return result;
}

std::vector<std::string> lines(const std::filesystem::path &path) {
  std::ifstream input{path};
  std::vector<std::string> result;
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    if (!line.empty())
      result.push_back(line);
  }
  return result;
}

void extract_verified_archive(const std::filesystem::path &archive,
                              const std::filesystem::path &destination,
                              const std::vector<ArchiveEntry> &entries) {
  const std::filesystem::path listing = archive.string() + ".listing";
  const std::filesystem::path verbose = archive.string() + ".verbose";
  run_command("tar" + std::string{tar_force_local} + " -tzf " +
                  shell_quote(archive) + " >" + shell_quote(listing),
              "registry archive is invalid");
  run_command("tar" + std::string{tar_force_local} + " -tvzf " +
                  shell_quote(archive) + " >" + shell_quote(verbose),
              "registry archive is invalid");
  std::vector<std::string> expected;
  expected.reserve(entries.size());
  for (const ArchiveEntry &entry : entries)
    expected.push_back(entry.path);
  std::vector<std::string> actual = lines(listing);
  std::sort(expected.begin(), expected.end());
  std::sort(actual.begin(), actual.end());
  if (actual != expected)
    throw std::runtime_error{
        "registry archive entries do not match the verified manifest"};
  const std::vector<std::string> verbose_lines = lines(verbose);
  if (verbose_lines.size() != entries.size() ||
      std::any_of(verbose_lines.begin(), verbose_lines.end(),
                  [](const std::string &line) {
                    return line.empty() || line.front() != '-';
                  }))
    throw std::runtime_error{"registry archive contains a non-regular entry"};
  std::filesystem::create_directories(destination);
  run_command("tar" + std::string{tar_force_local} + " -xzf " +
                  shell_quote(archive) + " -C " + shell_quote(destination),
              "could not extract registry archive");
  std::set<std::string> extracted;
  for (const auto &item :
       std::filesystem::recursive_directory_iterator(destination)) {
    if (item.is_directory())
      continue;
    if (!item.is_regular_file() || item.is_symlink())
      throw std::runtime_error{
          "registry archive extracted a non-regular entry"};
    const std::string relative =
        std::filesystem::relative(item.path(), destination).generic_string();
    extracted.insert(relative);
  }
  if (extracted != std::set<std::string>{expected.begin(), expected.end()})
    throw std::runtime_error{
        "registry archive extraction produced unexpected entries"};
  for (const ArchiveEntry &entry : entries) {
    const std::filesystem::path path = destination / entry.path;
    std::error_code error;
    if (std::filesystem::file_size(path, error) != entry.size || error ||
        sha256_file(path) != entry.sha256)
      throw std::runtime_error{
          "registry archive entry checksum mismatch for '" + entry.path + "'"};
  }
}

std::string cache_key(std::string_view registry) {
  return sha256_text(registry).substr(0, 32);
}

std::filesystem::path cache_destination(const std::filesystem::path &cache_root,
                                        std::string_view registry,
                                        std::string_view package,
                                        std::string_view version) {
  const auto [name_space, name] = split_package(package);
  return cache_root / "registry-v1" / cache_key(registry) / name_space / name /
         version;
}

std::string marker_contents(const janus::driver::RegistryLock &lock,
                            std::string_view package,
                            std::string_view manifest_sha256) {
  return "{\"archiveSha256\":" + json_quote(lock.archive_sha256) +
         ",\"manifestSha256\":" + json_quote(manifest_sha256) +
         ",\"metadataSha256\":" + json_quote(lock.metadata_sha256) +
         ",\"package\":" + json_quote(package) +
         ",\"protocolVersion\":\"1\",\"registry\":" +
         json_quote(lock.registry) +
         ",\"version\":" + json_quote(lock.version) + "}";
}

bool validate_cache(const std::filesystem::path &destination,
                    std::string_view package,
                    const janus::driver::RegistryLock &expected) {
  try {
    const std::string marker_text =
        read_file(destination / "verified.json", 64 * 1024);
    const llvm::json::Object marker =
        parse_object(marker_text, "registry cache marker");
    require_protocol(marker, "registry cache marker");
    if (required_string(marker, "package", "registry cache marker") !=
            package ||
        required_string(marker, "registry", "registry cache marker") !=
            expected.registry ||
        required_string(marker, "version", "registry cache marker") !=
            expected.version ||
        required_string(marker, "metadataSha256", "registry cache marker") !=
            expected.metadata_sha256 ||
        required_string(marker, "archiveSha256", "registry cache marker") !=
            expected.archive_sha256 ||
        !std::filesystem::is_directory(destination / "package") ||
        sha256_file(destination / "archive.tar.gz") != expected.archive_sha256)
      return false;
    const std::string manifest_sha =
        required_string(marker, "manifestSha256", "registry cache marker");
    if (sha256_file(destination / "archive-manifest.json") != manifest_sha)
      return false;
    const std::vector<ArchiveEntry> entries =
        parse_archive_manifest(read_file(destination / "archive-manifest.json"),
                               std::string{package}, expected.version);
    std::set<std::string> actual;
    for (const auto &item : std::filesystem::recursive_directory_iterator(
             destination / "package")) {
      if (item.is_directory())
        continue;
      if (!item.is_regular_file() || item.is_symlink())
        return false;
      actual.insert(
          std::filesystem::relative(item.path(), destination / "package")
              .generic_string());
    }
    std::set<std::string> listed;
    for (const ArchiveEntry &entry : entries) {
      listed.insert(entry.path);
      const auto path = destination / "package" / entry.path;
      std::error_code error;
      if (std::filesystem::file_size(path, error) != entry.size || error ||
          sha256_file(path) != entry.sha256)
        return false;
    }
    return actual == listed;
  } catch (const std::exception &) {
    return false;
  }
}

std::filesystem::path
generation_root(const std::filesystem::path &destination) {
  // Keep published generations beside the stable version anchor. Readers hold
  // generation paths directly, so a later publication never invalidates them.
  return destination.string() + ".generations";
}

std::vector<std::filesystem::path>
cache_generations(const std::filesystem::path &destination) {
  std::vector<std::filesystem::path> result;
  std::error_code error;
  if (std::filesystem::is_directory(destination, error))
    result.push_back(destination);
  error.clear();
  const auto root = generation_root(destination);
  if (!std::filesystem::is_directory(root, error))
    return result;
  for (std::filesystem::directory_iterator iterator{root, error}, end;
       !error && iterator != end; iterator.increment(error)) {
    std::error_code type_error;
    if (iterator->is_directory(type_error) && !type_error)
      result.push_back(iterator->path());
  }
  return result;
}

std::optional<std::filesystem::path>
find_valid_generation(const std::filesystem::path &destination,
                      std::string_view package,
                      const janus::driver::RegistryLock &expected) {
  for (const auto &candidate : cache_generations(destination))
    if (validate_cache(candidate, package, expected))
      return candidate;
  return std::nullopt;
}

class CacheMutationLock {
public:
  explicit CacheMutationLock(const std::filesystem::path &destination)
      : path_{destination.string() + ".lock"} {
#ifdef _WIN32
    for (int attempt = 0; attempt < 400; ++attempt) {
      handle_ = CreateFileW(path_.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (handle_ != INVALID_HANDLE_VALUE)
        return;
      const DWORD error = GetLastError();
      if (error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION)
        throw std::runtime_error{
            "cannot lock registry cache destination: " +
            std::system_category().message(static_cast<int>(error))};
      std::this_thread::sleep_for(std::chrono::milliseconds{25});
    }
#else
    descriptor_ = ::open(path_.c_str(), O_CREAT | O_RDWR, 0600);
    if (descriptor_ < 0)
      throw std::runtime_error{
          "cannot open registry cache lock: " +
          std::error_code{errno, std::generic_category()}.message()};
    for (int attempt = 0; attempt < 400; ++attempt) {
      if (::flock(descriptor_, LOCK_EX | LOCK_NB) == 0)
        return;
      if (errno != EWOULDBLOCK && errno != EAGAIN) {
        const std::string message =
            std::error_code{errno, std::generic_category()}.message();
        ::close(descriptor_);
        descriptor_ = -1;
        throw std::runtime_error{"cannot lock registry cache destination: " +
                                 message};
      }
      std::this_thread::sleep_for(std::chrono::milliseconds{25});
    }
#endif
    release();
    throw std::runtime_error{"timed out locking registry cache destination"};
  }

  CacheMutationLock(const CacheMutationLock &) = delete;
  CacheMutationLock &operator=(const CacheMutationLock &) = delete;

  ~CacheMutationLock() { release(); }

private:
  void release() noexcept {
#ifdef _WIN32
    if (handle_ != INVALID_HANDLE_VALUE) {
      CloseHandle(handle_);
      handle_ = INVALID_HANDLE_VALUE;
    }
#else
    if (descriptor_ >= 0) {
      static_cast<void>(::flock(descriptor_, LOCK_UN));
      static_cast<void>(::close(descriptor_));
      descriptor_ = -1;
    }
#endif
  }

  std::filesystem::path path_;
#ifdef _WIN32
  HANDLE handle_{INVALID_HANDLE_VALUE};
#else
  int descriptor_{-1};
#endif
};

std::filesystem::path unique_staging(const std::filesystem::path &destination) {
  static std::atomic<std::uint64_t> sequence{};
  for (int attempt = 0; attempt < 128; ++attempt) {
    const auto nonce =
        std::chrono::steady_clock::now().time_since_epoch().count() +
        static_cast<std::int64_t>(
            sequence.fetch_add(1, std::memory_order_relaxed));
    const std::filesystem::path candidate =
        destination.string() + ".new-" + std::to_string(nonce);
    std::error_code error;
    if (std::filesystem::create_directories(candidate, error))
      return candidate;
    if (error && error != std::errc::file_exists)
      throw std::runtime_error{"cannot create registry cache staging area"};
  }
  throw std::runtime_error{"cannot reserve registry cache staging area"};
}

std::string timestamp_utc() {
  const std::time_t now = std::time(nullptr);
  std::tm utc{};
#ifdef _WIN32
  gmtime_s(&utc, &now);
#else
  gmtime_r(&now, &utc);
#endif
  std::ostringstream result;
  result << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  return result.str();
}

std::vector<ArchiveEntry>
collect_package(const janus::driver::Manifest &manifest,
                const std::filesystem::path &staging) {
  static const std::regex safe_path{
      R"(^(?:(?:src|tests|examples|docs)/(?!\.{1,2}(?:/|$))[A-Za-z0-9._-]+(?:/(?!\.{1,2}(?:/|$))[A-Za-z0-9._-]+)*|janus\.toml|README\.md|LICENSE|NOTICE)$)"};
  std::vector<ArchiveEntry> entries;
  std::uintmax_t total = 0;
  for (const auto &item :
       std::filesystem::recursive_directory_iterator(manifest.root())) {
    const std::string relative =
        std::filesystem::relative(item.path(), manifest.root())
            .generic_string();
    if (relative == ".git" || relative == "target" ||
        relative.starts_with(".git/") || relative.starts_with("target/")) {
      if (item.is_directory())
        continue;
      continue;
    }
    if (item.is_directory())
      continue;
    if (!item.is_regular_file() || item.is_symlink())
      throw std::runtime_error{
          "cannot publish package containing non-regular files"};
    if (!std::regex_match(relative, safe_path))
      continue;
    const std::uintmax_t size = item.file_size();
    if (size > maximum_file_size || (total += size) > maximum_extracted_size ||
        entries.size() >= maximum_entries)
      throw std::runtime_error{"package exceeds registry archive limits"};
    const std::filesystem::path destination = staging / relative;
    std::filesystem::create_directories(destination.parent_path());
    std::filesystem::copy_file(item.path(), destination);
    std::error_code ignored;
    std::filesystem::last_write_time(
        destination, std::filesystem::file_time_type::clock::time_point{},
        ignored);
    entries.push_back({relative, sha256_file(item.path()), size});
  }
  std::sort(entries.begin(), entries.end(),
            [](const ArchiveEntry &left, const ArchiveEntry &right) {
              return left.path < right.path;
            });
  if (std::none_of(entries.begin(), entries.end(),
                   [](const ArchiveEntry &entry) {
                     return entry.path == "janus.toml";
                   }) ||
      std::none_of(entries.begin(), entries.end(),
                   [](const ArchiveEntry &entry) {
                     return entry.path.starts_with("src/");
                   }))
    throw std::runtime_error{
        "cannot publish a package without janus.toml and src/"};
  return entries;
}

std::string archive_manifest_json(const janus::driver::Manifest &manifest,
                                  const std::vector<ArchiveEntry> &entries) {
  std::string result{"{\"entries\":["};
  for (std::size_t index = 0; index < entries.size(); ++index) {
    if (index != 0)
      result += ',';
    const ArchiveEntry &entry = entries[index];
    result += "{\"path\":" + json_quote(entry.path) +
              ",\"sha256\":" + json_quote(entry.sha256) +
              ",\"size\":" + std::to_string(entry.size) + "}";
  }
  return result + "],\"package\":" + json_quote(manifest.name) +
         ",\"protocolVersion\":\"1\",\"version\":" +
         json_quote(manifest.version) + "}";
}

std::string metadata_json(const janus::driver::Manifest &manifest,
                          const std::string &api_base,
                          const std::string &archive_sha,
                          std::uintmax_t archive_size,
                          const std::string &manifest_sha) {
  std::string dependencies{"["};
  bool first = true;
  for (const janus::driver::Dependency &dependency : manifest.dependencies) {
    if (!dependency.is_registry())
      continue;
    if (dependency.registry.empty())
      throw std::runtime_error{
          "remote publication requires an explicit registry for dependency '" +
          dependency.name + "'"};
    if (!first)
      dependencies += ',';
    first = false;
    dependencies +=
        "{\"package\":" + json_quote(dependency.name) +
        ",\"registry\":" + json_quote(dependency.registry) +
        ",\"requirement\":" + json_quote(dependency.version_requirement) + "}";
  }
  dependencies += ']';
  const std::string archive_url = api_base + "/packages/" + manifest.name +
                                  "/" + manifest.version + "/archive.tar.gz";
  return "{\"archive\":{\"manifestSha256\":" + json_quote(manifest_sha) +
         ",\"sha256\":" + json_quote(archive_sha) +
         ",\"size\":" + std::to_string(archive_size) +
         ",\"url\":" + json_quote(archive_url) +
         "},\"dependencies\":" + dependencies +
         ",\"package\":" + json_quote(manifest.name) +
         ",\"protocolVersion\":\"1\",\"publishedAt\":" +
         json_quote(timestamp_utc()) +
         ",\"version\":" + json_quote(manifest.version) + "}";
}

} // namespace

namespace janus::driver {

std::vector<RegistrySearchResult>
search_registry(std::string_view query, const std::string &configured) {
  if (query.empty())
    throw std::runtime_error{"search requires a non-empty query"};
  const std::string registry =
      validate_registry(configured.empty() ? registry_location() : configured);
  TemporaryDirectory scratch = TemporaryDirectory::create("janus-registry");
  const std::string api_base = get_api_base(registry, scratch.path());
  const auto response_path = scratch.path() / "search.json";
  curl_request(api_base + "/search?q=" + encode_query(query), response_path);
  const llvm::json::Object response =
      parse_object(read_file(response_path), "registry search response");
  require_protocol(response, "registry search response");
  const llvm::json::Array *packages = response.getArray("packages");
  if (packages == nullptr)
    throw std::runtime_error{
        "invalid registry search response: missing packages"};
  std::vector<RegistrySearchResult> results;
  for (const llvm::json::Value &value : *packages) {
    const llvm::json::Object *package = value.getAsObject();
    if (package == nullptr)
      throw std::runtime_error{"invalid registry search result"};
    RegistrySearchResult result{
        required_string(*package, "package", "registry search result"),
        required_string(*package, "latestVersion", "registry search result"),
        package->getString("description").value_or("").str()};
    static_cast<void>(split_package(result.package));
    static_cast<void>(parse_semantic_version(result.latest_version));
    results.push_back(std::move(result));
  }
  return results;
}

ResolvedRegistryPackage
resolve_remote_package(const Dependency &dependency,
                       const std::optional<RegistryLock> &locked, bool offline,
                       const std::filesystem::path &cache_root) {
  static_cast<void>(split_package(dependency.name));
  const std::string registry = validate_registry(
      dependency.registry.empty() ? registry_location() : dependency.registry);
  if (locked && locked->registry != registry)
    throw std::runtime_error{"locked registry for package '" + dependency.name +
                             "' differs from janus.toml"};
  if (locked && !matches_version(dependency.version_requirement,
                                 parse_semantic_version(locked->version)))
    throw std::runtime_error{"locked version " + locked->version +
                             " of package '" + dependency.name +
                             "' no longer satisfies '" +
                             dependency.version_requirement + "'"};

  if (offline) {
    if (locked) {
      const auto destination = cache_destination(
          cache_root, registry, dependency.name, locked->version);
      const auto generation =
          find_valid_generation(destination, dependency.name, *locked);
      if (!generation)
        throw std::runtime_error{"registry package '" + dependency.name +
                                 "' is not cached and verified; --offline was "
                                 "requested"};
      return {*generation / "package", *locked};
    }
    const auto package_root =
        cache_destination(cache_root, registry, dependency.name, "placeholder")
            .parent_path();
    bool found = false;
    SemanticVersion best;
    RegistryLock best_lock;
    std::filesystem::path best_path;
    std::error_code error;
    if (std::filesystem::is_directory(package_root, error)) {
      for (const auto &entry :
           std::filesystem::directory_iterator(package_root)) {
        if (!entry.is_directory())
          continue;
        try {
          const SemanticVersion version =
              parse_semantic_version(entry.path().filename().string());
          if (!matches_version(dependency.version_requirement, version))
            continue;
          for (const auto &generation : cache_generations(entry.path())) {
            try {
              const llvm::json::Object marker = parse_object(
                  read_file(generation / "verified.json", 64 * 1024),
                  "registry cache marker");
              RegistryLock candidate{
                  required_string(marker, "registry", "registry cache marker"),
                  required_string(marker, "version", "registry cache marker"),
                  required_string(marker, "metadataSha256",
                                  "registry cache marker"),
                  required_string(marker, "archiveSha256",
                                  "registry cache marker")};
              if (validate_cache(generation, dependency.name, candidate) &&
                  (!found || compare(version, best) > 0)) {
                found = true;
                best = version;
                best_lock = std::move(candidate);
                best_path = generation;
              }
            } catch (const std::exception &) {
            }
          }
        } catch (const std::exception &) {
        }
      }
    }
    if (!found)
      throw std::runtime_error{"registry package '" + dependency.name +
                               "' is not cached and verified; --offline was "
                               "requested"};
    return {best_path / "package", best_lock};
  }

  TemporaryDirectory scratch = TemporaryDirectory::create("janus-registry");
  const std::string api_base = get_api_base(registry, scratch.path());
  SelectedRelease selected;
  if (locked) {
    selected.version = locked->version;
    selected.metadata_url = api_base + "/packages/" + dependency.name + "/" +
                            locked->version + "/metadata";
    selected.metadata_sha256 = locked->metadata_sha256;
  } else {
    const auto index_path = scratch.path() / "index.json";
    curl_request(api_base + "/packages/" + dependency.name, index_path);
    selected = select_release(
        parse_object(read_file(index_path), "registry index"), dependency);
  }
  const std::string expected_metadata_url = api_base + "/packages/" +
                                            dependency.name + "/" +
                                            selected.version + "/metadata";
  if (selected.metadata_url != expected_metadata_url)
    throw std::runtime_error{
        "registry metadata URL does not match the requested package"};
  require_registry_url(selected.metadata_url, registry, "metadata URL");
  const auto metadata_path = scratch.path() / "metadata.json";
  curl_request(selected.metadata_url, metadata_path);
  const std::string metadata_contents = read_file(metadata_path);
  if (sha256_text(metadata_contents) != selected.metadata_sha256)
    throw std::runtime_error{"registry metadata checksum mismatch"};
  const ReleaseMetadata metadata = parse_metadata(
      metadata_contents, dependency.name, selected.version, registry);
  const std::string expected_archive_url = api_base + "/packages/" +
                                           dependency.name + "/" +
                                           selected.version + "/archive.tar.gz";
  if (metadata.archive_url != expected_archive_url)
    throw std::runtime_error{
        "registry archive URL does not match the requested package"};
  if (locked && metadata.archive_sha256 != locked->archive_sha256)
    throw std::runtime_error{"registry archive checksum differs from lockfile"};
  RegistryLock resolution{registry, selected.version, selected.metadata_sha256,
                          metadata.archive_sha256};
  const auto destination = cache_destination(cache_root, registry,
                                             dependency.name, selected.version);
  if (const auto generation =
          find_valid_generation(destination, dependency.name, resolution))
    return {*generation / "package", resolution};
  std::error_code ignored;
  std::filesystem::create_directories(destination.parent_path());
  const std::filesystem::path staging = unique_staging(destination);
  try {
    const std::string manifest_url =
        selected.metadata_url.substr(0, selected.metadata_url.size() -
                                            std::string{"/metadata"}.size()) +
        "/archive-manifest";
    require_registry_url(manifest_url, registry, "archive manifest URL");
    const auto manifest_path = staging / "archive-manifest.json";
    curl_request(manifest_url, manifest_path);
    const std::string manifest_contents = read_file(manifest_path);
    if (sha256_text(manifest_contents) != metadata.manifest_sha256)
      throw std::runtime_error{"archive manifest checksum mismatch"};
    const std::vector<ArchiveEntry> entries = parse_archive_manifest(
        manifest_contents, dependency.name, selected.version);
    const auto archive_path = staging / "archive.tar.gz";
    try {
      curl_request(metadata.archive_url, archive_path);
    } catch (const std::runtime_error &) {
      throw std::runtime_error{"registry archive download failed"};
    }
    std::error_code size_error;
    const std::uintmax_t downloaded_size =
        std::filesystem::file_size(archive_path, size_error);
    if (size_error || downloaded_size != metadata.archive_size)
      throw std::runtime_error{"registry archive download is incomplete"};
    if (sha256_file(archive_path) != metadata.archive_sha256)
      throw std::runtime_error{"registry archive checksum mismatch"};
    extract_verified_archive(archive_path, staging / "package", entries);
    write_file(
        staging / "verified.json",
        marker_contents(resolution, dependency.name, metadata.manifest_sha256));
    {
      CacheMutationLock mutation_lock{destination};
      if (const auto generation =
              find_valid_generation(destination, dependency.name, resolution)) {
        std::filesystem::remove_all(staging, ignored);
        return {*generation / "package", resolution};
      }
      const auto root = generation_root(destination);
      // The empty version directory keeps cache discovery compatible with the
      // legacy layout; only complete staging directories enter the generation
      // root through the atomic rename below.
      std::filesystem::create_directories(destination);
      std::filesystem::create_directories(root);
      const std::string staging_name = staging.filename().string();
      const std::size_t nonce_offset = staging_name.rfind(".new-");
      const auto generation =
          root / ("generation-" +
                  staging_name.substr(nonce_offset == std::string::npos
                                          ? 0
                                          : nonce_offset + 5));
      std::error_code publish_error;
      std::filesystem::rename(staging, generation, publish_error);
      if (publish_error)
        throw std::runtime_error{"cannot publish verified registry cache: " +
                                 publish_error.message()};
      return {generation / "package", resolution};
    }
  } catch (...) {
    std::filesystem::remove_all(staging, ignored);
    throw;
  }
  throw std::runtime_error{"cannot publish verified registry cache"};
}

void publish_remote_package(const Manifest &manifest,
                            const std::string &configured) {
  static_cast<void>(split_package(manifest.name));
  const std::string registry = validate_registry(configured);
  TemporaryDirectory scratch = TemporaryDirectory::create("janus-publish");
  const std::string api_base = get_api_base(registry, scratch.path());
  const auto package_staging = scratch.path() / "package";
  std::filesystem::create_directories(package_staging);
  const std::vector<ArchiveEntry> entries =
      collect_package(manifest, package_staging);
  const std::string manifest_contents =
      archive_manifest_json(manifest, entries);
  const auto manifest_path = scratch.path() / "archive-manifest.json";
  write_file(manifest_path, manifest_contents);
  const auto file_list = scratch.path() / "files.txt";
  {
    std::ofstream output{file_list, std::ios::binary};
    for (const ArchiveEntry &entry : entries)
      output << entry.path << '\n';
  }
  const auto archive_path = scratch.path() / "archive.tar.gz";
  run_command("tar" + std::string{tar_force_local} + " -czf " +
                  shell_quote(archive_path) + " -C " +
                  shell_quote(package_staging) + " -T " + shell_quote(file_list),
              "could not create the registry archive");
  const std::uintmax_t archive_size = std::filesystem::file_size(archive_path);
  if (archive_size == 0 || archive_size > maximum_archive_size)
    throw std::runtime_error{"registry archive exceeds the client limit"};
  const std::string metadata_contents =
      metadata_json(manifest, api_base, sha256_file(archive_path), archive_size,
                    sha256_text(manifest_contents));
  const auto metadata_path = scratch.path() / "metadata.json";
  write_file(metadata_path, metadata_contents);
  const std::string boundary =
      "janus-" +
      sha256_text(manifest.name + "@" + manifest.version).substr(0, 32);
  std::string body;
  const auto append_part = [&body, &boundary](std::string_view content_id,
                                              std::string_view content_type,
                                              std::string_view payload) {
    body += "--" + boundary + "\r\nContent-ID: " + std::string{content_id} +
            "\r\nContent-Type: " + std::string{content_type} + "\r\n\r\n";
    body.append(payload);
    body += "\r\n";
  };
  append_part("metadata", media_type, metadata_contents);
  append_part("archive-manifest", media_type, manifest_contents);
  append_part("archive", "application/gzip",
              read_file(archive_path, maximum_archive_size));
  body += "--" + boundary + "--\r\n";
  const auto body_path = scratch.path() / "publish.multipart";
  write_file(body_path, body);
  const auto response_path = scratch.path() / "publish-response";
  curl_request(api_base + "/packages/" + manifest.name + "/" + manifest.version,
               response_path, token(), body_path,
               "multipart/related; boundary=" + boundary);
}

} // namespace janus::driver
