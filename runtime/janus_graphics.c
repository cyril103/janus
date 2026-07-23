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
  uint32_t id;
  int width;
  int height;
  int mipmaps;
  int format;
} JanusRaylibTexture;

typedef struct {
  void (*InitWindow)(int, int, const char *);
  bool (*IsWindowReady)(void);
  bool (*WindowShouldClose)(void);
  void (*CloseWindow)(void);
  void (*SetTargetFPS)(int);
  void (*BeginDrawing)(void);
  void (*EndDrawing)(void);
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
  bool (*IsKeyDown)(int);
  bool (*IsKeyPressed)(int);
  int (*GetMouseX)(void);
  int (*GetMouseY)(void);
  bool (*IsMouseButtonDown)(int);
  bool (*IsMouseButtonPressed)(int);
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
  JANUS_LOAD_GRAPHICS_SYMBOL(CloseWindow);
  JANUS_LOAD_GRAPHICS_SYMBOL(SetTargetFPS);
  JANUS_LOAD_GRAPHICS_SYMBOL(BeginDrawing);
  JANUS_LOAD_GRAPHICS_SYMBOL(EndDrawing);
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
  JANUS_LOAD_GRAPHICS_SYMBOL(IsKeyDown);
  JANUS_LOAD_GRAPHICS_SYMBOL(IsKeyPressed);
  JANUS_LOAD_GRAPHICS_SYMBOL(GetMouseX);
  JANUS_LOAD_GRAPHICS_SYMBOL(GetMouseY);
  JANUS_LOAD_GRAPHICS_SYMBOL(IsMouseButtonDown);
  JANUS_LOAD_GRAPHICS_SYMBOL(IsMouseButtonPressed);

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

bool janus_graphics_is_key_down(int key) {
  return graphics_loaded && graphics_api.IsKeyDown(key);
}

bool janus_graphics_is_key_pressed(int key) {
  return graphics_loaded && graphics_api.IsKeyPressed(key);
}

int janus_graphics_mouse_x(void) {
  return graphics_loaded ? graphics_api.GetMouseX() : 0;
}

int janus_graphics_mouse_y(void) {
  return graphics_loaded ? graphics_api.GetMouseY() : 0;
}

bool janus_graphics_is_mouse_button_down(int button) {
  return graphics_loaded && graphics_api.IsMouseButtonDown(button);
}

bool janus_graphics_is_mouse_button_pressed(int button) {
  return graphics_loaded && graphics_api.IsMouseButtonPressed(button);
}
