#include <stdint.h>
#include <stdio.h>
#include <string.h>

void janus_process_initialize(int argc, char **argv);
uint64_t janus_process_argument_count(void);
const char *janus_process_argument_data(uint64_t index);
uint64_t janus_process_argument_size(uint64_t index);
intptr_t janus_process_environment(const char *name, uint64_t length);
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
  CHECK(janus_process_argument_count() == (uint64_t)argc);
  CHECK(janus_process_argument_size(0) == strlen(argv[0]));
  CHECK(memcmp(janus_process_argument_data(0), argv[0], strlen(argv[0])) == 0);
  CHECK(janus_process_argument_data((uint64_t)argc) == NULL);

  const intptr_t path = janus_process_environment("PATH", 4);
  CHECK(path > 0);
  CHECK(janus_process_text_data(path) != NULL);
  CHECK(janus_process_text_size(path) > 0);
  janus_process_text_destroy(path);

  CHECK(janus_process_environment("JANUS_PROCESS_MISSING_6D52B", 27) == 0);
  CHECK(janus_process_environment("A\0B", 3) == -1);
  CHECK(janus_system_error_category() == JANUS_SYSTEM_INVALID_INPUT);

  const char *missing = "janus-process-definitely-missing-6d52b";
  CHECK(janus_process_run(missing, (uint64_t)strlen(missing), NULL, NULL, 0,
                          NULL, 0, 1, 1) == -1);
  CHECK(janus_system_error_category() == JANUS_SYSTEM_NOT_FOUND);
  return 0;
}
