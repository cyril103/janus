#include <stdint.h>
#include <stdio.h>
#include <string.h>

void janus_process_initialize(int argc, char **argv);
uint64_t janus_process_argument_count(void);
const char *janus_process_argument_data(uint64_t index);
uint64_t janus_process_argument_size(uint64_t index);
intptr_t janus_process_environment(const char *name, uint64_t length);
intptr_t janus_process_current_directory(void);
const char *janus_process_text_data(intptr_t text);
uint64_t janus_process_text_size(intptr_t text);
void janus_process_text_destroy(intptr_t text);
intptr_t janus_process_run(const char *executable, uint64_t executable_length,
                           const char *const *arguments,
                           const uint64_t *argument_lengths,
                           uint64_t argument_count,
                           const char *working_directory,
                           uint64_t working_directory_length,
                           int32_t capture_stdout, int32_t capture_stderr);
intptr_t janus_process_spawn(const char *executable, uint64_t executable_length,
                             const char *const *arguments,
                             const uint64_t *argument_lengths,
                             uint64_t argument_count,
                             const char *working_directory,
                             uint64_t working_directory_length);
int64_t janus_process_child_write(intptr_t handle, const void *data,
                                  uint64_t size);
int64_t janus_process_child_read(intptr_t handle, void *data, uint64_t size);
int32_t janus_process_child_close_input(intptr_t handle);
void janus_process_child_destroy(intptr_t handle);
int32_t janus_process_result_exit_code(intptr_t result);
const char *janus_process_result_stdout_data(intptr_t result);
uint64_t janus_process_result_stdout_size(intptr_t result);
const char *janus_process_result_stderr_data(intptr_t result);
uint64_t janus_process_result_stderr_size(intptr_t result);
void janus_process_result_destroy(intptr_t result);
int32_t janus_system_error_category(void);

enum {
  JANUS_SYSTEM_NOT_FOUND = 0,
  JANUS_SYSTEM_INVALID_INPUT = 3,
};

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      (void)fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, \
                    #condition);                                               \
      return 1;                                                                \
    }                                                                          \
  } while (0)

int main(int argc, char **argv) {
  janus_process_initialize(argc, argv);
  if (argc == 2 && strcmp(argv[1], "--interactive-child") == 0) {
    char buffer[32];
    while (fgets(buffer, sizeof(buffer), stdin) != NULL) {
      const size_t count = strlen(buffer);
      if (fwrite(buffer, 1, count, stdout) != count)
        return 2;
      fflush(stdout);
    }
    return 0;
  }
  if (argc == 2 && strcmp(argv[1], "--interactive-exit") == 0)
    return 0;
  CHECK(janus_process_argument_count() == (uint64_t)argc);
  CHECK(janus_process_argument_size(0) == strlen(argv[0]));
  CHECK(memcmp(janus_process_argument_data(0), argv[0], strlen(argv[0])) == 0);
  CHECK(janus_process_argument_data((uint64_t)argc) == NULL);

  const intptr_t path = janus_process_environment("PATH", 4);
  CHECK(path > 0);
  CHECK(janus_process_text_data(path) != NULL);
  CHECK(janus_process_text_size(path) > 0);
  janus_process_text_destroy(path);

  const intptr_t directory = janus_process_current_directory();
  CHECK(directory > 0);
  CHECK(janus_process_text_data(directory) != NULL);
  CHECK(janus_process_text_size(directory) > 0);
  janus_process_text_destroy(directory);

  CHECK(janus_process_environment("JANUS_PROCESS_MISSING_6D52B", 27) == 0);
  CHECK(janus_process_environment("A\0B", 3) == -1);
  CHECK(janus_system_error_category() == JANUS_SYSTEM_INVALID_INPUT);

  const char *missing = "janus-process-definitely-missing-6d52b";
  CHECK(janus_process_run(missing, (uint64_t)strlen(missing), NULL, NULL, 0,
                          NULL, 0, 1, 1) == -1);
  CHECK(janus_system_error_category() == JANUS_SYSTEM_NOT_FOUND);

  const char *interactive_arguments[] = {"--interactive-child"};
  const uint64_t interactive_lengths[] = {19};
  const intptr_t child = janus_process_spawn(argv[0], (uint64_t)strlen(argv[0]),
                                             interactive_arguments,
                                             interactive_lengths, 1, NULL, 0);
  CHECK(child > 0);
  CHECK(janus_process_child_write(child, "ping\n", 5) == 5);
  char echoed[5] = {0};
  uint64_t offset = 0;
  while (offset < sizeof(echoed)) {
    const int64_t count = janus_process_child_read(
        child, echoed + offset, (uint64_t)sizeof(echoed) - offset);
    CHECK(count > 0);
    offset += (uint64_t)count;
  }
  CHECK(memcmp(echoed, "ping\n", sizeof(echoed)) == 0);
  CHECK(janus_process_child_close_input(child) == 0);
  janus_process_child_destroy(child);

  const char *exit_arguments[] = {"--interactive-exit"};
  const uint64_t exit_lengths[] = {18};
  const intptr_t exited =
      janus_process_spawn(argv[0], (uint64_t)strlen(argv[0]), exit_arguments,
                          exit_lengths, 1, NULL, 0);
  CHECK(exited > 0);
  CHECK(janus_process_child_read(exited, echoed, sizeof(echoed)) == 0);
  CHECK(janus_process_child_write(exited, "ignored", 7) == -1);
  janus_process_child_destroy(exited);
  return 0;
}
