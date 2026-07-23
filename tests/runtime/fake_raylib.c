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

typedef struct {
  float x;
  float y;
} Vector2;

typedef struct {
  Vector2 offset;
  Vector2 target;
  float rotation;
  float zoom;
} Camera2D;

typedef struct {
  uint32_t id;
  int width;
  int height;
  int mipmaps;
  int format;
} Texture2D;

typedef struct {
  void *buffer;
  void *processor;
  uint32_t sample_rate;
  uint32_t sample_size;
  uint32_t channels;
} AudioStream;

typedef struct {
  AudioStream stream;
  uint32_t frame_count;
} Sound;

typedef struct {
  AudioStream stream;
  uint32_t frame_count;
  bool looping;
  int context_type;
  void *context_data;
} Music;

static bool window_ready;
static bool audio_ready;
static bool window_fullscreen;
static bool window_maximized;
static bool cursor_hidden;

RAYLIB_EXPORT void InitWindow(int width, int height, const char *title) {
  window_ready = width > 0 && height > 0 && title != 0;
}

RAYLIB_EXPORT bool IsWindowReady(void) { return window_ready; }

RAYLIB_EXPORT bool WindowShouldClose(void) { return false; }

RAYLIB_EXPORT bool IsWindowFullscreen(void) { return window_fullscreen; }
RAYLIB_EXPORT bool IsWindowHidden(void) { return false; }
RAYLIB_EXPORT bool IsWindowMinimized(void) { return false; }
RAYLIB_EXPORT bool IsWindowMaximized(void) { return window_maximized; }
RAYLIB_EXPORT bool IsWindowFocused(void) { return true; }
RAYLIB_EXPORT bool IsWindowResized(void) { return true; }

RAYLIB_EXPORT void CloseWindow(void) { window_ready = false; }

RAYLIB_EXPORT void ToggleFullscreen(void) {
  window_fullscreen = !window_fullscreen;
}

RAYLIB_EXPORT void MaximizeWindow(void) { window_maximized = true; }
RAYLIB_EXPORT void MinimizeWindow(void) { window_maximized = false; }
RAYLIB_EXPORT void RestoreWindow(void) { window_maximized = false; }
RAYLIB_EXPORT void SetWindowTitle(const char *title) { (void)title; }
RAYLIB_EXPORT void SetWindowPosition(int x, int y) {
  (void)x;
  (void)y;
}
RAYLIB_EXPORT void SetWindowSize(int width, int height) {
  (void)width;
  (void)height;
}
RAYLIB_EXPORT void SetWindowOpacity(float opacity) { (void)opacity; }
RAYLIB_EXPORT int GetScreenWidth(void) { return 800; }
RAYLIB_EXPORT int GetScreenHeight(void) { return 450; }

RAYLIB_EXPORT void SetTargetFPS(int frames_per_second) {
  (void)frames_per_second;
}

RAYLIB_EXPORT void BeginDrawing(void) {}

RAYLIB_EXPORT void EndDrawing(void) {}

RAYLIB_EXPORT void BeginMode2D(Camera2D camera) { (void)camera; }

RAYLIB_EXPORT void EndMode2D(void) {}

RAYLIB_EXPORT Vector2 GetScreenToWorld2D(Vector2 position, Camera2D camera) {
  Vector2 result = {
      (position.x - camera.offset.x) / camera.zoom + camera.target.x,
      (position.y - camera.offset.y) / camera.zoom + camera.target.y};
  return result;
}

RAYLIB_EXPORT Vector2 GetWorldToScreen2D(Vector2 position, Camera2D camera) {
  Vector2 result = {
      (position.x - camera.target.x) * camera.zoom + camera.offset.x,
      (position.y - camera.target.y) * camera.zoom + camera.offset.y};
  return result;
}

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

RAYLIB_EXPORT Texture2D LoadTexture(const char *file_name) {
  Texture2D texture = {1, 64, 32, 1, 7};
  if (file_name == 0)
    texture.id = 0;
  return texture;
}

RAYLIB_EXPORT bool IsTextureValid(Texture2D texture) {
  return texture.id != 0;
}

RAYLIB_EXPORT void UnloadTexture(Texture2D texture) { (void)texture; }

RAYLIB_EXPORT void DrawTexture(Texture2D texture, int x, int y, Color tint) {
  (void)texture;
  (void)x;
  (void)y;
  (void)tint;
}

RAYLIB_EXPORT void InitAudioDevice(void) { audio_ready = true; }

RAYLIB_EXPORT void CloseAudioDevice(void) { audio_ready = false; }

RAYLIB_EXPORT bool IsAudioDeviceReady(void) { return audio_ready; }

RAYLIB_EXPORT void SetMasterVolume(float volume) { (void)volume; }

RAYLIB_EXPORT Sound LoadSound(const char *file_name) {
  Sound sound = {{(void *)1, 0, 44100, 16, 2}, 128};
  if (file_name == 0)
    sound.stream.buffer = 0;
  return sound;
}

RAYLIB_EXPORT bool IsSoundValid(Sound sound) {
  return sound.stream.buffer != 0;
}

RAYLIB_EXPORT void UnloadSound(Sound sound) { (void)sound; }

RAYLIB_EXPORT void PlaySound(Sound sound) { (void)sound; }

RAYLIB_EXPORT void StopSound(Sound sound) { (void)sound; }

RAYLIB_EXPORT bool IsSoundPlaying(Sound sound) {
  return IsSoundValid(sound);
}

RAYLIB_EXPORT void SetSoundVolume(Sound sound, float volume) {
  (void)sound;
  (void)volume;
}

RAYLIB_EXPORT void SetSoundPitch(Sound sound, float pitch) {
  (void)sound;
  (void)pitch;
}

RAYLIB_EXPORT void SetSoundPan(Sound sound, float pan) {
  (void)sound;
  (void)pan;
}

RAYLIB_EXPORT Music LoadMusicStream(const char *file_name) {
  Music music = {{(void *)1, 0, 44100, 16, 2}, 1024, true, 0, (void *)1};
  if (file_name == 0)
    music.context_data = 0;
  return music;
}

RAYLIB_EXPORT bool IsMusicValid(Music music) {
  return music.context_data != 0;
}

RAYLIB_EXPORT void UnloadMusicStream(Music music) { (void)music; }

RAYLIB_EXPORT void PlayMusicStream(Music music) { (void)music; }

RAYLIB_EXPORT bool IsMusicStreamPlaying(Music music) {
  return IsMusicValid(music);
}

RAYLIB_EXPORT void UpdateMusicStream(Music music) { (void)music; }

RAYLIB_EXPORT void StopMusicStream(Music music) { (void)music; }

RAYLIB_EXPORT void SetMusicVolume(Music music, float volume) {
  (void)music;
  (void)volume;
}

RAYLIB_EXPORT void SetMusicPitch(Music music, float pitch) {
  (void)music;
  (void)pitch;
}

RAYLIB_EXPORT void SetMusicPan(Music music, float pan) {
  (void)music;
  (void)pan;
}

RAYLIB_EXPORT bool IsKeyDown(int key) { return key == 263; }

RAYLIB_EXPORT bool IsKeyPressed(int key) { return key == 256; }

RAYLIB_EXPORT int GetKeyPressed(void) { return 65; }

RAYLIB_EXPORT int GetMouseX(void) { return 123; }

RAYLIB_EXPORT int GetMouseY(void) { return 234; }

RAYLIB_EXPORT void SetMousePosition(int x, int y) {
  (void)x;
  (void)y;
}

RAYLIB_EXPORT float GetMouseWheelMove(void) { return 1.5f; }

RAYLIB_EXPORT bool IsMouseButtonDown(int button) { return button == 0; }

RAYLIB_EXPORT bool IsMouseButtonPressed(int button) { return button == 1; }

RAYLIB_EXPORT void ShowCursor(void) { cursor_hidden = false; }
RAYLIB_EXPORT void HideCursor(void) { cursor_hidden = true; }
RAYLIB_EXPORT bool IsCursorHidden(void) { return cursor_hidden; }
RAYLIB_EXPORT void EnableCursor(void) { cursor_hidden = false; }
RAYLIB_EXPORT void DisableCursor(void) { cursor_hidden = true; }
