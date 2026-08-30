#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

void *janus_shared_alias(void *pointer);
void janus_shared_forget(void *pointer);
bool janus_shared_retain(uint64_t *references);

#define CHECK(condition)                                                     \
  do {                                                                       \
    if (!(condition)) {                                                      \
      (void)fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__,          \
                    __LINE__, #condition);                                    \
      return 1;                                                              \
    }                                                                        \
  } while (0)

int main(void) {
  int value = 42;
  CHECK(janus_shared_alias(&value) == &value);
  janus_shared_forget(&value);

  uint64_t references = UINT64_C(1);
  CHECK(janus_shared_retain(&references));
  CHECK(references == UINT64_C(2));

  references = UINT64_MAX;
  CHECK(!janus_shared_retain(&references));
  CHECK(references == UINT64_MAX);
  return 0;
}
