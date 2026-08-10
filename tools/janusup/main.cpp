#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <charconv>
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

#ifndef _WIN32
#include <sys/wait.h>
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
  const std::string command =
      shell_quote(tar) +
#ifdef _WIN32
      (verbose ? " -tvf " : " -tf ") +
#else
      (verbose ? " --numeric-owner -tvf " : " -tf ") +
#endif
      shell_quote(archive) + " > " + shell_quote(output);
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
    std::error_code ignored;
    std::filesystem::remove_all(scratch, ignored);
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
};

bool is_channel(const std::string &name) {
  return name == "stable" || name == "beta" || name == "nightly";
}

ToolchainSpec resolve_spec(const std::string &name,
                           const std::filesystem::path &temporary) {
  validate_name(name);
  if (!is_channel(name))
    return {name, name, "v" + name};

  const std::filesystem::path manifest = temporary / (name + ".channel");
  fetch(distribution_location("channel-" + name, "version"), manifest);
  std::ifstream input{manifest};
  ToolchainSpec result{name, {}, {}};
  input >> result.version >> result.release;
  validate_name(result.version);
  validate_name(result.release);
  if (!input)
    throw std::runtime_error{"invalid '" + name + "' channel manifest"};
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

void activate(const std::string &name) {
  validate_name(name);
  const std::filesystem::path source = toolchains() / name / "bin";
  if (!std::filesystem::is_directory(source))
    throw std::runtime_error{"toolchain '" + name + "' is not installed"};
  std::filesystem::create_directories(home() / "bin");
  for (const char *program : {"janus", "janusc", "janusup", "janus-lsp"}) {
#ifdef _WIN32
    const std::filesystem::path filename = std::string{program} + ".exe";
    if (std::filesystem::exists(source / filename))
      std::filesystem::copy_file(
          source / filename, home() / "bin" / filename,
          std::filesystem::copy_options::overwrite_existing);
#else
    const std::filesystem::path shim = home() / "bin" / program;
    std::error_code ignored;
    std::filesystem::remove(shim, ignored);
    if (std::filesystem::exists(source / program))
      std::filesystem::create_symlink(std::filesystem::path{"../toolchains"} /
                                          name / "bin" / program,
                                      shim);
#endif
  }
  std::ofstream output{default_file(), std::ios::trunc};
  output << name << '\n';
}

void install_directory(const std::filesystem::path &source,
                       const std::string &name, bool replace = false) {
  validate_name(name);
  if (!std::filesystem::is_regular_file(source / "bin/janus")
#ifdef _WIN32
      && !std::filesystem::is_regular_file(source / "bin/janus.exe")
#endif
  )
    throw std::runtime_error{"the package does not contain bin/janus"};
  std::filesystem::create_directories(toolchains());
  const std::filesystem::path destination = toolchains() / name;
  if (std::filesystem::exists(destination) && !replace)
    throw std::runtime_error{"toolchain '" + name + "' is already installed"};
  const std::filesystem::path staging = toolchains() / ("." + name + ".new");
  const std::filesystem::path backup = toolchains() / ("." + name + ".old");
  std::filesystem::remove_all(staging);
  std::filesystem::remove_all(backup);
  std::filesystem::copy(source, staging,
                        std::filesystem::copy_options::recursive);
  if (std::filesystem::exists(destination)) {
    std::filesystem::rename(destination, backup);
    try {
      std::filesystem::rename(staging, destination);
    } catch (...) {
      std::filesystem::rename(backup, destination);
      throw;
    }
    std::filesystem::remove_all(backup);
  } else {
    std::filesystem::rename(staging, destination);
  }
  activate(name);
  std::cout << "installed and selected Janus toolchain '" << name << "'\n";
}

void install_spec(const std::string &name, bool replace) {
  const std::filesystem::path temporary = temporary_directory();
  try {
    const ToolchainSpec spec = resolve_spec(name, temporary);
    const std::filesystem::path package = download_package(spec, temporary);
    install_directory(package, spec.name, replace);
    std::ofstream metadata{toolchains() / spec.name / ".janus-version"};
    metadata << spec.version << ' ' << spec.release << '\n';
    std::filesystem::remove_all(temporary);
  } catch (...) {
    std::error_code ignored;
    std::filesystem::remove_all(temporary, ignored);
    throw;
  }
}

void uninstall(const std::string &name) {
  validate_name(name);
  if (active_toolchain() == name)
    throw std::runtime_error{
        "cannot uninstall the active toolchain; select another one first"};
  const std::filesystem::path path = toolchains() / name;
  if (!std::filesystem::exists(path))
    throw std::runtime_error{"toolchain '" + name + "' is not installed"};
  std::filesystem::remove_all(path);
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
      std::cout << "janusup " << JANUS_VERSION << '\n';
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
    if (argc == 2 && std::string_view{argv[1]} == "update") {
      const std::string active = active_toolchain();
      if (active.empty())
        throw std::runtime_error{"no active toolchain to update"};
      install_spec(active, true);
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
