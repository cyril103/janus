#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *janus_alloc(uint64_t bytes) { return malloc((size_t)bytes); }

void *janus_realloc(void *pointer, uint64_t bytes) {
  return realloc(pointer, (size_t)bytes);
}

void janus_free(void *pointer) { free(pointer); }

int32_t janus_memcmp(const void *left, const void *right, uint64_t size) {
  return (int32_t)memcmp(left, right, (size_t)size);
}

void janus_write_stdout(const char *data, uint64_t size) {
  (void)fwrite(data, 1, (size_t)size, stdout);
}

void janus_print_int(int32_t value) { (void)fprintf(stdout, "%" PRId32, value); }

void janus_print_uint(uint32_t value) {
  (void)fprintf(stdout, "%" PRIu32, value);
}

void janus_print_long(int64_t value) {
  (void)fprintf(stdout, "%" PRId64, value);
}

void janus_print_ulong(uint64_t value) {
  (void)fprintf(stdout, "%" PRIu64, value);
}

void janus_print_byte(int32_t value) {
  (void)fprintf(stdout, "%" PRId32, value);
}

void janus_print_ubyte(uint32_t value) {
  (void)fprintf(stdout, "%" PRIu32, value);
}

void janus_print_short(int32_t value) {
  (void)fprintf(stdout, "%" PRId32, value);
}

void janus_print_ushort(uint32_t value) {
  (void)fprintf(stdout, "%" PRIu32, value);
}

void janus_print_usize(uint64_t value) {
  (void)fprintf(stdout, "%" PRIu64, value);
}

void janus_print_isize(int64_t value) {
  (void)fprintf(stdout, "%" PRId64, value);
}

void janus_print_double(double value) {
  (void)fprintf(stdout, "%.17g", value);
}

void janus_print_float(float value) {
  (void)fprintf(stdout, "%.9g", (double)value);
}

void janus_print_bool(bool value) {
  const char *text = value ? "true" : "false";
  janus_write_stdout(text, value ? 4 : 5);
}

void janus_print_char(uint32_t codepoint) {
  char bytes[4];
  size_t size;
  if (codepoint <= 0x7f) {
    bytes[0] = (char)codepoint;
    size = 1;
  } else if (codepoint <= 0x7ff) {
    bytes[0] = (char)(0xc0 | (codepoint >> 6));
    bytes[1] = (char)(0x80 | (codepoint & 0x3f));
    size = 2;
  } else if (codepoint <= 0xffff) {
    bytes[0] = (char)(0xe0 | (codepoint >> 12));
    bytes[1] = (char)(0x80 | ((codepoint >> 6) & 0x3f));
    bytes[2] = (char)(0x80 | (codepoint & 0x3f));
    size = 3;
  } else {
    bytes[0] = (char)(0xf0 | (codepoint >> 18));
    bytes[1] = (char)(0x80 | ((codepoint >> 12) & 0x3f));
    bytes[2] = (char)(0x80 | ((codepoint >> 6) & 0x3f));
    bytes[3] = (char)(0x80 | (codepoint & 0x3f));
    size = 4;
  }
  janus_write_stdout(bytes, (uint64_t)size);
}

_Noreturn void janus_panic(const char *data, uint64_t size) {
  (void)fwrite(data, 1, (size_t)size, stderr);
  (void)fflush(stderr);
  abort();
}
