#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
