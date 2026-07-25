#include "janus/driver/temporary_directory.hpp"

#include <filesystem>
#include <fstream>
#include <exception>
#include <iostream>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

void require(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error{message};
}

void test_unique_directories_under_concurrency() {
  constexpr std::size_t worker_count = 32;
  std::mutex mutex;
  std::set<std::filesystem::path> paths;
  std::vector<std::filesystem::path> created_paths;
  std::vector<janus::driver::TemporaryDirectory> directories;
  directories.reserve(worker_count);
  std::vector<std::thread> workers;
  std::vector<std::exception_ptr> errors(worker_count);
  workers.reserve(worker_count);

  for (std::size_t index = 0; index < worker_count; ++index) {
    workers.emplace_back([&, index] {
      try {
        auto directory = janus::driver::TemporaryDirectory::create(
            "janus-concurrency-test");
        require(std::filesystem::is_directory(directory.path()),
                "temporary directory was not created");
        std::lock_guard lock{mutex};
        paths.insert(directory.path());
        created_paths.push_back(directory.path());
        directories.push_back(std::move(directory));
      } catch (...) {
        errors[index] = std::current_exception();
      }
    });
  }
  for (std::thread &worker : workers)
    worker.join();
  for (const std::exception_ptr &error : errors) {
    if (error)
      std::rethrow_exception(error);
  }

  require(paths.size() == worker_count,
          "concurrent temporary directories were not unique");
  directories.clear();
  for (const std::filesystem::path &path : created_paths)
    require(!std::filesystem::exists(path),
            "concurrent temporary directory survived destruction");
}

void test_cleanup_after_success() {
  std::filesystem::path path;
  {
    auto directory =
        janus::driver::TemporaryDirectory::create("janus-cleanup-test");
    path = directory.path();
    std::ofstream{directory.path() / "artifact.o"} << "temporary";
    require(std::filesystem::exists(path / "artifact.o"),
            "temporary artifact was not created");
  }
  require(!std::filesystem::exists(path),
          "temporary directory survived normal scope exit");
}

void test_cleanup_during_exception() {
  std::filesystem::path path;
  try {
    auto directory =
        janus::driver::TemporaryDirectory::create("janus-error-cleanup-test");
    path = directory.path();
    std::ofstream{directory.path() / "artifact.o"} << "temporary";
    throw std::runtime_error{"simulated compilation or link error"};
  } catch (const std::runtime_error &) {
  }
  require(!std::filesystem::exists(path),
          "temporary directory survived exception unwinding");
}

} // namespace

int main() {
  try {
    test_unique_directories_under_concurrency();
    test_cleanup_after_success();
    test_cleanup_during_exception();
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
