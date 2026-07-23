#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

typedef struct {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
  uint8_t alpha;
} JanusRaylibColor;

typedef struct {
  float x;
  float y;
} JanusRaylibVector2;

typedef struct {
  float x;
  float y;
  float width;
  float height;
} JanusRaylibRectangle;

typedef struct {
  JanusRaylibVector2 offset;
  JanusRaylibVector2 target;
  float rotation;
  float zoom;
} JanusRaylibCamera2D;

typedef struct {
  uint32_t id;
  int width;
  int height;
  int mipmaps;
  int format;
} JanusRaylibTexture;

typedef struct {
  void *buffer;
  void *processor;
  uint32_t sample_rate;
  uint32_t sample_size;
  uint32_t channels;
} JanusRaylibAudioStream;

typedef struct {
  JanusRaylibAudioStream stream;
  uint32_t frame_count;
} JanusRaylibSound;

typedef struct {
  JanusRaylibAudioStream stream;
  uint32_t frame_count;
  bool looping;
  int context_type;
  void *context_data;
} JanusRaylibMusic;

typedef struct {
  void (*InitWindow)(int, int, const char *);
  bool (*IsWindowReady)(void);
  bool (*WindowShouldClose)(void);
  bool (*IsWindowFullscreen)(void);
  bool (*IsWindowHidden)(void);
  bool (*IsWindowMinimized)(void);
  bool (*IsWindowMaximized)(void);
  bool (*IsWindowFocused)(void);
  bool (*IsWindowResized)(void);
  void (*CloseWindow)(void);
  void (*ToggleFullscreen)(void);
  void (*MaximizeWindow)(void);
  void (*MinimizeWindow)(void);
  void (*RestoreWindow)(void);
  void (*SetWindowTitle)(const char *);
  void (*SetWindowPosition)(int, int);
  void (*SetWindowSize)(int, int);
  void (*SetWindowOpacity)(float);
  int (*GetScreenWidth)(void);
  int (*GetScreenHeight)(void);
  void (*SetTargetFPS)(int);
  void (*BeginDrawing)(void);
  void (*EndDrawing)(void);
  void (*BeginMode2D)(JanusRaylibCamera2D);
  void (*EndMode2D)(void);
  JanusRaylibVector2 (*GetScreenToWorld2D)(JanusRaylibVector2,
                                           JanusRaylibCamera2D);
  JanusRaylibVector2 (*GetWorldToScreen2D)(JanusRaylibVector2,
                                           JanusRaylibCamera2D);
  void (*ClearBackground)(JanusRaylibColor);
  void (*DrawPixel)(int, int, JanusRaylibColor);
  void (*DrawLine)(int, int, int, int, JanusRaylibColor);
  void (*DrawCircle)(int, int, float, JanusRaylibColor);
  void (*DrawRectangle)(int, int, int, int, JanusRaylibColor);
  void (*DrawText)(const char *, int, int, int, JanusRaylibColor);
  JanusRaylibTexture (*LoadTexture)(const char *);
  bool (*IsTextureValid)(JanusRaylibTexture);
  void (*UnloadTexture)(JanusRaylibTexture);
  void (*DrawTexture)(JanusRaylibTexture, int, int, JanusRaylibColor);
  void (*DrawTexturePro)(JanusRaylibTexture, JanusRaylibRectangle,
                         JanusRaylibRectangle, JanusRaylibVector2, float,
                         JanusRaylibColor);
  void (*SetTextureFilter)(JanusRaylibTexture, int);
  void (*InitAudioDevice)(void);
  void (*CloseAudioDevice)(void);
  bool (*IsAudioDeviceReady)(void);
  void (*SetMasterVolume)(float);
  JanusRaylibSound (*LoadSound)(const char *);
  bool (*IsSoundValid)(JanusRaylibSound);
  void (*UnloadSound)(JanusRaylibSound);
  void (*PlaySound)(JanusRaylibSound);
  void (*StopSound)(JanusRaylibSound);
  bool (*IsSoundPlaying)(JanusRaylibSound);
  void (*SetSoundVolume)(JanusRaylibSound, float);
  void (*SetSoundPitch)(JanusRaylibSound, float);
  void (*SetSoundPan)(JanusRaylibSound, float);
  JanusRaylibMusic (*LoadMusicStream)(const char *);
  bool (*IsMusicValid)(JanusRaylibMusic);
  void (*UnloadMusicStream)(JanusRaylibMusic);
  void (*PlayMusicStream)(JanusRaylibMusic);
  bool (*IsMusicStreamPlaying)(JanusRaylibMusic);
  void (*UpdateMusicStream)(JanusRaylibMusic);
  void (*StopMusicStream)(JanusRaylibMusic);
  void (*SetMusicVolume)(JanusRaylibMusic, float);
  void (*SetMusicPitch)(JanusRaylibMusic, float);
  void (*SetMusicPan)(JanusRaylibMusic, float);
  bool (*IsKeyDown)(int);
  bool (*IsKeyPressed)(int);
  int (*GetKeyPressed)(void);
  int (*GetMouseX)(void);
  int (*GetMouseY)(void);
  void (*SetMousePosition)(int, int);
  float (*GetMouseWheelMove)(void);
  bool (*IsMouseButtonDown)(int);
  bool (*IsMouseButtonPressed)(int);
  void (*ShowCursor)(void);
  void (*HideCursor)(void);
  bool (*IsCursorHidden)(void);
  void (*EnableCursor)(void);
  void (*DisableCursor)(void);
} JanusGraphicsApi;

static JanusGraphicsApi graphics_api;
static bool graphics_loaded;
static const char *graphics_error = "raylib has not been loaded";

#ifdef _WIN32
static HMODULE graphics_library;
#else
static void *graphics_library;
#endif

static void close_graphics_library(void) {
  if (graphics_library == NULL)
    return;
#ifdef _WIN32
  (void)FreeLibrary(graphics_library);
#else
  (void)dlclose(graphics_library);
#endif
  graphics_library = NULL;
}

static bool load_graphics_symbol(void *target, size_t target_size,
                                 const char *name) {
#ifdef _WIN32
  FARPROC symbol = GetProcAddress(graphics_library, name);
#else
  void *symbol = dlsym(graphics_library, name);
#endif
  if (symbol == NULL || target_size != sizeof(symbol))
    return false;
  memcpy(target, &symbol, target_size);
  return true;
}

static bool open_graphics_library(void) {
  const char *configured = getenv("JANUS_RAYLIB_PATH");
  if (configured != NULL && configured[0] != '\0') {
#ifdef _WIN32
    graphics_library = LoadLibraryA(configured);
#else
    graphics_library = dlopen(configured, RTLD_NOW | RTLD_LOCAL);
#endif
    if (graphics_library != NULL)
      return true;
    graphics_error = "could not load JANUS_RAYLIB_PATH";
    return false;
  }

#ifdef _WIN32
  const char *candidates[] = {"raylib.dll", "libraylib.dll"};
#elif defined(__APPLE__)
  const char *candidates[] = {"libraylib.6.0.dylib", "libraylib.600.dylib",
                              "libraylib.dylib"};
#else
  const char *candidates[] = {"libraylib.so.600", "libraylib.so.6.0",
                              "libraylib.so"};
#endif
  for (size_t index = 0; index < sizeof(candidates) / sizeof(candidates[0]);
       ++index) {
#ifdef _WIN32
    graphics_library = LoadLibraryA(candidates[index]);
#else
    graphics_library = dlopen(candidates[index], RTLD_NOW | RTLD_LOCAL);
#endif
    if (graphics_library != NULL)
      return true;
  }
  graphics_error =
      "raylib 6 shared library not found; set JANUS_RAYLIB_PATH";
  return false;
}

static bool load_graphics_api(void) {
  if (graphics_loaded)
    return true;
  if (!open_graphics_library())
    return false;

#define JANUS_LOAD_GRAPHICS_SYMBOL(name)                                      \
  do {                                                                         \
    if (!load_graphics_symbol(&graphics_api.name, sizeof(graphics_api.name),   \
                              #name)) {                                         \
      graphics_error = "raylib is missing the required symbol " #name;         \
      close_graphics_library();                                                 \
      memset(&graphics_api, 0, sizeof(graphics_api));                           \
      return false;                                                             \
    }                                                                           \
  } while (false)

  JANUS_LOAD_GRAPHICS_SYMBOL(InitWindow);
  JANUS_LOAD_GRAPHICS_SYMBOL(IsWindowReady);
  JANUS_LOAD_GRAPHICS_SYMBOL(WindowShouldClose);
  JANUS_LOAD_GRAPHICS_SYMBOL(IsWindowFullscreen);
  JANUS_LOAD_GRAPHICS_SYMBOL(IsWindowHidden);
  JANUS_LOAD_GRAPHICS_SYMBOL(IsWindowMinimized);
  JANUS_LOAD_GRAPHICS_SYMBOL(IsWindowMaximized);
  JANUS_LOAD_GRAPHICS_SYMBOL(IsWindowFocused);
  JANUS_LOAD_GRAPHICS_SYMBOL(IsWindowResized);
  JANUS_LOAD_GRAPHICS_SYMBOL(CloseWindow);
  JANUS_LOAD_GRAPHICS_SYMBOL(ToggleFullscreen);
  JANUS_LOAD_GRAPHICS_SYMBOL(MaximizeWindow);
  JANUS_LOAD_GRAPHICS_SYMBOL(MinimizeWindow);
  JANUS_LOAD_GRAPHICS_SYMBOL(RestoreWindow);
  JANUS_LOAD_GRAPHICS_SYMBOL(SetWindowTitle);
  JANUS_LOAD_GRAPHICS_SYMBOL(SetWindowPosition);
  JANUS_LOAD_GRAPHICS_SYMBOL(SetWindowSize);
  JANUS_LOAD_GRAPHICS_SYMBOL(SetWindowOpacity);
  JANUS_LOAD_GRAPHICS_SYMBOL(GetScreenWidth);
  JANUS_LOAD_GRAPHICS_SYMBOL(GetScreenHeight);
  JANUS_LOAD_GRAPHICS_SYMBOL(SetTargetFPS);
  JANUS_LOAD_GRAPHICS_SYMBOL(BeginDrawing);
  JANUS_LOAD_GRAPHICS_SYMBOL(EndDrawing);
  JANUS_LOAD_GRAPHICS_SYMBOL(BeginMode2D);
  JANUS_LOAD_GRAPHICS_SYMBOL(EndMode2D);
  JANUS_LOAD_GRAPHICS_SYMBOL(GetScreenToWorld2D);
  JANUS_LOAD_GRAPHICS_SYMBOL(GetWorldToScreen2D);
  JANUS_LOAD_GRAPHICS_SYMBOL(ClearBackground);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawPixel);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawLine);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawCircle);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawRectangle);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawText);
  JANUS_LOAD_GRAPHICS_SYMBOL(LoadTexture);
  JANUS_LOAD_GRAPHICS_SYMBOL(IsTextureValid);
  JANUS_LOAD_GRAPHICS_SYMBOL(UnloadTexture);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawTexture);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawTexturePro);
  JANUS_LOAD_GRAPHICS_SYMBOL(SetTextureFilter);
  JANUS_LOAD_GRAPHICS_SYMBOL(InitAudioDevice);
  JANUS_LOAD_GRAPHICS_SYMBOL(CloseAudioDevice);
  JANUS_LOAD_GRAPHICS_SYMBOL(IsAudioDeviceReady);
  JANUS_LOAD_GRAPHICS_SYMBOL(SetMasterVolume);
  JANUS_LOAD_GRAPHICS_SYMBOL(LoadSound);
  JANUS_LOAD_GRAPHICS_SYMBOL(IsSoundValid);
  JANUS_LOAD_GRAPHICS_SYMBOL(UnloadSound);
  JANUS_LOAD_GRAPHICS_SYMBOL(PlaySound);
  JANUS_LOAD_GRAPHICS_SYMBOL(StopSound);
  JANUS_LOAD_GRAPHICS_SYMBOL(IsSoundPlaying);
  JANUS_LOAD_GRAPHICS_SYMBOL(SetSoundVolume);
  JANUS_LOAD_GRAPHICS_SYMBOL(SetSoundPitch);
  JANUS_LOAD_GRAPHICS_SYMBOL(SetSoundPan);
  JANUS_LOAD_GRAPHICS_SYMBOL(LoadMusicStream);
  JANUS_LOAD_GRAPHICS_SYMBOL(IsMusicValid);
  JANUS_LOAD_GRAPHICS_SYMBOL(UnloadMusicStream);
  JANUS_LOAD_GRAPHICS_SYMBOL(PlayMusicStream);
  JANUS_LOAD_GRAPHICS_SYMBOL(IsMusicStreamPlaying);
  JANUS_LOAD_GRAPHICS_SYMBOL(UpdateMusicStream);
  JANUS_LOAD_GRAPHICS_SYMBOL(StopMusicStream);
  JANUS_LOAD_GRAPHICS_SYMBOL(SetMusicVolume);
  JANUS_LOAD_GRAPHICS_SYMBOL(SetMusicPitch);
  JANUS_LOAD_GRAPHICS_SYMBOL(SetMusicPan);
  JANUS_LOAD_GRAPHICS_SYMBOL(IsKeyDown);
  JANUS_LOAD_GRAPHICS_SYMBOL(IsKeyPressed);
  JANUS_LOAD_GRAPHICS_SYMBOL(GetKeyPressed);
  JANUS_LOAD_GRAPHICS_SYMBOL(GetMouseX);
  JANUS_LOAD_GRAPHICS_SYMBOL(GetMouseY);
  JANUS_LOAD_GRAPHICS_SYMBOL(SetMousePosition);
  JANUS_LOAD_GRAPHICS_SYMBOL(GetMouseWheelMove);
  JANUS_LOAD_GRAPHICS_SYMBOL(IsMouseButtonDown);
  JANUS_LOAD_GRAPHICS_SYMBOL(IsMouseButtonPressed);
  JANUS_LOAD_GRAPHICS_SYMBOL(ShowCursor);
  JANUS_LOAD_GRAPHICS_SYMBOL(HideCursor);
  JANUS_LOAD_GRAPHICS_SYMBOL(IsCursorHidden);
  JANUS_LOAD_GRAPHICS_SYMBOL(EnableCursor);
  JANUS_LOAD_GRAPHICS_SYMBOL(DisableCursor);

#undef JANUS_LOAD_GRAPHICS_SYMBOL

  graphics_loaded = true;
  graphics_error = "";
  return true;
}

static JanusRaylibColor unpack_color(uint32_t color) {
  JanusRaylibColor result;
  result.red = (uint8_t)(color >> 24);
  result.green = (uint8_t)(color >> 16);
  result.blue = (uint8_t)(color >> 8);
  result.alpha = (uint8_t)color;
  return result;
}

uint32_t janus_graphics_rgba(uint8_t red, uint8_t green, uint8_t blue,
                             uint8_t alpha) {
  return ((uint32_t)red << 24) | ((uint32_t)green << 16) |
         ((uint32_t)blue << 8) | (uint32_t)alpha;
}

const void *janus_graphics_last_error(void) { return graphics_error; }

bool janus_graphics_available(void) { return load_graphics_api(); }

bool janus_graphics_init_window(int width, int height, const void *title) {
  if (!load_graphics_api() || title == NULL || width <= 0 || height <= 0)
    return false;
  graphics_api.InitWindow(width, height, (const char *)title);
  if (!graphics_api.IsWindowReady()) {
    graphics_error = "raylib could not create the window";
    return false;
  }
  return true;
}

bool janus_graphics_window_should_close(void) {
  return !graphics_loaded || graphics_api.WindowShouldClose();
}

void janus_graphics_close_window(void) {
  if (graphics_loaded && graphics_api.IsWindowReady())
    graphics_api.CloseWindow();
}

bool janus_graphics_is_window_fullscreen(void) {
  return graphics_loaded && graphics_api.IsWindowFullscreen();
}

bool janus_graphics_is_window_hidden(void) {
  return graphics_loaded && graphics_api.IsWindowHidden();
}

bool janus_graphics_is_window_minimized(void) {
  return graphics_loaded && graphics_api.IsWindowMinimized();
}

bool janus_graphics_is_window_maximized(void) {
  return graphics_loaded && graphics_api.IsWindowMaximized();
}

bool janus_graphics_is_window_focused(void) {
  return graphics_loaded && graphics_api.IsWindowFocused();
}

bool janus_graphics_is_window_resized(void) {
  return graphics_loaded && graphics_api.IsWindowResized();
}

void janus_graphics_toggle_fullscreen(void) {
  if (graphics_loaded)
    graphics_api.ToggleFullscreen();
}

void janus_graphics_maximize_window(void) {
  if (graphics_loaded)
    graphics_api.MaximizeWindow();
}

void janus_graphics_minimize_window(void) {
  if (graphics_loaded)
    graphics_api.MinimizeWindow();
}

void janus_graphics_restore_window(void) {
  if (graphics_loaded)
    graphics_api.RestoreWindow();
}

void janus_graphics_set_window_title(const void *title) {
  if (graphics_loaded && title != NULL)
    graphics_api.SetWindowTitle((const char *)title);
}

void janus_graphics_set_window_position(int x, int y) {
  if (graphics_loaded)
    graphics_api.SetWindowPosition(x, y);
}

void janus_graphics_set_window_size(int width, int height) {
  if (graphics_loaded && width > 0 && height > 0)
    graphics_api.SetWindowSize(width, height);
}

void janus_graphics_set_window_opacity(float opacity) {
  if (graphics_loaded)
    graphics_api.SetWindowOpacity(opacity);
}

int janus_graphics_screen_width(void) {
  return graphics_loaded ? graphics_api.GetScreenWidth() : 0;
}

int janus_graphics_screen_height(void) {
  return graphics_loaded ? graphics_api.GetScreenHeight() : 0;
}

void janus_graphics_set_target_fps(int frames_per_second) {
  if (graphics_loaded)
    graphics_api.SetTargetFPS(frames_per_second);
}

void janus_graphics_begin_drawing(void) {
  if (graphics_loaded)
    graphics_api.BeginDrawing();
}

void janus_graphics_end_drawing(void) {
  if (graphics_loaded)
    graphics_api.EndDrawing();
}

static JanusRaylibCamera2D make_camera(float offset_x, float offset_y,
                                       float target_x, float target_y,
                                       float rotation, float zoom) {
  JanusRaylibCamera2D camera = {{offset_x, offset_y},
                                {target_x, target_y},
                                rotation,
                                zoom};
  return camera;
}

void janus_graphics_begin_camera(float offset_x, float offset_y, float target_x,
                                 float target_y, float rotation, float zoom) {
  if (graphics_loaded)
    graphics_api.BeginMode2D(make_camera(offset_x, offset_y, target_x, target_y,
                                         rotation, zoom));
}

void janus_graphics_end_camera(void) {
  if (graphics_loaded)
    graphics_api.EndMode2D();
}

float janus_graphics_screen_to_world_x(float x, float y, float offset_x,
                                       float offset_y, float target_x,
                                       float target_y, float rotation,
                                       float zoom) {
  if (!graphics_loaded)
    return 0.0f;
  JanusRaylibVector2 result = graphics_api.GetScreenToWorld2D(
      (JanusRaylibVector2){x, y},
      make_camera(offset_x, offset_y, target_x, target_y, rotation, zoom));
  return result.x;
}

float janus_graphics_screen_to_world_y(float x, float y, float offset_x,
                                       float offset_y, float target_x,
                                       float target_y, float rotation,
                                       float zoom) {
  if (!graphics_loaded)
    return 0.0f;
  JanusRaylibVector2 result = graphics_api.GetScreenToWorld2D(
      (JanusRaylibVector2){x, y},
      make_camera(offset_x, offset_y, target_x, target_y, rotation, zoom));
  return result.y;
}

float janus_graphics_world_to_screen_x(float x, float y, float offset_x,
                                       float offset_y, float target_x,
                                       float target_y, float rotation,
                                       float zoom) {
  if (!graphics_loaded)
    return 0.0f;
  JanusRaylibVector2 result = graphics_api.GetWorldToScreen2D(
      (JanusRaylibVector2){x, y},
      make_camera(offset_x, offset_y, target_x, target_y, rotation, zoom));
  return result.x;
}

float janus_graphics_world_to_screen_y(float x, float y, float offset_x,
                                       float offset_y, float target_x,
                                       float target_y, float rotation,
                                       float zoom) {
  if (!graphics_loaded)
    return 0.0f;
  JanusRaylibVector2 result = graphics_api.GetWorldToScreen2D(
      (JanusRaylibVector2){x, y},
      make_camera(offset_x, offset_y, target_x, target_y, rotation, zoom));
  return result.y;
}

void janus_graphics_clear_background(uint32_t color) {
  if (graphics_loaded)
    graphics_api.ClearBackground(unpack_color(color));
}

void janus_graphics_draw_pixel(int x, int y, uint32_t color) {
  if (graphics_loaded)
    graphics_api.DrawPixel(x, y, unpack_color(color));
}

void janus_graphics_draw_line(int start_x, int start_y, int end_x, int end_y,
                              uint32_t color) {
  if (graphics_loaded)
    graphics_api.DrawLine(start_x, start_y, end_x, end_y, unpack_color(color));
}

void janus_graphics_draw_circle(int center_x, int center_y, float radius,
                                uint32_t color) {
  if (graphics_loaded)
    graphics_api.DrawCircle(center_x, center_y, radius, unpack_color(color));
}

void janus_graphics_draw_rectangle(int x, int y, int width, int height,
                                   uint32_t color) {
  if (graphics_loaded)
    graphics_api.DrawRectangle(x, y, width, height, unpack_color(color));
}

void janus_graphics_draw_text(const void *text, int x, int y, int font_size,
                              uint32_t color) {
  if (graphics_loaded && text != NULL)
    graphics_api.DrawText((const char *)text, x, y, font_size,
                          unpack_color(color));
}

void *janus_graphics_load_texture(const void *file_name) {
  if (!graphics_loaded || file_name == NULL)
    return NULL;
  JanusRaylibTexture *texture = malloc(sizeof(*texture));
  if (texture == NULL)
    return NULL;
  *texture = graphics_api.LoadTexture((const char *)file_name);
  return texture;
}

bool janus_graphics_texture_is_valid(const void *handle) {
  return graphics_loaded && handle != NULL &&
         graphics_api.IsTextureValid(*(const JanusRaylibTexture *)handle);
}

int janus_graphics_texture_width(const void *handle) {
  return handle != NULL ? ((const JanusRaylibTexture *)handle)->width : 0;
}

int janus_graphics_texture_height(const void *handle) {
  return handle != NULL ? ((const JanusRaylibTexture *)handle)->height : 0;
}

void janus_graphics_unload_texture(void *handle) {
  if (handle == NULL)
    return;
  JanusRaylibTexture texture = *(JanusRaylibTexture *)handle;
  if (graphics_loaded && graphics_api.IsTextureValid(texture))
    graphics_api.UnloadTexture(texture);
  free(handle);
}

void janus_graphics_draw_texture(const void *handle, int x, int y,
                                 uint32_t tint) {
  if (janus_graphics_texture_is_valid(handle))
    graphics_api.DrawTexture(*(const JanusRaylibTexture *)handle, x, y,
                             unpack_color(tint));
}

void janus_graphics_draw_texture_pro(
    const void *handle, float source_x, float source_y, float source_width,
    float source_height, float destination_x, float destination_y,
    float destination_width, float destination_height, float origin_x,
    float origin_y, float rotation, uint32_t tint) {
  if (!janus_graphics_texture_is_valid(handle))
    return;
  graphics_api.DrawTexturePro(
      *(const JanusRaylibTexture *)handle,
      (JanusRaylibRectangle){source_x, source_y, source_width, source_height},
      (JanusRaylibRectangle){destination_x, destination_y, destination_width,
                             destination_height},
      (JanusRaylibVector2){origin_x, origin_y}, rotation, unpack_color(tint));
}

void janus_graphics_set_texture_filter(const void *handle, int filter) {
  if (janus_graphics_texture_is_valid(handle))
    graphics_api.SetTextureFilter(*(const JanusRaylibTexture *)handle, filter);
}

bool janus_graphics_init_audio(void) {
  if (!load_graphics_api())
    return false;
  graphics_api.InitAudioDevice();
  return graphics_api.IsAudioDeviceReady();
}

void janus_graphics_close_audio(void) {
  if (graphics_loaded && graphics_api.IsAudioDeviceReady())
    graphics_api.CloseAudioDevice();
}

void janus_graphics_set_master_volume(float volume) {
  if (graphics_loaded && graphics_api.IsAudioDeviceReady())
    graphics_api.SetMasterVolume(volume);
}

void *janus_graphics_load_sound(const void *file_name) {
  if (!graphics_loaded || !graphics_api.IsAudioDeviceReady() ||
      file_name == NULL)
    return NULL;
  JanusRaylibSound *sound = malloc(sizeof(*sound));
  if (sound == NULL)
    return NULL;
  *sound = graphics_api.LoadSound((const char *)file_name);
  return sound;
}

bool janus_graphics_sound_is_valid(const void *handle) {
  return graphics_loaded && handle != NULL &&
         graphics_api.IsSoundValid(*(const JanusRaylibSound *)handle);
}

void janus_graphics_unload_sound(void *handle) {
  if (handle == NULL)
    return;
  JanusRaylibSound sound = *(JanusRaylibSound *)handle;
  if (graphics_loaded && graphics_api.IsSoundValid(sound))
    graphics_api.UnloadSound(sound);
  free(handle);
}

void janus_graphics_play_sound(const void *handle) {
  if (janus_graphics_sound_is_valid(handle))
    graphics_api.PlaySound(*(const JanusRaylibSound *)handle);
}

void janus_graphics_stop_sound(const void *handle) {
  if (janus_graphics_sound_is_valid(handle))
    graphics_api.StopSound(*(const JanusRaylibSound *)handle);
}

bool janus_graphics_sound_is_playing(const void *handle) {
  return janus_graphics_sound_is_valid(handle) &&
         graphics_api.IsSoundPlaying(*(const JanusRaylibSound *)handle);
}

void janus_graphics_set_sound_volume(const void *handle, float volume) {
  if (janus_graphics_sound_is_valid(handle))
    graphics_api.SetSoundVolume(*(const JanusRaylibSound *)handle, volume);
}

void janus_graphics_set_sound_pitch(const void *handle, float pitch) {
  if (janus_graphics_sound_is_valid(handle))
    graphics_api.SetSoundPitch(*(const JanusRaylibSound *)handle, pitch);
}

void janus_graphics_set_sound_pan(const void *handle, float pan) {
  if (janus_graphics_sound_is_valid(handle))
    graphics_api.SetSoundPan(*(const JanusRaylibSound *)handle, pan);
}

void *janus_graphics_load_music(const void *file_name) {
  if (!graphics_loaded || !graphics_api.IsAudioDeviceReady() ||
      file_name == NULL)
    return NULL;
  JanusRaylibMusic *music = malloc(sizeof(*music));
  if (music == NULL)
    return NULL;
  *music = graphics_api.LoadMusicStream((const char *)file_name);
  return music;
}

bool janus_graphics_music_is_valid(const void *handle) {
  return graphics_loaded && handle != NULL &&
         graphics_api.IsMusicValid(*(const JanusRaylibMusic *)handle);
}

void janus_graphics_unload_music(void *handle) {
  if (handle == NULL)
    return;
  JanusRaylibMusic music = *(JanusRaylibMusic *)handle;
  if (graphics_loaded && graphics_api.IsMusicValid(music))
    graphics_api.UnloadMusicStream(music);
  free(handle);
}

void janus_graphics_play_music(const void *handle) {
  if (janus_graphics_music_is_valid(handle))
    graphics_api.PlayMusicStream(*(const JanusRaylibMusic *)handle);
}

void janus_graphics_stop_music(const void *handle) {
  if (janus_graphics_music_is_valid(handle))
    graphics_api.StopMusicStream(*(const JanusRaylibMusic *)handle);
}

void janus_graphics_update_music(const void *handle) {
  if (janus_graphics_music_is_valid(handle))
    graphics_api.UpdateMusicStream(*(const JanusRaylibMusic *)handle);
}

bool janus_graphics_music_is_playing(const void *handle) {
  return janus_graphics_music_is_valid(handle) &&
         graphics_api.IsMusicStreamPlaying(*(const JanusRaylibMusic *)handle);
}

void janus_graphics_set_music_volume(const void *handle, float volume) {
  if (janus_graphics_music_is_valid(handle))
    graphics_api.SetMusicVolume(*(const JanusRaylibMusic *)handle, volume);
}

void janus_graphics_set_music_pitch(const void *handle, float pitch) {
  if (janus_graphics_music_is_valid(handle))
    graphics_api.SetMusicPitch(*(const JanusRaylibMusic *)handle, pitch);
}

void janus_graphics_set_music_pan(const void *handle, float pan) {
  if (janus_graphics_music_is_valid(handle))
    graphics_api.SetMusicPan(*(const JanusRaylibMusic *)handle, pan);
}

bool janus_graphics_is_key_down(int key) {
  return graphics_loaded && graphics_api.IsKeyDown(key);
}

bool janus_graphics_is_key_pressed(int key) {
  return graphics_loaded && graphics_api.IsKeyPressed(key);
}

int janus_graphics_key_pressed(void) {
  return graphics_loaded ? graphics_api.GetKeyPressed() : 0;
}

int janus_graphics_mouse_x(void) {
  return graphics_loaded ? graphics_api.GetMouseX() : 0;
}

int janus_graphics_mouse_y(void) {
  return graphics_loaded ? graphics_api.GetMouseY() : 0;
}

void janus_graphics_set_mouse_position(int x, int y) {
  if (graphics_loaded)
    graphics_api.SetMousePosition(x, y);
}

float janus_graphics_mouse_wheel_move(void) {
  return graphics_loaded ? graphics_api.GetMouseWheelMove() : 0.0f;
}

bool janus_graphics_is_mouse_button_down(int button) {
  return graphics_loaded && graphics_api.IsMouseButtonDown(button);
}

bool janus_graphics_is_mouse_button_pressed(int button) {
  return graphics_loaded && graphics_api.IsMouseButtonPressed(button);
}

void janus_graphics_show_cursor(void) {
  if (graphics_loaded)
    graphics_api.ShowCursor();
}

void janus_graphics_hide_cursor(void) {
  if (graphics_loaded)
    graphics_api.HideCursor();
}

bool janus_graphics_is_cursor_hidden(void) {
  return graphics_loaded && graphics_api.IsCursorHidden();
}

void janus_graphics_enable_cursor(void) {
  if (graphics_loaded)
    graphics_api.EnableCursor();
}

void janus_graphics_disable_cursor(void) {
  if (graphics_loaded)
    graphics_api.DisableCursor();
}
