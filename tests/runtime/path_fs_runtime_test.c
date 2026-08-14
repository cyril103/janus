#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

int32_t janus_path_normalize(const char *path, uint64_t length, char **output,
                             uint64_t *output_length);
int32_t janus_path_join(const char *base, uint64_t base_length,
                        const char *child, uint64_t child_length, char **output,
                        uint64_t *output_length);
int32_t janus_path_is_absolute(const char *path, uint64_t length);
int32_t janus_path_component_count(const char *path, uint64_t length);
int32_t janus_path_component(const char *path, uint64_t length, uint32_t index,
                             char **output, uint64_t *output_length);
int32_t janus_fs_read_file(const char *path, uint64_t length, void **output,
                           uint64_t *output_length);
int32_t janus_fs_write_file_atomic(const char *path, uint64_t length,
                                   const void *data, uint64_t size);
int32_t janus_fs_create_directory(const char *path, uint64_t length,
                                  int32_t recursive);
int32_t janus_fs_create_temporary_directory(const char *prefix,
                                            uint64_t prefix_length,
                                            char **output,
                                            uint64_t *output_length);
int32_t janus_fs_remove_directory(const char *path, uint64_t length);
int32_t janus_fs_remove_directory_all(const char *path, uint64_t length);
int32_t janus_fs_metadata(const char *path, uint64_t length, int32_t *kind,
                          uint64_t *size);
intptr_t janus_fs_directory_open(const char *path, uint64_t length);
int32_t janus_fs_directory_next(intptr_t handle, char **output,
                                uint64_t *output_length);
int32_t janus_fs_directory_close(intptr_t handle);
int32_t janus_system_remove(const char *path, uint64_t length);
void janus_fs_free(void *data);

enum {
  JANUS_FS_FILE = 0,
  JANUS_FS_DIRECTORY = 1,
  JANUS_FS_SYMBOLIC_LINK = 2,
};

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      (void)fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, \
                    #condition);                                               \
      return 1;                                                                \
    }                                                                          \
  } while (0)

static int equals(const char *data, uint64_t length, const char *expected) {
  return length == strlen(expected) &&
         memcmp(data, expected, (size_t)length) == 0;
}

int main(void) {
  char *normalized = NULL;
  uint64_t normalized_length = 0;
#if defined(_WIN32)
  const char *messy = "alpha/./beta/../gamma";
  const char *expected = "alpha\\gamma";
  const char *joined_expected = "alpha\\gamma\\delta";
#else
  const char *messy = "alpha/./beta/../gamma";
  const char *expected = "alpha/gamma";
  const char *joined_expected = "alpha/gamma/delta";
#endif
  CHECK(janus_path_normalize(messy, (uint64_t)strlen(messy), &normalized,
                             &normalized_length) == 0);
  CHECK(equals(normalized, normalized_length, expected));
  CHECK(janus_path_is_absolute(normalized, normalized_length) == 0);
  CHECK(janus_path_component_count(normalized, normalized_length) == 2);

  char *component = NULL;
  uint64_t component_length = 0;
  CHECK(janus_path_component(normalized, normalized_length, 1, &component,
                             &component_length) == 0);
  CHECK(equals(component, component_length, "gamma"));
  janus_fs_free(component);

  char *joined = NULL;
  uint64_t joined_length = 0;
  CHECK(janus_path_join(normalized, normalized_length, "delta", 5, &joined,
                        &joined_length) == 0);
  CHECK(equals(joined, joined_length, joined_expected));
  janus_fs_free(joined);
  janus_fs_free(normalized);

  char *temporary = NULL;
  uint64_t temporary_length = 0;
  CHECK(janus_fs_create_temporary_directory(
            "janus-é", (uint64_t)strlen("janus-é"), &temporary,
            &temporary_length) == 0);

  char *nested = NULL;
  uint64_t nested_length = 0;
  CHECK(janus_path_join(temporary, temporary_length, "répertoire",
                        (uint64_t)strlen("répertoire"), &nested,
                        &nested_length) == 0);
  CHECK(janus_fs_create_directory(nested, nested_length, 1) == 0);

  char *file = NULL;
  uint64_t file_length = 0;
  CHECK(janus_path_join(nested, nested_length, "données.txt",
                        (uint64_t)strlen("données.txt"), &file,
                        &file_length) == 0);
  CHECK(janus_fs_write_file_atomic(file, file_length, "old", 3) == 0);
  CHECK(janus_fs_write_file_atomic(file, file_length, "portable", 8) == 0);

  void *contents = NULL;
  uint64_t contents_length = 0;
  CHECK(janus_fs_read_file(file, file_length, &contents, &contents_length) ==
        0);
  CHECK(contents_length == 8);
  CHECK(memcmp(contents, "portable", 8) == 0);
  janus_fs_free(contents);

  int32_t kind = -1;
  uint64_t size = 0;
  CHECK(janus_fs_metadata(file, file_length, &kind, &size) == 0);
  CHECK(kind == JANUS_FS_FILE);
  CHECK(size == 8);
  CHECK(janus_fs_metadata(nested, nested_length, &kind, &size) == 0);
  CHECK(kind == JANUS_FS_DIRECTORY);

  const intptr_t directory = janus_fs_directory_open(nested, nested_length);
  CHECK(directory != -1);
  int saw_file = 0;
  for (;;) {
    char *entry = NULL;
    uint64_t entry_length = 0;
    const int32_t next =
        janus_fs_directory_next(directory, &entry, &entry_length);
    CHECK(next >= 0);
    if (next == 0)
      break;
    if (equals(entry, entry_length, "données.txt"))
      saw_file = 1;
    janus_fs_free(entry);
  }
  CHECK(saw_file);
  CHECK(janus_fs_directory_close(directory) == 0);

#if !defined(_WIN32)
  char *outside = NULL;
  uint64_t outside_length = 0;
  CHECK(janus_fs_create_temporary_directory(
            "janus-outside", (uint64_t)strlen("janus-outside"), &outside,
            &outside_length) == 0);
  char *outside_file = NULL;
  uint64_t outside_file_length = 0;
  CHECK(janus_path_join(outside, outside_length, "target.txt", 10,
                        &outside_file, &outside_file_length) == 0);
  CHECK(janus_fs_write_file_atomic(outside_file, outside_file_length, "safe",
                                   4) == 0);
  char *directory_link = NULL;
  uint64_t directory_link_length = 0;
  CHECK(janus_path_join(nested, nested_length, "directory-link", 14,
                        &directory_link, &directory_link_length) == 0);
  CHECK(symlink(outside, directory_link) == 0);
  const intptr_t linked_directory =
      janus_fs_directory_open(directory_link, directory_link_length);
  CHECK(linked_directory >= 0);
  CHECK(janus_fs_directory_close(linked_directory) == 0);
  CHECK(janus_system_remove(directory_link, directory_link_length) == 0);
  janus_fs_free(directory_link);
  char *link = NULL;
  uint64_t link_length = 0;
  CHECK(janus_path_join(nested, nested_length, "lien", (uint64_t)strlen("lien"),
                        &link, &link_length) == 0);
  CHECK(symlink(outside, link) == 0);
  CHECK(janus_fs_metadata(link, link_length, &kind, &size) == 0);
  CHECK(kind == JANUS_FS_SYMBOLIC_LINK);
  janus_fs_free(link);
#endif

  CHECK(janus_fs_remove_directory_all(temporary, temporary_length) == 0);
  CHECK(janus_fs_remove_directory_all(temporary, temporary_length) == 0);
#if !defined(_WIN32)
  CHECK(janus_fs_metadata(outside, outside_length, &kind, &size) == 0);
  CHECK(kind == JANUS_FS_DIRECTORY);
  CHECK(janus_fs_metadata(outside_file, outside_file_length, &kind, &size) ==
        0);
  CHECK(kind == JANUS_FS_FILE);
  CHECK(janus_system_remove(outside_file, outside_file_length) == 0);
  CHECK(janus_fs_remove_directory(outside, outside_length) == 0);
  janus_fs_free(outside_file);
  janus_fs_free(outside);
#endif
  janus_fs_free(file);
  janus_fs_free(nested);
  janus_fs_free(temporary);
  return 0;
}
