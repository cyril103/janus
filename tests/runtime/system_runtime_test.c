#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

intptr_t janus_system_open(const char *path, uint64_t length, int32_t mode);
intptr_t janus_system_read(intptr_t handle, void *data, uint64_t capacity);
intptr_t janus_system_write(intptr_t handle, const void *data, uint64_t size);
int32_t janus_system_close(intptr_t handle);
int32_t janus_system_remove(const char *path, uint64_t length);
uint32_t janus_system_error_code(void);
int32_t janus_system_error_category(void);

enum {
  JANUS_SYSTEM_NOT_FOUND = 0,
  JANUS_SYSTEM_INVALID_INPUT = 3,
};

#define CHECK(condition)                                                     \
  do {                                                                       \
    if (!(condition)) {                                                      \
      (void)fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__,          \
                    __LINE__, #condition);                                    \
      return 1;                                                              \
    }                                                                        \
  } while (0)

int main(void) {
  const char *missing = "janus-system-definitely-missing.file";
  CHECK(janus_system_open(missing, (uint64_t)strlen(missing), 0) == -1);
  CHECK(janus_system_error_category() == JANUS_SYSTEM_NOT_FOUND);
  CHECK(janus_system_error_code() != 0);

  const char invalid_utf8[] = {(char)0xc3, (char)0x28};
  CHECK(janus_system_open(invalid_utf8, sizeof(invalid_utf8), 0) == -1);
  CHECK(janus_system_error_category() == JANUS_SYSTEM_INVALID_INPUT);

  const char embedded_nul[] = {'a', '\0', 'b'};
  CHECK(janus_system_open(embedded_nul, sizeof(embedded_nul), 0) == -1);
  CHECK(janus_system_error_category() == JANUS_SYSTEM_INVALID_INPUT);

  char path[96];
#if defined(_WIN32)
  (void)snprintf(path, sizeof(path), "janus-system-%lu.tmp",
                 (unsigned long)GetCurrentProcessId());
#else
  (void)snprintf(path, sizeof(path), "janus-system-%ld.tmp", (long)getpid());
#endif
  (void)remove(path);

  const intptr_t output =
      janus_system_open(path, (uint64_t)strlen(path), 1);
  CHECK(output != -1);
  CHECK(janus_system_write(output, "portable", 8) == 8);
  CHECK(janus_system_close(output) == 0);
  CHECK(janus_system_close(output) == -1);
  CHECK(janus_system_error_category() == JANUS_SYSTEM_INVALID_INPUT);

  const intptr_t input =
      janus_system_open(path, (uint64_t)strlen(path), 0);
  CHECK(input != -1);
  char data[9] = {0};
  CHECK(janus_system_read(input, data, 8) == 8);
  CHECK(memcmp(data, "portable", 8) == 0);
  CHECK(janus_system_read(input, data, 8) == 0);
  CHECK(janus_system_close(input) == 0);

  CHECK(janus_system_remove(path, (uint64_t)strlen(path)) == 0);
  CHECK(janus_system_remove(path, (uint64_t)strlen(path)) == -1);
  CHECK(janus_system_error_category() == JANUS_SYSTEM_NOT_FOUND);
  return 0;
}
