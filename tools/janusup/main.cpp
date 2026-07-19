#include <array>
#include <cctype>
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
             (std::string{"gh --version"} + redirect).c_str())) == 0;
}

void verify_attestation(const std::filesystem::path &archive, bool required) {
  if (!github_cli_available()) {
    if (required)
      throw std::runtime_error{
          "GitHub CLI is required to verify artifact provenance"};
    std::cerr << "janusup: warning: install GitHub CLI to verify the "
                 "artifact attestation\n";
    return;
  }
  const char *configured = std::getenv("JANUS_ATTESTATION_REPOSITORY");
  const std::filesystem::path repository =
      configured == nullptr ? "cyril103/janus" : configured;
  const std::string command = "gh attestation verify " + shell_quote(archive) +
                              " --repo " + shell_quote(repository);
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
  return server + "/" + version + "/" + filename;
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
  fetch(distribution_location(spec.release, archive), archive_path);
  fetch(distribution_location(spec.release, archive + ".sha256"),
        checksum_path);
  if (sha256(archive_path) != expected_sha256(checksum_path))
    throw std::runtime_error{"SHA-256 verification failed for " + archive};
  if (std::getenv("JANUS_DIST_SERVER") == nullptr)
    verify_attestation(archive_path, false);

  const std::filesystem::path extracted = temporary / "package";
  std::filesystem::create_directory(extracted);
  const std::string command =
      "tar -xf " + shell_quote(archive_path) + " -C " + shell_quote(extracted);
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
      verify_attestation(argv[2], true);
      std::cout << "verified provenance for '" << argv[2] << "'\n";
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
