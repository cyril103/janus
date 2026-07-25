#if !defined(_WIN32)
#define _GNU_SOURCE
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#endif

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  const char *data;
  uint64_t length;
} JanusString;

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

static uint64_t janus_copy_text(char *buffer, uint64_t capacity,
                                uint64_t offset, JanusString text) {
  if (offset > capacity || text.length > capacity - offset)
    return UINT64_MAX;
  if (text.length != 0)
    memcpy(buffer + offset, text.data, (size_t)text.length);
  return text.length;
}

#define JANUS_FORMAT_INTEGER(name, type, format)                             \
  uint64_t name(char *buffer, uint64_t capacity, uint64_t offset, type value) { \
    if (offset > capacity) return UINT64_MAX;                                \
    int count = snprintf(buffer + offset, (size_t)(capacity - offset),       \
                         format, value);                                     \
    return count < 0 || (uint64_t)count >= capacity - offset                 \
               ? UINT64_MAX : (uint64_t)count;                              \
  }

JANUS_FORMAT_INTEGER(janus_text_int, int32_t, "%" PRId32)
JANUS_FORMAT_INTEGER(janus_text_uint, uint32_t, "%" PRIu32)
JANUS_FORMAT_INTEGER(janus_text_long, int64_t, "%" PRId64)
JANUS_FORMAT_INTEGER(janus_text_ulong, uint64_t, "%" PRIu64)
JANUS_FORMAT_INTEGER(janus_text_byte, int8_t, "%" PRId8)
JANUS_FORMAT_INTEGER(janus_text_ubyte, uint8_t, "%" PRIu8)
JANUS_FORMAT_INTEGER(janus_text_short, int16_t, "%" PRId16)
JANUS_FORMAT_INTEGER(janus_text_ushort, uint16_t, "%" PRIu16)
JANUS_FORMAT_INTEGER(janus_text_isize, int64_t, "%" PRId64)
JANUS_FORMAT_INTEGER(janus_text_usize, uint64_t, "%" PRIu64)

uint64_t janus_text_copy(char *buffer, uint64_t capacity, uint64_t offset,
                         JanusString text) {
  return janus_copy_text(buffer, capacity, offset, text);
}

uint64_t janus_text_bool(char *buffer, uint64_t capacity, uint64_t offset,
                         bool value) {
  JanusString text = value ? (JanusString){"true", 4}
                           : (JanusString){"false", 5};
  return janus_copy_text(buffer, capacity, offset, text);
}

uint64_t janus_text_char(char *buffer, uint64_t capacity, uint64_t offset,
                         uint32_t codepoint) {
  char bytes[4];
  uint64_t size;
  if (codepoint > UINT32_C(0x10ffff) ||
      (codepoint >= UINT32_C(0xd800) && codepoint <= UINT32_C(0xdfff)))
    codepoint = UINT32_C(0xfffd);
  if (codepoint <= 0x7f) {
    bytes[0] = (char)codepoint; size = 1;
  } else if (codepoint <= 0x7ff) {
    bytes[0] = (char)(0xc0 | (codepoint >> 6));
    bytes[1] = (char)(0x80 | (codepoint & 0x3f)); size = 2;
  } else if (codepoint <= 0xffff) {
    bytes[0] = (char)(0xe0 | (codepoint >> 12));
    bytes[1] = (char)(0x80 | ((codepoint >> 6) & 0x3f));
    bytes[2] = (char)(0x80 | (codepoint & 0x3f)); size = 3;
  } else {
    bytes[0] = (char)(0xf0 | (codepoint >> 18));
    bytes[1] = (char)(0x80 | ((codepoint >> 12) & 0x3f));
    bytes[2] = (char)(0x80 | ((codepoint >> 6) & 0x3f));
    bytes[3] = (char)(0x80 | (codepoint & 0x3f)); size = 4;
  }
  return janus_copy_text(buffer, capacity, offset,
                         (JanusString){bytes, size});
}

static int janus_c_locale_snprintf(char *buffer, size_t size,
                                   const char *format, int precision,
                                   double value) {
#if defined(_WIN32)
  _locale_t locale = _create_locale(LC_NUMERIC, "C");
  if (locale == NULL)
    return -1;
  int count = _snprintf_l(buffer, size, format, locale, precision, value);
  _free_locale(locale);
  return count;
#else
  locale_t locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
  if (locale == (locale_t)0)
    return -1;
  locale_t previous = uselocale(locale);
  if (previous == (locale_t)0) {
    freelocale(locale);
    return -1;
  }
  int count = snprintf(buffer, size, format, precision, value);
  (void)uselocale(previous);
  freelocale(locale);
  return count;
#endif
}

static uint64_t janus_text_floating(char *buffer, uint64_t capacity,
                                    uint64_t offset, double value,
                                    int precision) {
  if (!isfinite(value) || offset > capacity)
    return UINT64_MAX;
  if (capacity - offset > SIZE_MAX)
    return UINT64_MAX;
  int count = janus_c_locale_snprintf(
      buffer + offset, (size_t)(capacity - offset), "%.*g", precision, value);
  if (count < 0 || (uint64_t)count >= capacity - offset)
    return UINT64_MAX;
  return (uint64_t)count;
}

uint64_t janus_text_float(char *buffer, uint64_t capacity, uint64_t offset,
                          float value) {
  return janus_text_floating(buffer, capacity, offset, (double)value, 9);
}

uint64_t janus_text_double(char *buffer, uint64_t capacity, uint64_t offset,
                           double value) {
  return janus_text_floating(buffer, capacity, offset, value, 17);
}

uint64_t janus_text_hex(char *buffer, uint64_t capacity, uint64_t offset,
                        uint64_t value, bool uppercase) {
  if (offset > capacity) return UINT64_MAX;
  int count = snprintf(buffer + offset, (size_t)(capacity - offset),
                       uppercase ? "%" PRIX64 : "%" PRIx64, value);
  return count < 0 || (uint64_t)count >= capacity - offset
             ? UINT64_MAX : (uint64_t)count;
}

uint64_t janus_text_fixed(char *buffer, uint64_t capacity, uint64_t offset,
                          double value, uint32_t digits) {
  if (!isfinite(value) || digits > 18 || offset > capacity) return UINT64_MAX;
  if (capacity - offset > SIZE_MAX)
    return UINT64_MAX;
  int count = janus_c_locale_snprintf(
      buffer + offset, (size_t)(capacity - offset), "%.*f", (int)digits,
      value);
  if (count < 0 || (uint64_t)count >= capacity - offset) return UINT64_MAX;
  return (uint64_t)count;
}

enum {
  JANUS_PARSE_OK,
  JANUS_PARSE_EMPTY,
  JANUS_PARSE_INVALID,
  JANUS_PARSE_SIGN,
  JANUS_PARSE_OVERFLOW,
  JANUS_PARSE_UNDERFLOW,
  JANUS_PARSE_NON_FINITE
};

static uint64_t janus_parse_unsigned_value(const char *data, uint64_t length,
                                           uint64_t maximum, int *error) {
  *error = JANUS_PARSE_OK;
  if (length == 0) { *error = JANUS_PARSE_EMPTY; return 0; }
  if (data[0] == '+' || data[0] == '-') {
    *error = JANUS_PARSE_SIGN; return 0;
  }
  uint64_t value = 0;
  for (uint64_t index = 0; index < length; ++index) {
    unsigned digit = (unsigned char)data[index] - (unsigned)'0';
    if (digit > 9) { *error = JANUS_PARSE_INVALID; return 0; }
    if (value > (maximum - digit) / 10) {
      *error = JANUS_PARSE_OVERFLOW; return 0;
    }
    value = value * 10 + digit;
  }
  return value;
}

static int64_t janus_parse_signed_value(const char *data, uint64_t length,
                                        int bits, int *error) {
  *error = JANUS_PARSE_OK;
  if (length == 0) { *error = JANUS_PARSE_EMPTY; return 0; }
  bool negative = data[0] == '-';
  uint64_t start = (negative || data[0] == '+') ? 1 : 0;
  if (start == length) { *error = JANUS_PARSE_INVALID; return 0; }
  uint64_t positive_max = bits == 64 ? (uint64_t)INT64_MAX
                                     : (UINT64_C(1) << (bits - 1)) - 1;
  uint64_t limit = negative ? positive_max + 1 : positive_max;
  uint64_t magnitude = 0;
  for (uint64_t index = start; index < length; ++index) {
    unsigned digit = (unsigned char)data[index] - (unsigned)'0';
    if (digit > 9) { *error = JANUS_PARSE_INVALID; return 0; }
    if (magnitude > (limit - digit) / 10) {
      *error = negative ? JANUS_PARSE_UNDERFLOW : JANUS_PARSE_OVERFLOW;
      return 0;
    }
    magnitude = magnitude * 10 + digit;
  }
  if (!negative) return (int64_t)magnitude;
  if (magnitude == positive_max + 1)
    return bits == 64 ? INT64_MIN : -(int64_t)magnitude;
  return -(int64_t)magnitude;
}

#define JANUS_PARSE_SIGNED(name, type, bits)                                 \
  type name(const char *data, uint64_t length, int *error) {                 \
    return (type)janus_parse_signed_value(data, length, bits, error);         \
  }
#define JANUS_PARSE_UNSIGNED(name, type, maximum)                            \
  type name(const char *data, uint64_t length, int *error) {                 \
    return (type)janus_parse_unsigned_value(data, length, maximum, error);    \
  }

JANUS_PARSE_SIGNED(janus_parse_int, int32_t, 32)
JANUS_PARSE_UNSIGNED(janus_parse_uint, uint32_t, UINT32_MAX)
JANUS_PARSE_SIGNED(janus_parse_long, int64_t, 64)
JANUS_PARSE_UNSIGNED(janus_parse_ulong, uint64_t, UINT64_MAX)
JANUS_PARSE_SIGNED(janus_parse_byte, int8_t, 8)
JANUS_PARSE_UNSIGNED(janus_parse_ubyte, uint8_t, UINT8_MAX)
JANUS_PARSE_SIGNED(janus_parse_short, int16_t, 16)
JANUS_PARSE_UNSIGNED(janus_parse_ushort, uint16_t, UINT16_MAX)
JANUS_PARSE_SIGNED(janus_parse_isize, int64_t, 64)
JANUS_PARSE_UNSIGNED(janus_parse_usize, uint64_t, UINT64_MAX)

static char janus_ascii_lower(char value) {
  return value >= 'A' && value <= 'Z' ? (char)(value + ('a' - 'A')) : value;
}

static bool janus_ascii_word(const char *data, uint64_t length,
                             const char *word) {
  size_t word_length = strlen(word);
  if (length != (uint64_t)word_length)
    return false;
  for (size_t index = 0; index < word_length; ++index)
    if (janus_ascii_lower(data[index]) != word[index])
      return false;
  return true;
}

static bool janus_validate_floating(const char *data, uint64_t length,
                                    int *error) {
  *error = JANUS_PARSE_OK;
  if (length == 0) {
    *error = JANUS_PARSE_EMPTY;
    return false;
  }
  uint64_t index = (data[0] == '+' || data[0] == '-') ? 1 : 0;
  if (index < length &&
      (janus_ascii_word(data + index, length - index, "nan") ||
       janus_ascii_word(data + index, length - index, "inf") ||
       janus_ascii_word(data + index, length - index, "infinity"))) {
    *error = JANUS_PARSE_NON_FINITE;
    return false;
  }
  bool digit_seen = false;
  while (index < length && data[index] >= '0' && data[index] <= '9') {
    digit_seen = true;
    ++index;
  }
  if (index < length && data[index] == '.') {
    ++index;
    while (index < length && data[index] >= '0' && data[index] <= '9') {
      digit_seen = true;
      ++index;
    }
  }
  if (!digit_seen) {
    *error = JANUS_PARSE_INVALID;
    return false;
  }
  if (index < length && (data[index] == 'e' || data[index] == 'E')) {
    ++index;
    if (index < length && (data[index] == '+' || data[index] == '-'))
      ++index;
    uint64_t exponent_start = index;
    while (index < length && data[index] >= '0' && data[index] <= '9')
      ++index;
    if (index == exponent_start) {
      *error = JANUS_PARSE_INVALID;
      return false;
    }
  }
  if (index != length) {
    *error = JANUS_PARSE_INVALID;
    return false;
  }
  return true;
}

static char *janus_copy_floating(const char *data, uint64_t length,
                                 int *error) {
  if (length > (uint64_t)(SIZE_MAX - 1)) {
    *error = JANUS_PARSE_INVALID;
    return NULL;
  }
  char *copy = (char *)malloc((size_t)length + 1);
  if (copy == NULL) {
    *error = JANUS_PARSE_INVALID;
    return NULL;
  }
  memcpy(copy, data, (size_t)length);
  copy[length] = '\0';
  return copy;
}

double janus_parse_double(const char *data, uint64_t length, int *error) {
  if (!janus_validate_floating(data, length, error))
    return 0;
  char *copy = janus_copy_floating(data, length, error);
  if (copy == NULL)
    return 0;
  char *end = NULL;
  errno = 0;
#if defined(_WIN32)
  _locale_t locale = _create_locale(LC_NUMERIC, "C");
  double result = locale == NULL ? 0 : _strtod_l(copy, &end, locale);
  if (locale != NULL)
    _free_locale(locale);
#else
  locale_t locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
  double result = locale == (locale_t)0 ? 0 : strtod_l(copy, &end, locale);
  if (locale != (locale_t)0)
    freelocale(locale);
#endif
  int conversion_errno = errno;
  bool conversion_failed = end != copy + length;
  free(copy);
  if (conversion_failed) {
    *error = JANUS_PARSE_INVALID;
    return 0;
  }
  if (!isfinite(result)) {
    *error = JANUS_PARSE_OVERFLOW;
    return 0;
  }
  if (result == 0.0 && conversion_errno == ERANGE) {
    *error = JANUS_PARSE_UNDERFLOW;
    return 0;
  }
  return result;
}

float janus_parse_float(const char *data, uint64_t length, int *error) {
  if (!janus_validate_floating(data, length, error))
    return 0;
  char *copy = janus_copy_floating(data, length, error);
  if (copy == NULL)
    return 0;
  char *end = NULL;
  errno = 0;
#if defined(_WIN32)
  _locale_t locale = _create_locale(LC_NUMERIC, "C");
  float result = locale == NULL ? 0 : _strtof_l(copy, &end, locale);
  if (locale != NULL)
    _free_locale(locale);
#else
  locale_t locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
  float result = locale == (locale_t)0 ? 0 : strtof_l(copy, &end, locale);
  if (locale != (locale_t)0)
    freelocale(locale);
#endif
  int conversion_errno = errno;
  bool conversion_failed = end != copy + length;
  free(copy);
  if (conversion_failed) {
    *error = JANUS_PARSE_INVALID;
    return 0;
  }
  if (!isfinite(result)) {
    *error = JANUS_PARSE_OVERFLOW;
    return 0;
  }
  if (result == 0.0f && conversion_errno == ERANGE) {
    *error = JANUS_PARSE_UNDERFLOW;
    return 0;
  }
  return result;
}

uint32_t janus_parse_char(const char *data, uint64_t length, int *error) {
  *error = JANUS_PARSE_OK;
  if (length == 0) { *error = JANUS_PARSE_EMPTY; return 0; }
  const unsigned char first = (unsigned char)data[0];
  uint32_t value;
  uint64_t needed;
  if (first <= 0x7f) { value = first; needed = 1; }
  else if (first >= 0xc2 && first <= 0xdf) {
    value = first & 0x1f; needed = 2;
  } else if (first >= 0xe0 && first <= 0xef) {
    value = first & 0x0f; needed = 3;
  } else if (first >= 0xf0 && first <= 0xf4) {
    value = first & 0x07; needed = 4;
  } else { *error = JANUS_PARSE_INVALID; return 0; }
  if (length != needed) { *error = JANUS_PARSE_INVALID; return 0; }
  for (uint64_t index = 1; index < needed; ++index) {
    unsigned char next = (unsigned char)data[index];
    if ((next & 0xc0) != 0x80) { *error = JANUS_PARSE_INVALID; return 0; }
    value = (value << 6) | (next & 0x3f);
  }
  if ((needed == 2 && value < 0x80) ||
      (needed == 3 && value < 0x800) ||
      (needed == 4 && value < 0x10000) ||
      value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) {
    *error = JANUS_PARSE_INVALID; return 0;
  }
  return value;
}

static void (*janus_panic_cleanup)(void);

void janus_set_panic_cleanup(void (*cleanup)(void)) {
  janus_panic_cleanup = cleanup;
}

_Noreturn void janus_panic(const char *data, uint64_t size) {
  (void)fwrite(data, 1, (size_t)size, stderr);
  (void)fflush(stderr);
  if (janus_panic_cleanup != NULL) {
    void (*cleanup)(void) = janus_panic_cleanup;
    janus_panic_cleanup = NULL;
    cleanup();
  }
  (void)fflush(stdout);
  abort();
}
