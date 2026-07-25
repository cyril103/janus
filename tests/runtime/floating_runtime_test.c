#if !defined(_WIN32)
#define _GNU_SOURCE
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#endif

#include <float.h>
#include <locale.h>
#if defined(__APPLE__)
#include <xlocale.h>
#endif
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
  const char *data;
  uint64_t length;
} JanusString;

double janus_parse_double(const char *data, uint64_t length, int *error);
float janus_parse_float(const char *data, uint64_t length, int *error);
uint64_t janus_text_double(char *buffer, uint64_t capacity, uint64_t offset,
                           double value);
uint64_t janus_text_float(char *buffer, uint64_t capacity, uint64_t offset,
                          float value);
uint64_t janus_text_fixed(char *buffer, uint64_t capacity, uint64_t offset,
                          double value, uint32_t digits);

enum {
  PARSE_OK,
  PARSE_EMPTY,
  PARSE_INVALID,
  PARSE_SIGN,
  PARSE_OVERFLOW,
  PARSE_UNDERFLOW,
  PARSE_NON_FINITE
};

#define CHECK(condition)                                                     \
  do {                                                                       \
    if (!(condition)) {                                                      \
      (void)fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__,          \
                    __LINE__, #condition);                                    \
      return 1;                                                              \
    }                                                                        \
  } while (0)

static int expect_double(const char *text, double expected) {
  int error = -1;
  double actual = janus_parse_double(text, (uint64_t)strlen(text), &error);
  CHECK(error == PARSE_OK);
  CHECK(actual == expected);
  return 0;
}

static int expect_float(const char *text, float expected) {
  int error = -1;
  float actual = janus_parse_float(text, (uint64_t)strlen(text), &error);
  CHECK(error == PARSE_OK);
  CHECK(actual == expected);
  return 0;
}

static int expect_error(const char *text, int expected) {
  int error = -1;
  (void)janus_parse_double(text, (uint64_t)strlen(text), &error);
  CHECK(error == expected);
  return 0;
}

static int expect_float_error(const char *text, int expected) {
  int error = -1;
  (void)janus_parse_float(text, (uint64_t)strlen(text), &error);
  CHECK(error == expected);
  return 0;
}

static int expect_format(double value, const char *expected) {
  char buffer[128] = {0};
  uint64_t size = janus_text_double(buffer, sizeof(buffer), 0, value);
  CHECK(size == strlen(expected));
  CHECK(memcmp(buffer, expected, (size_t)size) == 0);
  return 0;
}

int main(void) {
  CHECK(expect_double("1.7976931348623157e308", DBL_MAX) == 0);
  CHECK(expect_double("4.9406564584124654e-324", nextafter(0.0, 1.0)) == 0);
  CHECK(expect_float("3.4028234663852886e38", FLT_MAX) == 0);
  CHECK(expect_float("1.401298464324817e-45", nextafterf(0.0f, 1.0f)) == 0);

  CHECK(expect_error("1e309", PARSE_OVERFLOW) == 0);
  CHECK(expect_error("-1e309", PARSE_OVERFLOW) == 0);
  CHECK(expect_error("1e-4000", PARSE_UNDERFLOW) == 0);
  CHECK(expect_float_error("1e39", PARSE_OVERFLOW) == 0);
  CHECK(expect_float_error("1e-50", PARSE_UNDERFLOW) == 0);

  const char *non_finite[] = {
      "nan", "+NaN", "-NAN", "inf", "+Inf", "-INF",
      "infinity", "+Infinity", "-INFINITY"};
  for (size_t index = 0; index < sizeof(non_finite) / sizeof(non_finite[0]);
       ++index)
    CHECK(expect_error(non_finite[index], PARSE_NON_FINITE) == 0);

  CHECK(expect_double(".5", 0.5) == 0);
  CHECK(expect_double("1.", 1.0) == 0);
  CHECK(expect_double("+1e+2", 100.0) == 0);
  CHECK(expect_double("-1E-2", -0.01) == 0);
  const char *invalid[] = {
      "", " ", "1 ", " 1", "1x", "1,5", ".", "+", "-", "e1",
      "1e", "1e+", "1e-", "nanx", "infinity!"};
  for (size_t index = 0; index < sizeof(invalid) / sizeof(invalid[0]); ++index)
    CHECK(expect_error(invalid[index],
                       invalid[index][0] == '\0' ? PARSE_EMPTY : PARSE_INVALID)
          == 0);

  {
    const char delimited[] = {'1', '.', '2', '5', 'x'};
    int error = -1;
    CHECK(janus_parse_double(delimited, 4, &error) == 1.25);
    CHECK(error == PARSE_OK);
  }

  CHECK(expect_format(DBL_MAX, "1.7976931348623157e+308") == 0);
  CHECK(expect_format(nextafter(0.0, 1.0), "4.9406564584124654e-324") == 0);
  {
    char buffer[64] = {0};
    uint64_t size = janus_text_float(buffer, sizeof(buffer), 0, FLT_MAX);
    CHECK(size == strlen("3.40282347e+38"));
    CHECK(memcmp(buffer, "3.40282347e+38", (size_t)size) == 0);
  }

  {
    const char *locales[] = {"de_DE.UTF-8", "de_DE.utf8", "fr_FR.UTF-8",
                             "fr_FR.utf8", "German_Germany.1252"};
#if defined(_WIN32)
    int previous_mode = _configthreadlocale(_ENABLE_PER_THREAD_LOCALE);
    const char *saved = setlocale(LC_NUMERIC, NULL);
    char saved_copy[128] = {0};
    if (saved != NULL)
      (void)snprintf(saved_copy, sizeof(saved_copy), "%s", saved);
#endif
    for (size_t index = 0; index < sizeof(locales) / sizeof(locales[0]);
         ++index) {
#if defined(_WIN32)
      bool available = setlocale(LC_NUMERIC, locales[index]) != NULL;
#else
      locale_t candidate =
          newlocale(LC_NUMERIC_MASK, locales[index], (locale_t)0);
      bool available = candidate != (locale_t)0;
      locale_t previous =
          available ? uselocale(candidate) : (locale_t)0;
#endif
      if (available) {
        CHECK(expect_format(1.5, "1.5") == 0);
        char buffer[32] = {0};
        uint64_t size = janus_text_fixed(buffer, sizeof(buffer), 0, 1.5, 2);
        CHECK(size == 4);
        CHECK(memcmp(buffer, "1.50", 4) == 0);
        CHECK(expect_double("1.5", 1.5) == 0);
        CHECK(expect_error("1,5", PARSE_INVALID) == 0);
#if !defined(_WIN32)
        CHECK(uselocale(previous) != (locale_t)0);
        freelocale(candidate);
#endif
        break;
      }
#if !defined(_WIN32)
      if (candidate != (locale_t)0)
        freelocale(candidate);
#endif
    }
#if defined(_WIN32)
    if (saved_copy[0] != '\0')
      (void)setlocale(LC_NUMERIC, saved_copy);
    (void)_configthreadlocale(previous_mode);
#endif
  }

  return 0;
}
