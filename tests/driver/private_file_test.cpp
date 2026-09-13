#include "janus/driver/temporary_directory.hpp"
#include "private_file.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

#ifndef _WIN32
#include <csignal>
#include <sys/resource.h>
#include <sys/stat.h>
#endif

namespace {

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error{message};
}

std::string read(const std::filesystem::path &path) {
  std::ifstream input{path, std::ios::binary};
  return {std::istreambuf_iterator<char>{input}, {}};
}

void test_file(const std::filesystem::path &path) {
  {
    janus::driver::PrivateFile file{path};
#ifndef _WIN32
    // Observe immediately after creation, before even the first secret byte.
    struct stat status{};
    require(::stat(path.c_str(), &status) == 0 &&
                (status.st_mode & 0777) == 0600 && status.st_size == 0,
            "new file is not empty and private at creation");
#endif
    const std::string contents(150000, 's');
    file.write(contents);
    require(read(path) == contents, "file contents were truncated");
  }
  require(!std::filesystem::exists(path), "file survived success");
  try {
    janus::driver::PrivateFile file{path};
    file.write("secret");
    throw std::runtime_error{"simulated curl failure"};
  } catch (const std::runtime_error &error) {
    require(std::string{error.what()} == "simulated curl failure",
            error.what());
  }
  require(!std::filesystem::exists(path), "file survived exception");
}

void reject_existing(const std::filesystem::path &path) {
  bool rejected = false;
  try {
    janus::driver::PrivateFile file{path};
    file.write("secret");
  } catch (const std::runtime_error &error) {
    rejected = true;
    require(std::string{error.what()}.find("secret") == std::string::npos,
            "error exposed the secret");
  }
  require(rejected, "existing path was accepted");
  require(std::filesystem::symlink_status(path).type() !=
              std::filesystem::file_type::not_found,
          "existing path was removed");
}

void test_collisions(const std::filesystem::path &root) {
  const auto existing = root / "existing";
  std::ofstream{existing} << "sentinel";
  reject_existing(existing);
  require(read(existing) == "sentinel", "existing file was modified");
  const auto directory = root / "directory";
  std::filesystem::create_directory(directory);
  reject_existing(directory);
#ifndef _WIN32
  const auto link = root / "link";
  std::filesystem::create_symlink(existing, link);
  reject_existing(link);
  require(std::filesystem::is_symlink(link) && read(existing) == "sentinel",
          "symlink or target was modified");
  const auto dangling = root / "dangling";
  const auto absent = root / "absent";
  std::filesystem::create_symlink(absent, dangling);
  reject_existing(dangling);
  require(!std::filesystem::exists(absent), "dangling symlink was followed");
#endif
}

#ifndef _WIN32
void test_write_failure(const std::filesystem::path &path) {
  struct rlimit previous{};
  require(::getrlimit(RLIMIT_FSIZE, &previous) == 0, "cannot read file limit");
  const auto handler = std::signal(SIGXFSZ, SIG_IGN);
  auto limit = previous;
  limit.rlim_cur = 0;
  require(::setrlimit(RLIMIT_FSIZE, &limit) == 0, "cannot set file limit");
  bool rejected = false;
  try {
    janus::driver::PrivateFile file{path};
    file.write("secret");
  } catch (const std::runtime_error &error) {
    rejected = std::string{error.what()} ==
               "cannot write private registry request file";
  }
  const auto restored = ::setrlimit(RLIMIT_FSIZE, &previous);
  std::signal(SIGXFSZ, handler);
  require(restored == 0, "cannot restore file limit");
  require(rejected, "write failure was not reported safely");
  require(!std::filesystem::exists(path), "file survived write failure");
}
#endif

} // namespace

int main() {
#ifndef _WIN32
  const auto previous = ::umask(0);
#endif
  int result = 0;
  try {
    auto directory = janus::driver::TemporaryDirectory::create("janus-private");
    test_file(directory.path() / "auth");
    test_collisions(directory.path());
#ifndef _WIN32
    test_write_failure(directory.path() / "failed-write");
#endif
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    result = 1;
  }
#ifndef _WIN32
  ::umask(previous);
#endif
  return result;
}
