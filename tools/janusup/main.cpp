#include "janus/build_identity.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <llvm/ADT/ArrayRef.h>
#include <llvm/Support/SHA256.h>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

std::filesystem::path home() {
  if (const char *configured = std::getenv("JANUSUP_HOME"))
    return configured;
#ifdef _WIN32
  if (const char *local = std::getenv("LOCALAPPDATA"))
    return std::filesystem::path{local} / "Janus";
#else
  if (const char *user = std::getenv("HOME"))
    return std::filesystem::path{user} / ".janus";
#endif
  throw std::runtime_error{"cannot determine Janus home directory"};
}

std::filesystem::path toolchains() { return home() / "toolchains"; }
std::filesystem::path default_file() { return home() / "default"; }

std::string active_toolchain() {
  std::ifstream input{default_file()};
  std::string name;
  std::getline(input, name);
  return name;
}

void validate_name(const std::string &name) {
  if (name.empty() || name == "." || name == ".." ||
      name.find_first_of("/\\") != std::string::npos)
    throw std::runtime_error{"invalid toolchain name '" + name + "'"};
}

std::string shell_quote(const std::filesystem::path &path) {
  const std::string value = path.string();
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

std::string platform() {
#ifdef _WIN32
  return "Windows";
#elif defined(__APPLE__)
  return "Darwin";
#elif defined(__linux__)
  return "Linux";
#else
#error "unsupported Janus host"
#endif
}

std::string architecture() {
#if defined(__aarch64__) || defined(_M_ARM64)
  return "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
#ifdef _WIN32
  return "AMD64";
#else
  return "x86_64";
#endif
#else
#error "unsupported Janus architecture"
#endif
}

std::string package_basename(const std::string &version) {
  return "janus-" + version + "-" + platform() + "-" + architecture();
}

std::string archive_name(const std::string &version) {
  return package_basename(version) +
#ifdef _WIN32
         ".zip";
#else
         ".tar.gz";
#endif
}

std::string sha256(const std::filesystem::path &path) {
  std::ifstream input{path, std::ios::binary};
  if (!input)
    throw std::runtime_error{"cannot read '" + path.string() + "'"};
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

std::string expected_sha256(const std::filesystem::path &path) {
  std::ifstream input{path};
  std::string digest;
  input >> digest;
  if (digest.size() != 64)
    throw std::runtime_error{"invalid SHA-256 file"};
  for (char &character : digest)
    character =
        static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  return digest;
}

bool github_cli_available() {
#ifdef _WIN32
  constexpr const char *redirect = " >NUL 2>&1";
#else
  constexpr const char *redirect = " >/dev/null 2>&1";
#endif
  return command_status(std::system(
             (std::string{"gh attestation --help"} + redirect).c_str())) == 0;
}

void verify_attestation(const std::filesystem::path &archive) {
  if (!github_cli_available()) {
    throw std::runtime_error{
        "GitHub CLI with attestation support is required to verify artifact "
        "provenance"};
  }
  const std::string command = "gh attestation verify " + shell_quote(archive) +
                              " --repo cyril103/janus";
  if (command_status(std::system(command.c_str())) != 0)
    throw std::runtime_error{"provenance verification failed for '" +
                             archive.filename().string() + "'"};
}

std::filesystem::path temporary_directory() {
  const auto stamp =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path path = std::filesystem::temp_directory_path() /
                                     ("janusup-" + std::to_string(stamp));
  std::filesystem::create_directories(path);
  return path;
}

std::uint64_t archive_limit(const char *test_name,
                            std::uint64_t production) {
  const char *configured = std::getenv(test_name);
  if (configured == nullptr)
    return production;
  std::uint64_t candidate = 0;
  const std::string_view text{configured};
  const auto parsed =
      std::from_chars(text.data(), text.data() + text.size(), candidate);
  return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() &&
                 candidate < production
             ? candidate
             : production;
}

std::filesystem::path archive_tar() {
#ifdef _WIN32
  if (const char *system_root = std::getenv("SystemRoot")) {
    const std::filesystem::path system_tar =
        std::filesystem::path{system_root} / "System32/tar.exe";
    if (std::filesystem::is_regular_file(system_tar))
      return system_tar;
  }
#endif
  return "tar";
}

std::vector<std::string> read_archive_listing(
    const std::filesystem::path &archive, const std::filesystem::path &tar,
    const std::filesystem::path &output, bool verbose) {
#ifdef _WIN32
  // cmd.exe strips the first and last quote when a command starts with a
  // quoted executable. Prefix the invocation with cd, as the extraction path
  // already does, so an absolute System32 tar.exe remains executable.
  const std::string command =
      "cd /d " + shell_quote(output.parent_path()) + " && " +
      shell_quote(tar) + (verbose ? " -tvf " : " -tf ") +
      shell_quote(archive) + " > " + shell_quote(output.filename());
#else
  const std::string command =
      shell_quote(tar) + (verbose ? " --numeric-owner -tvf " : " -tf ") +
      shell_quote(archive) + " > " + shell_quote(output);
#endif
  if (command_status(std::system(command.c_str())) != 0)
    throw std::runtime_error{"could not inspect archive"};
  std::ifstream input{output};
  std::vector<std::string> lines;
  for (std::string line; std::getline(input, line);)
    lines.push_back(std::move(line));
  return lines;
}

std::string portable_archive_path(std::string_view name,
                                  std::string_view expected_root) {
  if (name.empty() || name.front() == '/' || name.find('\\') != name.npos ||
      (name.size() >= 2 && std::isalpha(static_cast<unsigned char>(name[0])) &&
       name[1] == ':'))
    throw std::runtime_error{"unsafe archive: absolute or ambiguous entry path"};
  std::string normalized;
  std::string first;
  std::size_t begin = 0;
  while (begin < name.size()) {
    const std::size_t end = name.find('/', begin);
    const std::string_view part = name.substr(begin, end - begin);
    if (part.empty() || part == "." || part == "..")
      throw std::runtime_error{"unsafe archive: traversing entry path"};
    if (part.back() == '.' ||
        part.find_first_of("<>:\"|?*") != std::string_view::npos)
      throw std::runtime_error{"unsafe archive: Windows-ambiguous entry path"};
    std::string portable_component{part};
    std::transform(portable_component.begin(), portable_component.end(),
                   portable_component.begin(), [](const unsigned char byte) {
                     return static_cast<char>(std::tolower(byte));
                   });
    const std::string_view stem = portable_component.substr(
        0, portable_component.find('.'));
    const bool reserved_device =
        stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" ||
        (stem.size() == 4 &&
         (stem.starts_with("com") || stem.starts_with("lpt")) &&
         stem.back() >= '1' && stem.back() <= '9');
    if (reserved_device)
      throw std::runtime_error{"unsafe archive: reserved Windows entry path"};
    if (first.empty())
      first = part;
    if (!normalized.empty())
      normalized += '/';
    normalized += part;
    if (end == name.npos)
      break;
    begin = end + 1;
    if (begin == name.size())
      break;
  }
  if (first != expected_root)
    throw std::runtime_error{"unsafe archive: unexpected archive root"};
  for (char &character : normalized) {
    const unsigned char byte = static_cast<unsigned char>(character);
    if (byte <= 0x20 || byte >= 0x7f)
      throw std::runtime_error{"unsafe archive: ambiguous entry name"};
    character = static_cast<char>(std::tolower(byte));
  }
  return normalized;
}

void validate_archive(const std::filesystem::path &archive,
                      std::string_view expected_root) {
  constexpr std::uint64_t production_entries = 100000;
  constexpr std::uint64_t production_file_size = 1024ULL * 1024 * 1024;
  constexpr std::uint64_t production_total_size = 4ULL * 1024 * 1024 * 1024;
  const std::uint64_t max_entries =
      archive_limit("JANUS_ARCHIVE_TEST_MAX_ENTRIES", production_entries);
  const std::uint64_t max_file_size = archive_limit(
      "JANUS_ARCHIVE_TEST_MAX_FILE_SIZE", production_file_size);
  const std::uint64_t max_total_size = archive_limit(
      "JANUS_ARCHIVE_TEST_MAX_TOTAL_SIZE", production_total_size);
  const auto scratch = temporary_directory();
  try {
    const auto tar = archive_tar();
    const auto names = read_archive_listing(archive, tar, scratch / "names", false);
    const auto verbose =
        read_archive_listing(archive, tar, scratch / "verbose", true);
    if (names.empty() || names.size() != verbose.size())
      throw std::runtime_error{"unsafe archive: inconsistent archive listing"};
    if (names.size() > max_entries)
      throw std::runtime_error{"unsafe archive: too many entries"};
    std::unordered_set<std::string> paths;
    std::unordered_set<std::string> regular_paths;
    std::unordered_set<std::string> required_directories;
    std::uint64_t total = 0;
    for (std::size_t index = 0; index < names.size(); ++index) {
      const std::string path =
          portable_archive_path(names[index], expected_root);
      if (!paths.insert(path).second)
        throw std::runtime_error{"unsafe archive: colliding entry paths"};
      std::istringstream fields{verbose[index]};
      std::vector<std::string> tokens;
      for (std::string token; fields >> token;)
        tokens.push_back(std::move(token));
      if (tokens.empty() || (tokens[0][0] != '-' && tokens[0][0] != 'd'))
        throw std::runtime_error{"unsafe archive: link or special entry"};
      for (std::size_t separator = path.find('/'); separator != path.npos;
           separator = path.find('/', separator + 1)) {
        const std::string ancestor = path.substr(0, separator);
        if (regular_paths.contains(ancestor))
          throw std::runtime_error{
              "unsafe archive: file/directory path collision"};
        required_directories.insert(ancestor);
      }
      if (tokens[0][0] == 'd')
        continue;
      if (required_directories.contains(path))
        throw std::runtime_error{
            "unsafe archive: file/directory path collision"};
      regular_paths.insert(path);
      const std::size_t size_index =
          tokens.size() > 2 && tokens[1].find('/') != std::string::npos ? 2 : 4;
      if (tokens.size() <= size_index)
        throw std::runtime_error{"unsafe archive: unparseable entry size"};
      std::uint64_t size = 0;
      const auto parsed = std::from_chars(tokens[size_index].data(),
                                          tokens[size_index].data() +
                                              tokens[size_index].size(),
                                          size);
      if (parsed.ec != std::errc{} ||
          parsed.ptr != tokens[size_index].data() + tokens[size_index].size())
        throw std::runtime_error{"unsafe archive: unparseable entry size"};
      if (size > max_file_size)
        throw std::runtime_error{"unsafe archive: entry is too large"};
      if (size > max_total_size - total)
        throw std::runtime_error{"unsafe archive: total size limit exceeded"};
      total += size;
    }
    std::filesystem::remove_all(scratch);
  } catch (...) {
    std::error_code cleanup_error;
    std::filesystem::remove_all(scratch, cleanup_error);
    if (cleanup_error)
      std::cerr << "janusup: WARNING: cleanup after archive validation failed: "
                << cleanup_error.message() << '\n';
    throw;
  }
}

void fetch(const std::string &location,
           const std::filesystem::path &destination) {
  if (std::filesystem::is_regular_file(location)) {
    std::filesystem::copy_file(location, destination);
    return;
  }
  const std::string command =
      "curl --fail --location --proto '=https' --tlsv1.2 " +
      shell_quote(location) + " -o " + shell_quote(destination);
  if (command_status(std::system(command.c_str())) != 0)
    throw std::runtime_error{"could not download '" + location + "'"};
}

std::string distribution_location(const std::string &version,
                                  const std::string &filename) {
  const char *configured = std::getenv("JANUS_DIST_SERVER");
  const std::string server =
      configured == nullptr
          ? "https://github.com/cyril103/janus/releases/download"
          : configured;
  if (std::filesystem::is_directory(server))
    return (std::filesystem::path{server} / version / filename).string();
  return server + (server.ends_with('/') ? "" : "/") + version + "/" +
         filename;
}

std::string channel_manifest_location(const std::string &name) {
  const char *configured = std::getenv("JANUS_DIST_SERVER");
  if (configured == nullptr && name == "nightly")
    return "https://raw.githubusercontent.com/cyril103/janus/"
           "nightly-channel/version";
  return distribution_location("channel-" + name, "version");
}

bool ascii_iequals(std::string_view left, std::string_view right) {
  return left.size() == right.size() &&
         std::equal(left.begin(), left.end(), right.begin(),
                    [](char a, char b) {
                      return std::tolower(static_cast<unsigned char>(a)) ==
                             std::tolower(static_cast<unsigned char>(b));
                    });
}

bool ascii_istarts_with(std::string_view value, std::string_view prefix) {
  return value.size() >= prefix.size() &&
         ascii_iequals(value.substr(0, prefix.size()), prefix);
}

bool official_distribution(std::string_view url) {
  const auto scheme_end = url.find("://");
  if (scheme_end == std::string_view::npos ||
      !ascii_iequals(url.substr(0, scheme_end), "https"))
    return false;
  const auto authority_begin = scheme_end + 3;
  const auto path_begin = url.find_first_of("/?#", authority_begin);
  if (path_begin == std::string_view::npos || url[path_begin] != '/')
    return false;
  auto authority = url.substr(authority_begin, path_begin - authority_begin);
  if (const auto userinfo = authority.rfind('@');
      userinfo != std::string_view::npos)
    authority.remove_prefix(userinfo + 1);
  std::string_view host = authority;
  std::string_view port;
  if (const auto colon = authority.rfind(':'); colon != std::string_view::npos) {
    host = authority.substr(0, colon);
    port = authority.substr(colon + 1);
  }
  if (!ascii_iequals(host, "github.com") ||
      (!port.empty() && port != "443") ||
      (authority.ends_with(':') && port.empty()))
    return false;
  const auto path_end = url.find_first_of("?#", path_begin);
  const auto path = url.substr(path_begin, path_end - path_begin);
  return ascii_istarts_with(path,
                            "/cyril103/janus/releases/download/");
}

bool unverified_private_mirror_allowed() {
  const char *configured =
      std::getenv("JANUS_ALLOW_UNVERIFIED_PRIVATE_MIRROR");
  return configured != nullptr && std::string_view{configured} == "1";
}

struct ToolchainSpec {
  std::string name;
  std::string version;
  std::string release;
  std::string source_sha;
  std::string published_at;
};

bool full_source_sha(std::string_view value) {
  return value.size() == 40 &&
         std::ranges::all_of(value, [](unsigned char character) {
           return std::isdigit(character) ||
                  (character >= 'a' && character <= 'f');
         });
}

bool utc_publication_time(std::string_view value) {
  if (value.size() != 20 || value[4] != '-' || value[7] != '-' ||
      value[10] != 'T' || value[13] != ':' || value[16] != ':' ||
      value[19] != 'Z')
    return false;
  for (const std::size_t index : {0U, 1U, 2U, 3U, 5U, 6U, 8U, 9U, 11U,
                                  12U, 14U, 15U, 17U, 18U})
    if (!std::isdigit(static_cast<unsigned char>(value[index])))
      return false;
  return true;
}

bool is_channel(const std::string &name) {
  return name == "stable" || name == "beta" || name == "nightly";
}

ToolchainSpec resolve_spec(const std::string &name,
                           const std::filesystem::path &temporary) {
  validate_name(name);
  if (!is_channel(name))
    return {name, name, "v" + name, {}, {}};

  const std::filesystem::path manifest = temporary / (name + ".channel");
  fetch(channel_manifest_location(name), manifest);
  std::ifstream input{manifest};
  ToolchainSpec result{name, {}, {}, {}, {}};
  input >> result.version >> result.release;
  validate_name(result.version);
  validate_name(result.release);
  if (!input)
    throw std::runtime_error{"invalid '" + name + "' channel manifest"};
  input >> std::ws;
  if (!input.eof()) {
    input >> result.source_sha >> result.published_at >> std::ws;
    if (!input.eof() || !full_source_sha(result.source_sha) ||
        !utc_publication_time(result.published_at))
      throw std::runtime_error{"invalid '" + name + "' channel manifest"};
  }
  return result;
}

std::filesystem::path download_package(const ToolchainSpec &spec,
                                       const std::filesystem::path &temporary) {
  const std::string archive = archive_name(spec.version);
  const std::filesystem::path archive_path = temporary / archive;
  const std::filesystem::path checksum_path = temporary / (archive + ".sha256");
  const std::string archive_location =
      distribution_location(spec.release, archive);
  fetch(archive_location, archive_path);
  fetch(distribution_location(spec.release, archive + ".sha256"),
        checksum_path);
  if (sha256(archive_path) != expected_sha256(checksum_path))
    throw std::runtime_error{"SHA-256 verification failed for " + archive};
  if (!official_distribution(archive_location) &&
      unverified_private_mirror_allowed()) {
    std::cerr << "janusup: WARNING: using an unverified private mirror "
                 "(JANUS_ALLOW_UNVERIFIED_PRIVATE_MIRROR=1)\n";
  } else {
    verify_attestation(archive_path);
  }

  const std::filesystem::path extracted = temporary / "package";
  std::filesystem::create_directory(extracted);
  validate_archive(archive_path, package_basename(spec.version));
#ifdef _WIN32
  const std::filesystem::path tar = archive_tar();
  const std::string command =
      "cd /d " + shell_quote(temporary) + " && " + shell_quote(tar) +
      " -xf " + shell_quote(archive_path.filename()) + " -C package";
#else
  const std::string command =
      "tar -xf " + shell_quote(archive_path) + " -C " + shell_quote(extracted);
#endif
  if (command_status(std::system(command.c_str())) != 0)
    throw std::runtime_error{"could not extract " + archive};
  const std::filesystem::path package =
      extracted / package_basename(spec.version);
  if (!std::filesystem::is_directory(package))
    throw std::runtime_error{"archive has an invalid directory layout"};
  return package;
}

std::filesystem::path state_root() { return home() / ".janusup-state"; }
std::filesystem::path locks_root() { return state_root() / "locks"; }
std::filesystem::path transactions_root() {
  return state_root() / "transactions";
}
std::filesystem::path journals_root() { return state_root() / "journals"; }
std::filesystem::path quarantine_root() { return state_root() / "quarantine"; }

std::string encode_name(std::string_view name) {
  static constexpr char digits[] = "0123456789abcdef";
  std::string result;
  result.reserve(name.size() * 2);
  for (const unsigned char byte : name) {
    result += digits[byte >> 4];
    result += digits[byte & 15];
  }
  return result;
}

void ensure_state_directories() {
  std::filesystem::create_directories(locks_root());
  std::filesystem::create_directories(transactions_root());
  std::filesystem::create_directories(journals_root());
  std::filesystem::create_directories(quarantine_root());
}

class ProcessLock {
public:
  ProcessLock(std::filesystem::path path, std::string description)
      : path_{std::move(path)}, description_{std::move(description)} {
#ifdef _WIN32
    for (;;) {
      handle_ = CreateFileW(path_.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                            nullptr);
      if (handle_ != INVALID_HANDLE_VALUE)
        break;
      if (GetLastError() != ERROR_SHARING_VIOLATION &&
          GetLastError() != ERROR_LOCK_VIOLATION)
        break;
      Sleep(10);
    }
    if (handle_ == INVALID_HANDLE_VALUE)
      throw std::runtime_error{"cannot lock " + description_};
#else
    descriptor_ = ::open(path_.c_str(), O_CREAT | O_RDWR, 0600);
    if (descriptor_ == -1 || ::flock(descriptor_, LOCK_EX) == -1) {
      const int saved = errno;
      if (descriptor_ != -1)
        ::close(descriptor_);
      throw std::runtime_error{"cannot lock " + description_ + ": " +
                               std::generic_category().message(saved)};
    }
#endif
  }

  ProcessLock(const ProcessLock &) = delete;
  ProcessLock &operator=(const ProcessLock &) = delete;

  ~ProcessLock() {
#ifdef _WIN32
    if (handle_ != INVALID_HANDLE_VALUE)
      CloseHandle(handle_);
#else
    if (descriptor_ != -1)
      ::close(descriptor_);
#endif
  }

private:
  std::filesystem::path path_;
  std::string description_;
#ifdef _WIN32
  HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
  int descriptor_ = -1;
#endif
};

ProcessLock global_lock() {
  ensure_state_directories();
  return ProcessLock{locks_root() / "global.lock", "global Janus state"};
}

ProcessLock toolchain_lock(const std::string &name) {
  return ProcessLock{locks_root() / ("toolchain-" + encode_name(name) + ".lock"),
                     "toolchain '" + name + "'"};
}

std::string transaction_id() {
  static std::atomic<unsigned long long> sequence{0};
#ifdef _WIN32
  const auto process = static_cast<unsigned long long>(GetCurrentProcessId());
#else
  const auto process = static_cast<unsigned long long>(::getpid());
#endif
  return std::to_string(process) + "-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()) +
         "-" + std::to_string(sequence.fetch_add(1));
}

void sync_directory(const std::filesystem::path &path);

std::filesystem::path exclusive_transaction_path(std::string &id) {
  for (unsigned attempt = 0; attempt != 100; ++attempt) {
    id = transaction_id();
    const auto candidate = transactions_root() / id;
    std::error_code error;
    if (std::filesystem::create_directory(candidate, error)) {
#ifndef _WIN32
      sync_directory(transactions_root());
#endif
      return candidate;
    }
    if (error && error != std::errc::file_exists)
      throw std::filesystem::filesystem_error{"cannot reserve transaction path",
                                              candidate, error};
  }
  throw std::runtime_error{"cannot reserve toolchain transaction path"};
}

void remove_tree(const std::filesystem::path &path, std::string_view purpose) {
  const bool existed = std::filesystem::exists(path);
  std::error_code error;
  std::filesystem::remove_all(path, error);
  if (error)
    throw std::filesystem::filesystem_error{std::string{purpose}, path, error};
#ifndef _WIN32
  if (existed && std::filesystem::is_directory(path.parent_path()))
    sync_directory(path.parent_path());
#endif
}

void sync_file(const std::filesystem::path &path) {
#ifdef _WIN32
  HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE |
                                  FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
  if (handle == INVALID_HANDLE_VALUE || !FlushFileBuffers(handle)) {
    const DWORD error = GetLastError();
    if (handle != INVALID_HANDLE_VALUE)
      CloseHandle(handle);
    throw std::runtime_error{"cannot flush '" + path.string() +
                             "' (Windows error " + std::to_string(error) + ")"};
  }
  CloseHandle(handle);
#else
  const int descriptor = ::open(path.c_str(), O_RDONLY);
  if (descriptor == -1 || ::fsync(descriptor) == -1) {
    const int saved = errno;
    if (descriptor != -1)
      ::close(descriptor);
    throw std::runtime_error{"cannot sync '" + path.string() + "': " +
                             std::generic_category().message(saved)};
  }
  ::close(descriptor);
#endif
}

void sync_directory(const std::filesystem::path &path) {
#ifdef _WIN32
  static_cast<void>(path);
#else
  const int descriptor = ::open(path.c_str(), O_RDONLY | O_DIRECTORY);
  if (descriptor == -1 || ::fsync(descriptor) == -1) {
    const int saved = errno;
    if (descriptor != -1)
      ::close(descriptor);
    throw std::runtime_error{"cannot sync directory '" + path.string() + "': " +
                             std::generic_category().message(saved)};
  }
  ::close(descriptor);
#endif
}

void sync_tree(const std::filesystem::path &root) {
  if (!std::filesystem::exists(root))
    return;
  for (const auto &entry : std::filesystem::recursive_directory_iterator(root)) {
    if (entry.is_regular_file())
      sync_file(entry.path());
  }
#ifndef _WIN32
  std::vector<std::filesystem::path> directories;
  directories.push_back(root);
  for (const auto &entry : std::filesystem::recursive_directory_iterator(root))
    if (entry.is_directory())
      directories.push_back(entry.path());
  std::sort(directories.rbegin(), directories.rend());
  for (const auto &directory : directories)
    sync_directory(directory);
#endif
}

void write_file(const std::filesystem::path &path, const std::string &contents) {
  std::ofstream output{path, std::ios::binary | std::ios::trunc};
  output << contents;
  output.flush();
  if (!output)
    throw std::runtime_error{"cannot write '" + path.string() + "'"};
  output.close();
  sync_file(path);
}

void durable_rename(const std::filesystem::path &source,
                    const std::filesystem::path &destination,
                    bool replace = false) {
#ifdef _WIN32
  DWORD flags = MOVEFILE_WRITE_THROUGH;
  if (replace)
    flags |= MOVEFILE_REPLACE_EXISTING;
  if (!MoveFileExW(source.c_str(), destination.c_str(),
                   flags))
    throw std::runtime_error{"cannot atomically publish '" +
                             destination.string() + "'"};
#else
  static_cast<void>(replace);
  std::filesystem::rename(source, destination);
  sync_directory(destination.parent_path());
  if (source.parent_path() != destination.parent_path())
    sync_directory(source.parent_path());
#endif
}

void atomic_replace(const std::filesystem::path &source,
                    const std::filesystem::path &destination) {
  durable_rename(source, destination, true);
}

#ifdef JANUSUP_TEST_HOOKS
void test_point(std::string_view point) {
  if (const char *configured = std::getenv("JANUSUP_TEST_CRASH");
      configured != nullptr && point == configured)
    std::_Exit(86);
  if (const char *configured = std::getenv("JANUSUP_TEST_PAUSE");
      configured != nullptr && point == configured) {
    const auto directory = home() / ".janusup-test";
    std::filesystem::create_directories(directory);
    const auto ready = directory / ("pause-" + std::string{point} + ".ready");
    const auto release = directory / ("pause-" + std::string{point} + ".release");
    write_file(ready, "ready\n");
    while (!std::filesystem::exists(release))
#ifdef _WIN32
      Sleep(10);
#else
      ::usleep(10000);
#endif
  }
}
#define JANUSUP_TEST_POINT(point) test_point(point)
#else
#define JANUSUP_TEST_POINT(point) static_cast<void>(0)
#endif

struct Journal {
  std::string id;
  std::string phase;
  std::string name;
  bool had_backup = false;
};

bool valid_transaction_id(std::string_view id) {
  unsigned fields = 0;
  bool has_digit = false;
  for (const char character : id) {
    if (character >= '0' && character <= '9') {
      has_digit = true;
    } else if (character == '-' && has_digit && fields < 2) {
      ++fields;
      has_digit = false;
    } else {
      return false;
    }
  }
  return fields == 2 && has_digit;
}

Journal read_journal(const std::filesystem::path &path) {
  std::ifstream input{path};
  Journal journal;
  int backup = 0;
  input >> journal.id >> journal.phase >> std::quoted(journal.name) >> backup;
  journal.had_backup = backup != 0;
  input >> std::ws;
  static const std::unordered_set<std::string> phases{
      "prepared", "bin-backup-intent", "bin-backed", "bin-publish-intent",
      "bin-published", "default-ready", "default-published", "staged",
      "backup-intent", "backed", "publish-intent", "published", "committed"};
  if (!input.eof() || !valid_transaction_id(journal.id) ||
      !phases.contains(journal.phase))
    throw std::runtime_error{"invalid transaction journal '" + path.string() + "'"};
  return journal;
}

void write_journal(const std::filesystem::path &path, const Journal &journal) {
  const auto temporary = path.string() + ".new";
  std::ostringstream contents;
  contents << journal.id << ' ' << journal.phase << ' '
           << std::quoted(journal.name) << ' ' << journal.had_backup << '\n';
  write_file(temporary, contents.str());
  atomic_replace(temporary, path);
}

std::filesystem::path quarantine_journal(const std::filesystem::path &path,
                                         std::string_view reason) {
  std::filesystem::create_directories(quarantine_root());
  const auto destination =
      quarantine_root() /
      (path.filename().string() + "-" + transaction_id() + ".journal");
  durable_rename(path, destination);
  std::cerr << "janusup: WARNING: quarantined journal '" << path.string()
            << "' as '" << destination.string() << "': " << reason << '\n';
  return destination;
}

std::unordered_set<std::string> referenced_transaction_ids() {
  std::unordered_set<std::string> result;
  if (!std::filesystem::is_directory(journals_root()))
    return result;
  for (const auto &entry : std::filesystem::directory_iterator(journals_root())) {
    if (!entry.is_regular_file() || entry.path().extension() == ".new")
      continue;
    std::ifstream input{entry.path()};
    std::string id;
    input >> id;
    // Be conservative even for an otherwise corrupt journal: a syntactically
    // valid leading id owns its transaction until the journal is quarantined.
    if (valid_transaction_id(id))
      result.insert(std::move(id));
  }
  return result;
}

void cleanup_orphan_transactions() {
  const auto referenced = referenced_transaction_ids();
  if (!std::filesystem::is_directory(transactions_root()))
    return;
  for (const auto &entry : std::filesystem::directory_iterator(transactions_root())) {
    const std::string id = entry.path().filename().string();
    if (!entry.is_directory() || !valid_transaction_id(id) || referenced.contains(id))
      continue;
    remove_tree(entry.path(), "cannot remove orphaned transaction");
#ifndef _WIN32
    sync_directory(transactions_root());
#endif
  }
}

void validate_package(const std::filesystem::path &source) {
  if (!std::filesystem::is_directory(source) ||
      (!std::filesystem::is_regular_file(source / "bin/janus")
#ifdef _WIN32
       && !std::filesystem::is_regular_file(source / "bin/janus.exe")
#endif
       ))
    throw std::runtime_error{"the package does not contain bin/janus"};
}

std::filesystem::path activation_journal() {
  return journals_root() / "activation";
}

std::filesystem::path toolchain_journal(const std::string &name) {
  return journals_root() / ("toolchain-" + encode_name(name));
}

void remove_file(const std::filesystem::path &path, std::string_view purpose) {
  std::error_code error;
  const bool removed = std::filesystem::remove(path, error);
  if (error)
    throw std::filesystem::filesystem_error{std::string{purpose}, path, error};
  if (!removed && std::filesystem::exists(path))
    throw std::runtime_error{std::string{purpose} + ": '" + path.string() + "'"};
#ifndef _WIN32
  if (removed)
    sync_directory(path.parent_path());
#endif
}

void publish_default(const std::filesystem::path &transaction,
                     const std::string &name) {
  const auto temporary = transaction / "default.new";
  if (!std::filesystem::exists(temporary))
    write_file(temporary, name + '\n');
  atomic_replace(temporary, default_file());
}

bool activation_bin_matches(const std::string &name) {
  const auto source = toolchains() / name / "bin";
  const auto published = home() / "bin";
  if (!std::filesystem::is_directory(source) ||
      !std::filesystem::is_directory(published))
    return false;
  for (const char *program : {"janus", "janusc", "janusup", "janus-lsp"}) {
#ifdef _WIN32
    const std::filesystem::path filename = std::string{program} + ".exe";
#else
    const std::filesystem::path filename = program;
#endif
    const auto expected = source / filename;
    const auto live = published / filename;
    const bool expected_exists = std::filesystem::is_regular_file(expected);
#ifdef _WIN32
    if (expected_exists != std::filesystem::is_regular_file(live))
      return false;
    if (expected_exists &&
        (std::filesystem::file_size(expected) != std::filesystem::file_size(live) ||
         sha256(expected) != sha256(live)))
      return false;
#else
    const bool live_exists = std::filesystem::exists(live) ||
                             std::filesystem::is_symlink(live);
    if (expected_exists != live_exists)
      return false;
    if (expected_exists &&
        (!std::filesystem::is_symlink(live) ||
         std::filesystem::read_symlink(live) !=
             std::filesystem::path{"../toolchains"} / name / "bin" / filename))
      return false;
#endif
  }
  return true;
}

bool observable_activation_is_valid() {
  try {
    const std::string selected = active_toolchain();
    if (selected.empty())
      return false;
    validate_name(selected);
    validate_package(toolchains() / selected);
    return activation_bin_matches(selected);
  } catch (...) {
    return false;
  }
}

void recover_activation() {
  const auto journal_path = activation_journal();
  if (!std::filesystem::exists(journal_path))
    return;
  Journal journal;
  try {
    journal = read_journal(journal_path);
  } catch (const std::exception &error) {
    const bool recoverable = observable_activation_is_valid();
    const auto quarantined = quarantine_journal(journal_path, error.what());
    if (recoverable)
      return;
    throw std::runtime_error{
        "activation state is ambiguous; the corrupt journal was quarantined at '" +
        quarantined.string() +
        "'. Repair by selecting an intact installed toolchain with "
        "'janusup default <name>'"};
  }
  try {
    validate_name(journal.name);
  } catch (const std::exception &error) {
    const bool recoverable = observable_activation_is_valid();
    const auto quarantined = quarantine_journal(journal_path, error.what());
    if (recoverable)
      return;
    throw std::runtime_error{
        "activation journal has an invalid owner and public state is ambiguous; "
        "journal quarantined at '" + quarantined.string() + "'"};
  }
  const auto transaction = transactions_root() / journal.id;
  const auto old_bin = transaction / "bin.old";
  const auto new_bin = transaction / "bin.new";
  const auto live_bin = home() / "bin";
  if (!std::filesystem::is_directory(transaction)) {
    const bool recoverable = observable_activation_is_valid();
    const auto quarantined = quarantine_journal(
        journal_path, "activation journal references a missing transaction");
    if (recoverable)
      return;
    throw std::runtime_error{
        "activation transaction is missing and public state is ambiguous; journal "
        "quarantined at '" + quarantined.string() +
        "'. Repair with 'janusup default <intact-installed-name>'"};
  }

  if (journal.phase == "prepared") {
    remove_tree(transaction, "cannot discard prepared activation");
  } else if (journal.phase == "bin-backup-intent" ||
             journal.phase == "bin-backed") {
    if (!std::filesystem::exists(live_bin) && std::filesystem::exists(old_bin))
      durable_rename(old_bin, live_bin);
    if (!std::filesystem::exists(live_bin))
      throw std::runtime_error{"cannot deterministically restore activation bin"};
    remove_tree(transaction, "cannot discard rolled-back activation");
  } else if (journal.phase == "bin-publish-intent") {
    if (!std::filesystem::exists(live_bin) && std::filesystem::exists(new_bin))
      durable_rename(new_bin, live_bin);
    if (!std::filesystem::is_directory(live_bin))
      throw std::runtime_error{"cannot deterministically publish activation bin"};
    publish_default(transaction, journal.name);
    remove_tree(transaction, "cannot clean recovered activation");
  } else if (journal.phase == "bin-published" ||
             journal.phase == "default-ready" ||
             journal.phase == "default-published") {
    if (!std::filesystem::is_directory(live_bin))
      throw std::runtime_error{"activation bin generation is missing"};
    publish_default(transaction, journal.name);
    remove_tree(transaction, "cannot clean recovered activation");
  } else {
    const bool recoverable = observable_activation_is_valid();
    const auto quarantined =
        quarantine_journal(journal_path, "invalid activation recovery phase");
    if (recoverable)
      return;
    throw std::runtime_error{"invalid activation recovery phase; journal "
                             "quarantined at '" + quarantined.string() + "'"};
  }
  remove_file(journal_path, "cannot remove recovered activation journal");
}

void build_bin_generation(const std::filesystem::path &destination,
                          const std::string &name) {
  const auto source = toolchains() / name / "bin";
  if (!std::filesystem::is_directory(source))
    throw std::runtime_error{"toolchain '" + name + "' is not installed"};
  std::filesystem::create_directory(destination);
  const auto current = home() / "bin";
  const std::unordered_set<std::string> managed{
      "janus", "janusc", "janusup", "janus-lsp",
      "janus.exe", "janusc.exe", "janusup.exe", "janus-lsp.exe"};
  if (std::filesystem::is_directory(current)) {
    for (const auto &entry : std::filesystem::directory_iterator(current)) {
      if (managed.contains(entry.path().filename().string()))
        continue;
      std::filesystem::copy(
          entry.path(), destination / entry.path().filename(),
          std::filesystem::copy_options::recursive |
              std::filesystem::copy_options::copy_symlinks);
    }
  }
  for (const char *program : {"janus", "janusc", "janusup", "janus-lsp"}) {
#ifdef _WIN32
    const std::filesystem::path filename = std::string{program} + ".exe";
    if (std::filesystem::exists(source / filename))
      std::filesystem::copy_file(source / filename, destination / filename);
#else
    if (std::filesystem::exists(source / program))
      std::filesystem::create_symlink(std::filesystem::path{"../toolchains"} /
                                          name / "bin" / program,
                                      destination / program);
#endif
  }
}

void activate_locked(const std::string &name) {
  validate_name(name);
  recover_activation();
  std::string id;
  const auto transaction = exclusive_transaction_path(id);
  const auto journal_path = activation_journal();
  Journal journal{id, "prepared", name, std::filesystem::exists(home() / "bin")};
  try {
    build_bin_generation(transaction / "bin.new", name);
    sync_tree(transaction / "bin.new");
    write_journal(journal_path, journal);
    JANUSUP_TEST_POINT("activate-after-state");
    if (journal.had_backup) {
      journal.phase = "bin-backup-intent";
      write_journal(journal_path, journal);
      durable_rename(home() / "bin", transaction / "bin.old");
      journal.phase = "bin-backed";
      write_journal(journal_path, journal);
      JANUSUP_TEST_POINT("activate-after-bin-backup");
    }
    journal.phase = "bin-publish-intent";
    write_journal(journal_path, journal);
    durable_rename(transaction / "bin.new", home() / "bin");
    journal.phase = "bin-published";
    write_journal(journal_path, journal);
    JANUSUP_TEST_POINT("activate-after-bin-publish");
    write_file(transaction / "default.new", name + '\n');
    journal.phase = "default-ready";
    write_journal(journal_path, journal);
    JANUSUP_TEST_POINT("activate-after-default-temp");
    atomic_replace(transaction / "default.new", default_file());
    journal.phase = "default-published";
    write_journal(journal_path, journal);
    JANUSUP_TEST_POINT("activate-after-default-publish");
    remove_tree(transaction, "cannot clean committed activation");
    remove_file(journal_path, "cannot remove committed activation journal");
  } catch (...) {
    const auto original = std::current_exception();
    try {
      if (std::filesystem::exists(journal_path))
        recover_activation();
      else
        remove_tree(transaction, "cannot discard unpublished activation");
    } catch (const std::exception &rollback) {
      std::cerr << "janusup: WARNING: rollback after activation failure also "
                   "failed: "
                << rollback.what() << '\n';
    }
    std::rethrow_exception(original);
  }
}

void recover_toolchain(const std::string &name) {
  const auto journal_path = toolchain_journal(name);
  if (!std::filesystem::exists(journal_path))
    return;
  const auto destination = toolchains() / name;
  auto destination_is_valid = [&] {
    try {
      validate_package(destination);
      return true;
    } catch (...) {
      return false;
    }
  };
  Journal journal;
  try {
    journal = read_journal(journal_path);
  } catch (const std::exception &error) {
    const bool recoverable = destination_is_valid();
    const auto quarantined = quarantine_journal(journal_path, error.what());
    if (recoverable)
      return;
    throw std::runtime_error{
        "toolchain '" + name +
        "' has ambiguous state; its corrupt journal was quarantined at '" +
        quarantined.string() +
        "'. Repair by reinstalling that toolchain or uninstalling its invalid "
        "directory before retrying"};
  }
  if (journal.name != name) {
    const bool recoverable = destination_is_valid();
    const auto quarantined =
        quarantine_journal(journal_path, "toolchain journal owner mismatch");
    if (recoverable)
      return;
    throw std::runtime_error{
        "toolchain journal owner mismatch; journal quarantined at '" +
        quarantined.string() + "' and the requested toolchain must be reinstalled"};
  }
  const auto transaction = transactions_root() / journal.id;
  const auto staging = transaction / "toolchain.new";
  const auto backup = transaction / "toolchain.old";
  if (!std::filesystem::is_directory(transaction)) {
    const bool recoverable = destination_is_valid();
    const auto quarantined = quarantine_journal(
        journal_path, "toolchain journal references a missing transaction");
    if (recoverable)
      return;
    throw std::runtime_error{
        "toolchain '" + name +
        "' transaction is missing and no valid installed copy is observable; "
        "journal quarantined at '" + quarantined.string() +
        "'. Repair by reinstalling the toolchain"};
  }

  if (journal.phase == "staged") {
    remove_tree(transaction, "cannot discard abandoned staging");
  } else if (journal.phase == "backup-intent" || journal.phase == "backed") {
    if (!std::filesystem::exists(destination) && std::filesystem::exists(backup))
      durable_rename(backup, destination);
    validate_package(destination);
    remove_tree(transaction, "cannot clean rolled-back replacement");
  } else if (journal.phase == "publish-intent") {
    if (!std::filesystem::exists(destination) && std::filesystem::exists(staging))
      durable_rename(staging, destination);
    validate_package(destination);
    if (journal.had_backup) {
      validate_package(backup);
      remove_tree(destination, "cannot remove interrupted replacement");
      durable_rename(backup, destination);
      validate_package(destination);
    }
    remove_tree(transaction, "cannot clean recovered publication");
  } else if (journal.phase == "published") {
    validate_package(destination);
    if (journal.had_backup) {
      if (!std::filesystem::is_directory(backup))
        throw std::runtime_error{"replacement backup is missing"};
      remove_tree(destination, "cannot remove interrupted replacement");
      durable_rename(backup, destination);
      validate_package(destination);
    }
    remove_tree(transaction, "cannot clean recovered replacement");
  } else if (journal.phase == "committed") {
    validate_package(destination);
    remove_tree(transaction, "cannot clean committed replacement");
  } else {
    const bool recoverable = destination_is_valid();
    const auto quarantined =
        quarantine_journal(journal_path, "invalid toolchain recovery phase");
    if (recoverable)
      return;
    throw std::runtime_error{"invalid recovery phase for toolchain '" + name +
                             "'; journal quarantined at '" +
                             quarantined.string() + "'"};
  }
  remove_file(journal_path, "cannot remove recovered toolchain journal");
}

void install_directory(const std::filesystem::path &source,
                       const std::string &name, bool replace = false,
                       const ToolchainSpec *metadata = nullptr,
                       std::string_view expected_active = {}) {
  validate_name(name);
  validate_package(source);
  std::filesystem::create_directories(toolchains());
  auto global = global_lock();
  auto lock = toolchain_lock(name);
  recover_activation();
  recover_toolchain(name);
  cleanup_orphan_transactions();
  if (!expected_active.empty() && active_toolchain() != expected_active)
    throw std::runtime_error{
        "active toolchain changed while preparing the update; retry the command"};
  const std::filesystem::path destination = toolchains() / name;
  if (std::filesystem::exists(destination) && !replace)
    throw std::runtime_error{"toolchain '" + name + "' is already installed"};
  std::string id;
  const auto transaction = exclusive_transaction_path(id);
  const auto staging = transaction / "toolchain.new";
  const auto backup = transaction / "toolchain.old";
  const auto journal_path = toolchain_journal(name);
  Journal journal{id, "staged", name, std::filesystem::exists(destination)};
  try {
    JANUSUP_TEST_POINT("install-after-transaction-reserved");
    std::filesystem::copy(source, staging,
                          std::filesystem::copy_options::recursive);
    validate_package(staging);
    if (metadata != nullptr) {
      std::ofstream output{staging / ".janus-version", std::ios::trunc};
      output << metadata->version << ' ' << metadata->release;
      if (!metadata->source_sha.empty())
        output << ' ' << metadata->source_sha << ' ' << metadata->published_at;
      output << '\n';
      if (!output)
        throw std::runtime_error{"cannot write toolchain metadata"};
    }
    sync_tree(staging);
    write_journal(journal_path, journal);
    JANUSUP_TEST_POINT("install-after-stage");
    if (journal.had_backup) {
      journal.phase = "backup-intent";
      write_journal(journal_path, journal);
      durable_rename(destination, backup);
      journal.phase = "backed";
      write_journal(journal_path, journal);
      JANUSUP_TEST_POINT("install-after-backup");
    }
    journal.phase = "publish-intent";
    write_journal(journal_path, journal);
    durable_rename(staging, destination);
    journal.phase = "published";
    write_journal(journal_path, journal);
    JANUSUP_TEST_POINT("install-after-publish");
    activate_locked(name);
    journal.phase = "committed";
    write_journal(journal_path, journal);
    JANUSUP_TEST_POINT("install-after-commit");
    remove_tree(transaction, "cannot clean committed installation");
    remove_file(journal_path, "cannot remove committed installation journal");
  } catch (...) {
    const auto original = std::current_exception();
    try {
      recover_activation();
      if (std::filesystem::exists(journal_path))
        recover_toolchain(name);
      else
        remove_tree(transaction, "cannot discard unpublished installation");
      if (std::filesystem::exists(destination))
        activate_locked(name);
    } catch (const std::exception &rollback) {
      std::cerr << "janusup: WARNING: rollback after installation failure also "
                   "failed: "
                << rollback.what() << '\n';
    }
    std::rethrow_exception(original);
  }
  std::cout << "installed and selected Janus toolchain '" << name << "'\n";
}

void install_spec(const std::string &name, bool replace,
                  std::string_view expected_active = {}) {
  const std::filesystem::path temporary = temporary_directory();
  try {
    const ToolchainSpec spec = resolve_spec(name, temporary);
    const std::filesystem::path package = download_package(spec, temporary);
    install_directory(package, spec.name, replace, &spec, expected_active);
    std::filesystem::remove_all(temporary);
  } catch (...) {
    std::error_code cleanup_error;
    std::filesystem::remove_all(temporary, cleanup_error);
    if (cleanup_error)
      std::cerr << "janusup: WARNING: cleanup after download failure failed: "
                << cleanup_error.message() << '\n';
    throw;
  }
}

void activate(const std::string &name) {
  validate_name(name);
  auto global = global_lock();
  auto lock = toolchain_lock(name);
  recover_activation();
  recover_toolchain(name);
  cleanup_orphan_transactions();
  activate_locked(name);
}

void uninstall(const std::string &name) {
  validate_name(name);
  std::filesystem::create_directories(toolchains());
  auto global = global_lock();
  auto lock = toolchain_lock(name);
  recover_activation();
  recover_toolchain(name);
  cleanup_orphan_transactions();
  if (active_toolchain() == name)
    throw std::runtime_error{
        "cannot uninstall the active toolchain; select another one first"};
  const std::filesystem::path path = toolchains() / name;
  if (!std::filesystem::exists(path))
    throw std::runtime_error{"toolchain '" + name + "' is not installed"};
  remove_tree(path, "cannot uninstall toolchain");
  std::cout << "uninstalled Janus toolchain '" << name << "'\n";
}

void list() {
  const std::string active = active_toolchain();
  if (!std::filesystem::exists(toolchains()))
    return;
  for (const auto &entry : std::filesystem::directory_iterator(toolchains())) {
    if (!entry.is_directory())
      continue;
    std::cout << (entry.path().filename() == active ? "* " : "  ")
              << entry.path().filename().string();
    std::ifstream metadata{entry.path() / ".janus-version"};
    std::string version;
    if (metadata >> version)
      std::cout << " (" << version << ')';
    std::cout << '\n';
  }
}

void usage() {
  std::cerr << "usage:\n"
            << "  janusup install [stable|beta|nightly|<version>]\n"
            << "  janusup install <package-directory> <name>\n"
            << "  janusup update [stable|beta|nightly|<version>]\n"
            << "  janusup uninstall <name>\n"
            << "  janusup default <name>\n"
            << "  janusup verify <archive>\n"
            << "  janusup validate-archive <archive> <expected-root>\n"
            << "  janusup list\n"
            << "  janusup home\n"
            << "  janusup --version\n";
}

} // namespace

int main(int argc, char **argv) {
  try {
    if (argc == 2 && std::string_view{argv[1]} == "--version") {
      std::cout << "janusup " << janus::build::display_version << '\n';
      return 0;
    }
    if (argc == 3 && std::string_view{argv[1]} == "--version" &&
        std::string_view{argv[2]} == "--json") {
      std::cout << janus::build::json() << '\n';
      return 0;
    }
    if (argc == 2 && std::string_view{argv[1]} == "home") {
      std::cout << home().string() << '\n';
      return 0;
    }
    if (argc == 2 && std::string_view{argv[1]} == "list") {
      list();
      return 0;
    }
    if (argc == 3 && std::string_view{argv[1]} == "default") {
      activate(argv[2]);
      std::cout << "selected Janus toolchain '" << argv[2] << "'\n";
      return 0;
    }
    if (argc == 3 && std::string_view{argv[1]} == "verify") {
      verify_attestation(argv[2]);
      std::cout << "verified provenance for '" << argv[2] << "'\n";
      return 0;
    }
    if (argc == 4 && std::string_view{argv[1]} == "validate-archive") {
      validate_archive(argv[2], argv[3]);
      return 0;
    }
    if (argc == 2 && std::string_view{argv[1]} == "install") {
      install_spec("stable", false);
      return 0;
    }
    if (argc == 3 && std::string_view{argv[1]} == "install") {
      install_spec(argv[2], false);
      return 0;
    }
    if (argc == 4 && std::string_view{argv[1]} == "install") {
      install_directory(argv[2], argv[3]);
      return 0;
    }
#ifdef JANUSUP_TEST_HOOKS
    if (argc == 4 && std::string_view{argv[1]} == "replace") {
      install_directory(argv[2], argv[3], true);
      return 0;
    }
    if (argc == 3 && std::string_view{argv[1]} == "replace-active") {
      const std::string active = active_toolchain();
      if (active.empty())
        throw std::runtime_error{"no active toolchain to update"};
      JANUSUP_TEST_POINT("update-after-active-read");
      install_directory(argv[2], active, true, nullptr, active);
      return 0;
    }
#endif
    if (argc == 2 && std::string_view{argv[1]} == "update") {
      const std::string active = active_toolchain();
      if (active.empty())
        throw std::runtime_error{"no active toolchain to update"};
      install_spec(active, true, active);
      return 0;
    }
    if (argc == 3 && std::string_view{argv[1]} == "update") {
      install_spec(argv[2], true);
      return 0;
    }
    if (argc == 3 && std::string_view{argv[1]} == "uninstall") {
      uninstall(argv[2]);
      return 0;
    }
    usage();
  } catch (const std::exception &error) {
    std::cerr << "janusup: error: " << error.what() << '\n';
  }
  return 1;
}
