#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <signal.h>
#include <unistd.h>
#endif

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
int64_t janus_process_child_try_write(intptr_t handle, const void *data,
                                      uint64_t size);
int64_t janus_process_child_try_read(intptr_t handle, void *data,
                                     uint64_t size);
int32_t janus_process_child_close_input(intptr_t handle);
int32_t janus_process_child_terminate(intptr_t handle);
int32_t janus_process_child_try_wait(intptr_t handle, int32_t *exit_code);
void janus_process_child_destroy(intptr_t handle);
int32_t janus_process_result_exit_code(intptr_t result);
const char *janus_process_result_stdout_data(intptr_t result);
uint64_t janus_process_result_stdout_size(intptr_t result);
const char *janus_process_result_stderr_data(intptr_t result);
uint64_t janus_process_result_stderr_size(intptr_t result);
void janus_process_result_destroy(intptr_t result);
uint32_t janus_system_error_code(void);
int32_t janus_system_error_category(void);

enum {
  JANUS_SYSTEM_NOT_FOUND = 0,
  JANUS_SYSTEM_INVALID_INPUT = 3,
  JANUS_SYSTEM_WOULD_BLOCK = 5,
  JANUS_SYSTEM_OTHER = 10,
};

static void wait_milliseconds(unsigned milliseconds) {
#if defined(_WIN32)
  Sleep(milliseconds);
#else
  usleep(milliseconds * 1000U);
#endif
}

static uint64_t monotonic_milliseconds(void) {
#if defined(_WIN32)
  return (uint64_t)GetTickCount64();
#else
  struct timespec now = {0};
  (void)clock_gettime(CLOCK_MONOTONIC, &now);
  return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
#endif
}

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
#if defined(_WIN32)
    (void)_setmode(_fileno(stdin), _O_BINARY);
    (void)_setmode(_fileno(stdout), _O_BINARY);
#endif
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
  if (argc == 2 && strcmp(argv[1], "--silent-child") == 0) {
    wait_milliseconds(250);
    return 0;
  }
  if (argc == 2 && strcmp(argv[1], "--partial-child") == 0) {
    (void)fwrite("part", 1, 4, stdout);
    (void)fflush(stdout);
    wait_milliseconds(100);
    (void)fwrite("ial", 1, 3, stdout);
    (void)fflush(stdout);
    return 0;
  }
  if (argc == 2 && strcmp(argv[1], "--delayed-reader") == 0) {
#if defined(_WIN32)
    (void)_setmode(_fileno(stdout), _O_BINARY);
#endif
    wait_milliseconds(250);
    size_t size = 0;
    size_t capacity = 0;
    char *received = NULL;
    for (;;) {
      if (size == capacity) {
        const size_t next = capacity == 0 ? 4096 : capacity * 2;
        char *grown = (char *)realloc(received, next);
        if (grown == NULL) {
          free(received);
          return 3;
        }
        received = grown;
        capacity = next;
      }
#if defined(_WIN32)
      DWORD count = 0;
      const DWORD requested = capacity - size > UINT32_MAX
                                  ? UINT32_MAX
                                  : (DWORD)(capacity - size);
      if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), received + size, requested,
                    &count, NULL)) {
        if (GetLastError() == ERROR_BROKEN_PIPE)
          break;
        free(received);
        return 5;
      }
      size += count;
      if (count == 0)
        break;
#else
      const size_t count = fread(received + size, 1, capacity - size, stdin);
      size += count;
      if (count == 0)
        break;
#endif
    }
    if (fwrite(received, 1, size, stdout) != size) {
      free(received);
      return 4;
    }
    free(received);
    (void)fflush(stdout);
    return 0;
  }
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
  CHECK(janus_process_child_try_write(exited, "ignored", 7) == -1);
  CHECK(janus_system_error_category() == JANUS_SYSTEM_OTHER);
#if defined(_WIN32)
  CHECK(janus_system_error_code() == ERROR_BROKEN_PIPE);
#endif
  CHECK(janus_process_child_write(exited, "ignored", 7) == -1);
  janus_process_child_destroy(exited);

  const char *silent_arguments[] = {"--silent-child"};
  const uint64_t silent_lengths[] = {14};
  const intptr_t silent = janus_process_spawn(
      argv[0], (uint64_t)strlen(argv[0]), silent_arguments, silent_lengths, 1,
      NULL, 0);
  CHECK(silent > 0);
  CHECK(janus_process_child_try_read(silent, echoed, sizeof(echoed)) == -1);
  CHECK(janus_system_error_category() == JANUS_SYSTEM_WOULD_BLOCK);
  CHECK(janus_process_child_try_read(silent, NULL, 0) == 0);
  CHECK(janus_process_child_try_write(silent, NULL, 0) == 0);
  int32_t child_exit_code = -1;
  CHECK(janus_process_child_try_wait(silent, &child_exit_code) == 0);
  uint64_t operation_started = monotonic_milliseconds();
  CHECK(janus_process_child_terminate(silent) == 0);
  CHECK(monotonic_milliseconds() - operation_started < 1000U);
  int32_t wait_state = 0;
  for (unsigned attempt = 0; attempt < 500 && wait_state == 0; ++attempt) {
    wait_state = janus_process_child_try_wait(silent, &child_exit_code);
    if (wait_state == 0)
      wait_milliseconds(1);
  }
  CHECK(wait_state == 1);
#if defined(_WIN32)
  CHECK(child_exit_code == 1);
#else
  CHECK(child_exit_code == 128 + SIGKILL);
#endif
  CHECK(janus_process_child_try_wait(silent, &child_exit_code) == 1);
  /* The process has already been reaped/cached, so destruction only closes
     handles and cannot wait for or signal the child again. */
  operation_started = monotonic_milliseconds();
  janus_process_child_destroy(silent);
  CHECK(monotonic_milliseconds() - operation_started < 1000U);

  /* A successful termination request makes immediate destruction bounded,
     without requiring an intervening tryWait. */
  const intptr_t terminated_then_destroyed = janus_process_spawn(
      argv[0], (uint64_t)strlen(argv[0]), silent_arguments, silent_lengths, 1,
      NULL, 0);
  CHECK(terminated_then_destroyed > 0);
  operation_started = monotonic_milliseconds();
  CHECK(janus_process_child_terminate(terminated_then_destroyed) == 0);
  janus_process_child_destroy(terminated_then_destroyed);
  CHECK(monotonic_milliseconds() - operation_started < 1000U);

  const char *partial_arguments[] = {"--partial-child"};
  const uint64_t partial_lengths[] = {15};
  const intptr_t partial = janus_process_spawn(
      argv[0], (uint64_t)strlen(argv[0]), partial_arguments, partial_lengths,
      1, NULL, 0);
  CHECK(partial > 0);
  char fragmented[7] = {0};
  offset = 0;
  while (offset < sizeof(fragmented)) {
    const int64_t count = janus_process_child_try_read(
        partial, fragmented + offset, sizeof(fragmented) - offset);
    if (count < 0) {
      CHECK(janus_system_error_category() == JANUS_SYSTEM_WOULD_BLOCK);
      wait_milliseconds(10);
      continue;
    }
    CHECK(count > 0);
    offset += (uint64_t)count;
  }
  CHECK(memcmp(fragmented, "partial", sizeof(fragmented)) == 0);
  for (;;) {
    const int64_t count = janus_process_child_try_read(partial, echoed,
                                                       sizeof(echoed));
    if (count == 0)
      break;
    CHECK(count == -1);
    CHECK(janus_system_error_category() == JANUS_SYSTEM_WOULD_BLOCK);
    wait_milliseconds(10);
  }
  janus_process_child_destroy(partial);

  const char *delayed_arguments[] = {"--delayed-reader"};
  const uint64_t delayed_lengths[] = {16};
  const intptr_t delayed = janus_process_spawn(
      argv[0], (uint64_t)strlen(argv[0]), delayed_arguments, delayed_lengths,
      1, NULL, 0);
  CHECK(delayed > 0);
  enum { PAYLOAD_SIZE = 256 * 1024 };
  char *payload = (char *)malloc(PAYLOAD_SIZE);
  char *received = (char *)malloc(PAYLOAD_SIZE);
  CHECK(payload != NULL && received != NULL);
  for (size_t index = 0; index < PAYLOAD_SIZE; ++index)
    payload[index] = (char)((index * 37U + index / 251U) & 0xffU);
  uint64_t total = 0;
  int observed_would_block = 0;
  while (total < PAYLOAD_SIZE) {
    const int64_t count = janus_process_child_try_write(
        delayed, payload + total, PAYLOAD_SIZE - total);
    if (count < 0) {
      const int32_t category = janus_system_error_category();
      CHECK(category == JANUS_SYSTEM_WOULD_BLOCK);
#if defined(_WIN32)
      const uint32_t error = janus_system_error_code();
      CHECK(error == ERROR_NO_DATA || error == ERROR_PIPE_BUSY ||
            error == ERROR_IO_PENDING);
#endif
      observed_would_block = 1;
      wait_milliseconds(10);
    } else {
      CHECK(count > 0);
      total += (uint64_t)count;
    }
  }
  CHECK(observed_would_block);
  CHECK(janus_process_child_close_input(delayed) == 0);
  total = 0;
  while (total < PAYLOAD_SIZE) {
    const int64_t count = janus_process_child_try_read(
        delayed, received + total, PAYLOAD_SIZE - total);
    if (count < 0) {
      CHECK(janus_system_error_category() == JANUS_SYSTEM_WOULD_BLOCK);
      wait_milliseconds(10);
      continue;
    }
    CHECK(count > 0);
    total += (uint64_t)count;
  }
  CHECK(memcmp(received, payload, PAYLOAD_SIZE) == 0);
  for (;;) {
    const int64_t count = janus_process_child_try_read(delayed, received, 1);
    if (count == 0)
      break;
    CHECK(count == -1);
    CHECK(janus_system_error_category() == JANUS_SYSTEM_WOULD_BLOCK);
    wait_milliseconds(10);
  }
  free(received);
  free(payload);
  janus_process_child_destroy(delayed);
  return 0;
}
