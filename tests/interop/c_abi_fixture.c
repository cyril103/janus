#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

int32_t janus_c_add(int32_t left, int32_t right) { return left + right; }

void janus_c_scale(int32_t *values, size_t length, int32_t factor) {
  for (size_t index = 0; index < length; ++index)
    values[index] *= factor;
}

double janus_c_average(const int32_t *values, size_t length) {
  int32_t total = 0;
  for (size_t index = 0; index < length; ++index)
    total += values[index];
  return (double)total / (double)length;
}

int8_t janus_c_increment_byte(int8_t value) { return (int8_t)(value + 1); }

uint32_t janus_c_next_codepoint(uint32_t value) { return value + 1; }

bool janus_c_is_positive(int32_t value) { return value > 0; }

size_t janus_c_count_nonzero(const int32_t *values, size_t length) {
  size_t count = 0;
  for (size_t index = 0; index < length; ++index)
    if (values[index] != 0)
      ++count;
  return count;
}

int32_t janus_c_hidden_increment(int32_t value) { return value + 1; }

bool janus_c_check_variadic(int32_t marker, ...) {
  va_list arguments;
  va_start(arguments, marker);
  const int promoted_byte = va_arg(arguments, int);
  const int promoted_bool = va_arg(arguments, int);
  const uint32_t codepoint = va_arg(arguments, uint32_t);
  const size_t size = va_arg(arguments, size_t);
  const double ratio = va_arg(arguments, double);
  const char *text = va_arg(arguments, const char *);
  va_end(arguments);
  return marker == 123 && promoted_byte == -7 && promoted_bool == 1 &&
         codepoint == 65 && size == 9 && ratio == 2.5 &&
         strcmp(text, "Janus") == 0;
}
