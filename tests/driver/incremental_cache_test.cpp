#include "janus/driver/incremental_cache.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

void require(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error{message};
}

std::filesystem::path temporary_root() {
  const auto root = std::filesystem::temp_directory_path() /
                    ("janus-cache-test-" +
                     janus::driver::stable_digest(
                         std::to_string(std::chrono::steady_clock::now()
                                            .time_since_epoch()
                                            .count())));
  std::filesystem::create_directories(root);
  return root;
}

janus::driver::BuildFingerprintInput base_input() {
  return {
      "0.7.8",
      "x86_64-unknown-linux-gnu",
      {"debug", "panic-trace=full", "emit=executable"},
      "/workspace/src/main.janus",
      "def main() : int { return library.answer() }\n",
      {{"library", "public-interface-v1", "implementation-v1", "",
        "library"}},
  };
}

void test_sha256_digest_vectors() {
  require(janus::driver::stable_digest("") ==
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "SHA-256 empty-input vector mismatch");
  require(janus::driver::stable_digest("abc") ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "SHA-256 abc vector mismatch");
}

void test_fingerprint_covers_every_compatibility_input() {
  const auto original = base_input();
  const std::string fingerprint =
      janus::driver::build_fingerprint(original);

  auto changed = original;
  changed.janus_version = "0.7.9";
  require(janus::driver::build_fingerprint(changed) != fingerprint,
          "JANUS_VERSION is absent from the fingerprint");
  changed = original;
  changed.target = "aarch64-unknown-linux-gnu";
  require(janus::driver::build_fingerprint(changed) != fingerprint,
          "target is absent from the fingerprint");
  changed = original;
  changed.options[0] = "release";
  require(janus::driver::build_fingerprint(changed) != fingerprint,
          "compilation options are absent from the fingerprint");
  changed = original;
  changed.source_path = "/workspace/src/renamed.janus";
  require(janus::driver::build_fingerprint(changed) != fingerprint,
          "source path is absent from the artifact fingerprint");
  require(janus::driver::consumer_fingerprint(changed) !=
              janus::driver::consumer_fingerprint(original),
          "source path is absent from the consumer fingerprint");
  changed = original;
  changed.source += "// source change\n";
  require(janus::driver::build_fingerprint(changed) != fingerprint,
          "source contents are absent from the fingerprint");
  changed = original;
  changed.dependencies[0].public_interface = "public-interface-v2";
  require(janus::driver::build_fingerprint(changed) != fingerprint,
          "dependency public interface is absent from the fingerprint");
}

void test_const_def_contract_includes_implementation() {
  const std::string before =
      "module library\nconst def answer() : int { return 1 }\n";
  const std::string after =
      "module library\nconst def answer() : int { return 2 }\n";
  require(janus::driver::public_interface_fingerprint(before) !=
              janus::driver::public_interface_fingerprint(after),
          "const def implementation is absent from its public contract");
}

void test_dependency_contract_is_nominal_and_canonical() {
  auto original = base_input();
  original.dependencies.push_back(
      {"transitive", "value-1", "implementation", "", "base"});
  auto reordered = original;
  std::reverse(reordered.dependencies.begin(), reordered.dependencies.end());
  require(janus::driver::consumer_fingerprint(original) ==
              janus::driver::consumer_fingerprint(reordered),
          "dependency order changes the canonical fingerprint");
  auto renamed = original;
  renamed.dependencies.back().import_name = "renamed-base";
  require(janus::driver::consumer_fingerprint(original) !=
              janus::driver::consumer_fingerprint(renamed),
          "nominal dependency spelling is absent from the fingerprint");
  auto value_changed = original;
  value_changed.dependencies.back().public_interface = "value-2";
  require(janus::driver::consumer_fingerprint(original) !=
              janus::driver::consumer_fingerprint(value_changed),
          "transitive dependency value is absent from the fingerprint");
}

void test_imported_constant_interface_is_fingerprintable() {
  const std::string before =
      "module bridge\nimport base\nconst exported : int = base.answer\n";
  const std::string after =
      "module bridge\nimport base\nconst exported : int = base.other\n";
  require(!janus::driver::public_interface_fingerprint(before).empty(),
          "an imported constant contract cannot be fingerprinted");
  require(janus::driver::public_interface_fingerprint(before) !=
              janus::driver::public_interface_fingerprint(after),
          "an imported constant dependency is absent from its contract");
}

void test_external_ownership_contract_changes_public_interface() {
  const std::string borrowed = janus::driver::public_interface_fingerprint(
      "extern def use(borrow data : Ptr[byte]) : Unit");
  const std::string consumed = janus::driver::public_interface_fingerprint(
      "extern def use(consume data : Ptr[byte]) : Unit");
  require(borrowed != consumed,
          "external ownership qualifiers are absent from the public "
          "interface fingerprint");
  const std::string borrowed_return =
      janus::driver::public_interface_fingerprint(
          "extern def data() : borrow Ptr[byte]");
  const std::string owned_return = janus::driver::public_interface_fingerprint(
      "extern def data() : owned Ptr[byte]");
  require(borrowed_return != owned_return,
          "external return ownership qualifiers are absent from the public "
          "interface fingerprint");
}

void test_consumer_invalidation_uses_only_public_interface() {
  const auto original = base_input();
  auto private_change = original;
  private_change.dependencies[0].implementation = "implementation-v2";
  require(janus::driver::consumer_fingerprint(private_change) ==
              janus::driver::consumer_fingerprint(original),
          "private dependency implementation invalidated its consumer");
  require(janus::driver::consumer_identity(private_change) ==
              janus::driver::consumer_identity(original),
          "private dependency implementation changed the consumer identity");

  auto public_change = original;
  public_change.dependencies[0].public_interface = "public-interface-v2";
  require(janus::driver::consumer_fingerprint(public_change) !=
              janus::driver::consumer_fingerprint(original),
          "public dependency interface did not invalidate its consumer");
  require(janus::driver::build_fingerprint(private_change) !=
              janus::driver::build_fingerprint(original),
          "dependency implementation did not invalidate the final artifact");
}

void test_public_interface_excludes_private_implementation() {
  const std::string before =
      "module library\n"
      "private def helper() : int { return 1 }\n"
      "def answer(value : int) : int { return helper() + value }\n";
  const std::string private_change =
      "module library\n"
      "private def helper() : int { return 2 }\n"
      "def answer(value : int) : int { return helper() + value }\n";
  const std::string public_change =
      "module library\n"
      "private def helper() : int { return 2 }\n"
      "def answer(value : int) : bool { return true }\n";
  require(janus::driver::public_interface_fingerprint(before) ==
              janus::driver::public_interface_fingerprint(private_change),
          "private implementation leaked into the public interface");
  require(janus::driver::public_interface_fingerprint(before) !=
              janus::driver::public_interface_fingerprint(public_change),
          "public signature change was not detected");

  const std::string public_const_before =
      "module library\nprivate const base : int = 1\n"
      "const exported : int = base\n";
  const std::string public_const_after =
      "module library\nprivate const base : int = 2\n"
      "const exported : int = base\n";
  require(janus::driver::public_interface_fingerprint(public_const_before) !=
              janus::driver::public_interface_fingerprint(public_const_after),
          "transitive public constant value change was not detected");
  const std::string aggregate_before =
      "module library\nstruct Pair(val left : int, val right : int) {}\n"
      "const exported : Pair = new Pair(1, 2)\n";
  const std::string aggregate_after =
      "module library\nstruct Pair(val left : int, val right : int) {}\n"
      "const exported : Pair = new Pair(1, 3)\n";
  require(janus::driver::public_interface_fingerprint(aggregate_before) !=
              janus::driver::public_interface_fingerprint(aggregate_after),
          "aggregate public constant fields were not serialized in the interface");
  require(janus::driver::public_interface_fingerprint(
              "module library\nconst exported : int = 1\n") !=
              janus::driver::public_interface_fingerprint(
                  "module library\nconst exported : int = 2\n"),
          "direct public constant value change was not detected");
  require(janus::driver::public_interface_fingerprint(
              "module library\nprivate const hidden : int = 1\n"
              "const exported : int = 7\n") ==
              janus::driver::public_interface_fingerprint(
                  "module library\nprivate const hidden : int = 2\n"
                  "const exported : int = 7\n"),
          "unreferenced private constant leaked into the public interface");

  const std::string internal_before =
      "module library\n"
      "class Box() { internal def helper(value : int) : int { return value } }\n"
      "def answer(value : int) : int { return value }\n";
  const std::string internal_change =
      "module library\n"
      "class Box() { internal def helper(value : bool) : int { return 0 } }\n"
      "def answer(value : int) : int { return value }\n";
  require(janus::driver::public_interface_fingerprint(internal_before) ==
              janus::driver::public_interface_fingerprint(internal_change),
          "module-internal declaration leaked into the public interface");

  const std::string dynamic_before =
      "module library\nprivate def seed() : int { return 1 }\n"
      "private val offset : int = seed()\ndef answer() : int { return offset }\n";
  const std::string dynamic_change =
      "module library\nprivate def seed() : int { return 2 }\n"
      "private val offset : int = seed()\ndef answer() : int { return offset }\n";
  require(janus::driver::public_interface_fingerprint(dynamic_before) ==
              janus::driver::public_interface_fingerprint(dynamic_change),
          "private dynamic global implementation leaked into the interface");
  const std::string constant_before =
      "module library\nval answer : int = 1\n";
  const std::string constant_change =
      "module library\nval answer : int = 2\n";
  require(janus::driver::public_interface_fingerprint(constant_before) ==
              janus::driver::public_interface_fingerprint(constant_change),
          "replaceable public global initializer leaked into the interface");

  const std::string layout_before =
      "module library\nclass Box(private val hidden : int) {}\n";
  const std::string layout_change =
      "module library\nclass Box(private val hidden : usize) {}\n";
  require(janus::driver::public_interface_fingerprint(layout_before) !=
              janus::driver::public_interface_fingerprint(layout_change),
          "private field layout change was absent from the ABI interface");

  const std::string generic_before =
      "module library\n"
      "def identity[T](value : T) : T { return value }\n";
  const std::string generic_change =
      "module library\n"
      "def identity[T](value : T) : T { val copy : T = value return copy }\n";
  require(janus::driver::public_interface_fingerprint(generic_before) !=
              janus::driver::public_interface_fingerprint(generic_change),
          "generic implementation was absent from the compilation interface");
  const std::string generic_private_before =
      "module library\n"
      "private def helper() : int { return 1 }\n"
      "def identity[T](value : T) : T { return value }\n";
  const std::string generic_private_after =
      "module library\n"
      "private def helper() : int { return 2 }\n"
      "def identity[T](value : T) : T { return value }\n";
  require(janus::driver::public_interface_fingerprint(generic_private_before) ==
              janus::driver::public_interface_fingerprint(generic_private_after),
          "unrelated private change invalidated a module with a public generic");
}

void test_store_is_atomic_concurrent_and_validated() {
  const auto root = temporary_root();
  const auto source = root / "artifact.bin";
  std::ofstream{source, std::ios::binary} << "known-good-artifact";
  janus::driver::IncrementalCache cache{root / "cache"};
  const std::string key = janus::driver::build_fingerprint(base_input());
  const std::string identity = janus::driver::build_identity(base_input());

  const std::string consumer =
      janus::driver::consumer_fingerprint(base_input());
  std::vector<std::thread> writers;
  for (int index = 0; index < 12; ++index)
    writers.emplace_back(
        [&] { cache.store(key, identity, source, consumer); });
  for (auto &writer : writers)
    writer.join();

  const auto restored = root / "restored.bin";
  require(cache.restore(key, identity, restored) ==
              janus::driver::CacheLookup::Hit,
          "concurrent cache entry was not readable");
  std::ifstream input{restored, std::ios::binary};
  require(std::string{std::istreambuf_iterator<char>{input},
                      std::istreambuf_iterator<char>{}} ==
              "known-good-artifact",
          "restored artifact differs from stored artifact");

  const auto conflicting = root / "conflicting.bin";
  std::ofstream{conflicting, std::ios::binary} << "different-artifact";
  cache.store(key, identity, conflicting, consumer);
  std::ifstream converged{conflicting, std::ios::binary};
  require(std::string{std::istreambuf_iterator<char>{converged},
                      std::istreambuf_iterator<char>{}} ==
              "known-good-artifact",
          "a competing cache writer did not converge on the winning artifact");
  converged.close();

  require(cache.restore(key, identity + "-collision", restored) ==
              janus::driver::CacheLookup::Miss,
          "a fingerprint collision reused incompatible inputs");

  std::ofstream{cache.artifact_path(key), std::ios::binary | std::ios::trunc}
      << "corrupt";
  std::ofstream{restored, std::ios::binary | std::ios::trunc}
      << "existing-output";
  require(cache.restore(key, identity, restored) ==
              janus::driver::CacheLookup::Corrupt,
          "corrupt artifact was accepted");
  std::ifstream existing_output{restored, std::ios::binary};
  require(std::string{std::istreambuf_iterator<char>{existing_output},
                      std::istreambuf_iterator<char>{}} == "existing-output",
          "corrupt cache changed the requested output");
  require(!std::filesystem::exists(cache.entry_path(key)),
          "corrupt cache entry was not quarantined/removed");

  const auto victim = root / "must-not-be-deleted";
  std::ofstream{victim, std::ios::binary} << "user data";
  std::filesystem::create_directories(cache.entry_path(key).parent_path());
  std::ofstream{cache.entry_path(key), std::ios::binary}
      << "schema=1\nidentity=" << identity << "\nartifact="
      << victim.string() << "\ndigest=invalid\n";
  require(cache.restore(key, identity, restored) ==
              janus::driver::CacheLookup::Corrupt,
          "malicious artifact metadata was not rejected");
  require(std::filesystem::is_regular_file(victim),
          "cache metadata was allowed to delete an external file");
  input.close();
  existing_output.close();
  std::filesystem::remove_all(root);
}

void test_corrupt_consumer_is_invalidated_and_repaired() {
  const auto root = temporary_root();
  janus::driver::IncrementalCache cache{root / "cache"};
  const auto input = base_input();
  const std::string key = janus::driver::consumer_fingerprint(input);
  const std::string identity = janus::driver::consumer_identity(input);
  const auto bitcode = root / "consumer.bc";
  const auto restored = root / "restored.bc";
  std::ofstream{bitcode, std::ios::binary} << "valid-consumer";
  cache.store_consumer(key, identity, bitcode, {"f:main"});
  std::ofstream{cache.consumer_path(key), std::ios::binary | std::ios::trunc}
      << "corrupt";
  require(!cache.restore_consumer(key, identity, restored),
          "corrupt consumer cache was accepted");
  require(!std::filesystem::exists(cache.consumer_path(key)),
          "corrupt consumer cache was not invalidated");
  cache.store_consumer(key, identity, bitcode, {"f:main"});
  require(cache.restore_consumer(key, identity, restored),
          "consumer cache was not repairable after corruption");
  std::filesystem::remove_all(root);
}

void test_interrupted_entry_is_ignored() {
  const auto root = temporary_root();
  janus::driver::IncrementalCache cache{root / "cache"};
  const std::string key = janus::driver::build_fingerprint(base_input());
  std::filesystem::create_directories(cache.entry_path(key).parent_path());
  std::ofstream{cache.entry_path(key).string() + ".tmp-interrupted"}
      << "partial";
  require(cache.restore(key, janus::driver::build_identity(base_input()),
                        root / "output") ==
              janus::driver::CacheLookup::Miss,
          "interrupted temporary entry was treated as complete");
  std::filesystem::remove_all(root);
}

void test_restore_replaces_an_existing_output() {
  const auto root = temporary_root();
  janus::driver::IncrementalCache cache{root / "cache"};
  const auto input = base_input();
  const std::string key = janus::driver::build_fingerprint(input);
  const std::string identity = janus::driver::build_identity(input);
  const std::string consumer =
      janus::driver::consumer_fingerprint(input);
  const auto artifact = root / "artifact.bin";
  const auto output = root / "output.bin";
  std::ofstream{artifact, std::ios::binary} << "new-output";
  cache.store(key, identity, artifact, consumer);
  std::ofstream{output, std::ios::binary} << "stale-output";

  require(cache.restore(key, identity, output) ==
              janus::driver::CacheLookup::Hit,
          "restore to an existing output was not reported as a hit");
  std::ifstream restored{output, std::ios::binary};
  require(std::string{std::istreambuf_iterator<char>{restored},
                      std::istreambuf_iterator<char>{}} == "new-output",
          "cache hit left the pre-existing output in place");
  restored.close();
  std::filesystem::remove_all(root);
}

void test_rejects_untrusted_cache_keys() {
  const auto root = temporary_root();
  janus::driver::IncrementalCache cache{root / "cache"};
  const auto source = root / "artifact.bin";
  std::ofstream{source, std::ios::binary} << "artifact";
  const auto input = base_input();
  const std::string key = janus::driver::build_fingerprint(input);
  const std::string identity = janus::driver::build_identity(input);

  bool rejected_key = false;
  try {
    static_cast<void>(cache.entry_path("../escape"));
  } catch (const std::invalid_argument &) {
    rejected_key = true;
  }
  require(rejected_key, "cache accepted a path-traversal key");

  bool rejected_consumer = false;
  try {
    cache.store(key, identity, source, "../escape");
  } catch (const std::invalid_argument &) {
    rejected_consumer = true;
  }
  require(rejected_consumer, "cache accepted a path-traversal consumer key");
  std::filesystem::remove_all(root);
}

} // namespace

int main() {
  try {
    test_sha256_digest_vectors();
    test_fingerprint_covers_every_compatibility_input();
    test_const_def_contract_includes_implementation();
    test_dependency_contract_is_nominal_and_canonical();
    test_imported_constant_interface_is_fingerprintable();
    test_external_ownership_contract_changes_public_interface();
    test_consumer_invalidation_uses_only_public_interface();
    test_public_interface_excludes_private_implementation();
    test_store_is_atomic_concurrent_and_validated();
    test_corrupt_consumer_is_invalidated_and_repaired();
    test_interrupted_entry_is_ignored();
    test_restore_replaces_an_existing_output();
    test_rejects_untrusted_cache_keys();
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
