#include <stdbool.h>
#include <stdint.h>

#ifdef _WIN32
#define RAYLIB_EXPORT __declspec(dllexport)
#else
#define RAYLIB_EXPORT
#endif

typedef struct {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
  uint8_t alpha;
} Color;

static bool window_ready;

RAYLIB_EXPORT void InitWindow(int width, int height, const char *title) {
  window_ready = width > 0 && height > 0 && title != 0;
}

RAYLIB_EXPORT bool IsWindowReady(void) { return window_ready; }

RAYLIB_EXPORT bool WindowShouldClose(void) { return false; }

RAYLIB_EXPORT void CloseWindow(void) { window_ready = false; }

RAYLIB_EXPORT void SetTargetFPS(int frames_per_second) {
  (void)frames_per_second;
}

RAYLIB_EXPORT void BeginDrawing(void) {}

RAYLIB_EXPORT void EndDrawing(void) {}

RAYLIB_EXPORT void ClearBackground(Color color) { (void)color; }

RAYLIB_EXPORT void DrawPixel(int x, int y, Color color) {
  (void)x;
  (void)y;
  (void)color;
}

RAYLIB_EXPORT void DrawLine(int start_x, int start_y, int end_x, int end_y,
                           Color color) {
  (void)start_x;
  (void)start_y;
  (void)end_x;
  (void)end_y;
  (void)color;
}

RAYLIB_EXPORT void DrawCircle(int center_x, int center_y, float radius,
                             Color color) {
  (void)center_x;
  (void)center_y;
  (void)radius;
  (void)color;
}

RAYLIB_EXPORT void DrawRectangle(int x, int y, int width, int height,
                                Color color) {
  (void)x;
  (void)y;
  (void)width;
  (void)height;
  (void)color;
}

RAYLIB_EXPORT void DrawText(const char *text, int x, int y, int font_size,
                           Color color) {
  (void)text;
  (void)x;
  (void)y;
  (void)font_size;
  (void)color;
}

RAYLIB_EXPORT bool IsKeyDown(int key) { return key == 263; }

RAYLIB_EXPORT bool IsKeyPressed(int key) { return key == 256; }

RAYLIB_EXPORT int GetMouseX(void) { return 123; }

RAYLIB_EXPORT int GetMouseY(void) { return 234; }

RAYLIB_EXPORT bool IsMouseButtonDown(int button) { return button == 0; }

RAYLIB_EXPORT bool IsMouseButtonPressed(int button) { return button == 1; }
