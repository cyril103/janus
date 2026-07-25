#include <stdint.h>
#include <stdio.h>

uint64_t janus_monotonic_nanoseconds(void);
uint64_t janus_wall_nanoseconds(void);
uint64_t janus_random_advance(uint64_t state);
uint64_t janus_random_mix(uint64_t value);
uint64_t janus_random_seed(void);

#define CHECK(condition)                                                     \
  do {                                                                       \
    if (!(condition)) {                                                      \
      (void)fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__,          \
                    __LINE__, #condition);                                    \
      return 1;                                                              \
    }                                                                        \
  } while (0)

int main(void) {
  uint64_t previous = janus_monotonic_nanoseconds();
  for (int index = 0; index < 10000; ++index) {
    const uint64_t current = janus_monotonic_nanoseconds();
    CHECK(current >= previous);
    previous = current;
  }

  CHECK(janus_wall_nanoseconds() > UINT64_C(1700000000000000000));

  uint64_t state = 0;
  state = janus_random_advance(state);
  CHECK(janus_random_mix(state) == UINT64_C(16294208416658607535));
  state = janus_random_advance(state);
  CHECK(janus_random_mix(state) == UINT64_C(7960286522194355700));
  state = janus_random_advance(state);
  CHECK(janus_random_mix(state) == UINT64_C(487617019471545679));

  (void)janus_random_seed();
  return 0;
}
