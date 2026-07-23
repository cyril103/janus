#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern uint32_t janus_graphics_rgba(uint8_t red, uint8_t green, uint8_t blue,
                                    uint8_t alpha);
extern const void *janus_graphics_last_error(void);
extern bool janus_graphics_available(void);

int main(void) {
  if (janus_graphics_rgba(0x12, 0x34, 0x56, 0x78) != UINT32_C(0x12345678)) {
    fputs("graphics runtime packs RGBA channels incorrectly\n", stderr);
    return 1;
  }

#ifdef _WIN32
  if (_putenv_s("JANUS_RAYLIB_PATH", "Z:\\missing\\raylib.dll") != 0)
    return 1;
#else
  if (setenv("JANUS_RAYLIB_PATH", "/janus/missing/libraylib.so", 1) != 0)
    return 1;
#endif

  if (janus_graphics_available()) {
    fputs("graphics runtime unexpectedly loaded a missing configured library\n",
          stderr);
    return 1;
  }
  const char *error = (const char *)janus_graphics_last_error();
  if (error == NULL || strstr(error, "JANUS_RAYLIB_PATH") == NULL) {
    fputs("graphics runtime did not expose a useful loading error\n", stderr);
    return 1;
  }

  return 0;
}
