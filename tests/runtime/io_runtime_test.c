#include <stdint.h>
#include <stdio.h>
#include <string.h>

intptr_t janus_io_standard_handle(int32_t stream);
int32_t janus_io_flush(intptr_t handle);
int32_t janus_io_valid_utf8(const void *data, uint64_t size);
int32_t janus_bytes_valid_utf8(const void *data, uint64_t offset,
                               uint64_t size);
intptr_t janus_system_open(const char *path, uint64_t length, int32_t mode);
intptr_t janus_system_write(intptr_t handle, const void *data, uint64_t size);
int32_t janus_system_close(intptr_t handle);
int32_t janus_system_remove(const char *path, uint64_t length);
int32_t janus_system_error_category(void);

enum {
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

int main(void) {
  CHECK(janus_io_standard_handle(0) != -1);
  CHECK(janus_io_standard_handle(1) != -1);
  CHECK(janus_io_standard_handle(2) != -1);
  CHECK(janus_io_standard_handle(3) == -1);
  CHECK(janus_system_error_category() == JANUS_SYSTEM_INVALID_INPUT);

  const char valid[] = {'a', '\0', (char)0xc3, (char)0xa9};
  const char invalid[] = {(char)0xc3, (char)0x28};
  CHECK(janus_io_valid_utf8(valid, sizeof(valid)) == 1);
  CHECK(janus_io_valid_utf8(invalid, sizeof(invalid)) == 0);
  CHECK(janus_io_valid_utf8(NULL, 0) == 1);
  CHECK(janus_io_valid_utf8(NULL, 1) == 0);
  const uint8_t prefixed[] = {0xff, 'o', 'k'};
  CHECK(janus_bytes_valid_utf8(prefixed, 1, 2) == 1);
  CHECK(janus_bytes_valid_utf8(prefixed, 0, sizeof(prefixed)) == 0);

  const char *path = "janus-io-runtime.tmp";
  (void)remove(path);
  const intptr_t output = janus_system_open(path, (uint64_t)strlen(path), 1);
  CHECK(output != -1);
  CHECK(janus_system_write(output, "buffered", 8) == 8);
  CHECK(janus_io_flush(output) == 0);
  CHECK(janus_system_close(output) == 0);
  CHECK(janus_system_remove(path, (uint64_t)strlen(path)) == 0);

  CHECK(janus_io_flush(-1) == -1);
  CHECK(janus_system_error_category() == JANUS_SYSTEM_INVALID_INPUT);
  return 0;
}
