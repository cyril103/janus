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
  JanusRaylibRectangle source;
  int left;
  int top;
  int right;
  int bottom;
  int layout;
} JanusRaylibNPatchInfo;

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
  void *data;
  int width;
  int height;
  int mipmaps;
  int format;
} JanusRaylibImage;

typedef struct {
  uint32_t id;
  JanusRaylibTexture texture;
  JanusRaylibTexture depth;
} JanusRaylibRenderTexture;

typedef struct {
  uint32_t id;
  int *locations;
} JanusRaylibShader;

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
  int base_size;
  int glyph_count;
  int glyph_padding;
  JanusRaylibTexture texture;
  JanusRaylibRectangle *recs;
  void *glyphs;
} JanusRaylibFont;

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
  void (*BeginBlendMode)(int);
  void (*EndBlendMode)(void);
  void (*BeginScissorMode)(int, int, int, int);
  void (*EndScissorMode)(void);
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
  void (*DrawLineEx)(JanusRaylibVector2, JanusRaylibVector2, float,
                     JanusRaylibColor);
  void (*DrawLineBezier)(JanusRaylibVector2, JanusRaylibVector2, float,
                         JanusRaylibColor);
  void (*DrawLineDashed)(JanusRaylibVector2, JanusRaylibVector2, int, int,
                         JanusRaylibColor);
  void (*DrawLineStrip)(const JanusRaylibVector2 *, int, JanusRaylibColor);
  void (*DrawCircle)(int, int, float, JanusRaylibColor);
  void (*DrawCircleGradient)(JanusRaylibVector2, float, JanusRaylibColor,
                             JanusRaylibColor);
  void (*DrawCircleSector)(JanusRaylibVector2, float, float, float, int,
                           JanusRaylibColor);
  void (*DrawCircleSectorLines)(JanusRaylibVector2, float, float, float, int,
                                JanusRaylibColor);
  void (*DrawCircleLines)(int, int, float, JanusRaylibColor);
  void (*DrawEllipse)(int, int, float, float, JanusRaylibColor);
  void (*DrawEllipseLines)(int, int, float, float, JanusRaylibColor);
  void (*DrawRing)(JanusRaylibVector2, float, float, float, float, int,
                   JanusRaylibColor);
  void (*DrawRingLines)(JanusRaylibVector2, float, float, float, float, int,
                        JanusRaylibColor);
  void (*DrawRectangle)(int, int, int, int, JanusRaylibColor);
  void (*DrawRectanglePro)(JanusRaylibRectangle, JanusRaylibVector2, float,
                           JanusRaylibColor);
  void (*DrawRectangleGradientV)(int, int, int, int, JanusRaylibColor,
                                 JanusRaylibColor);
  void (*DrawRectangleGradientH)(int, int, int, int, JanusRaylibColor,
                                 JanusRaylibColor);
  void (*DrawRectangleGradientEx)(JanusRaylibRectangle, JanusRaylibColor,
                                  JanusRaylibColor, JanusRaylibColor,
                                  JanusRaylibColor);
  void (*DrawRectangleLinesEx)(JanusRaylibRectangle, float, JanusRaylibColor);
  void (*DrawRectangleRounded)(JanusRaylibRectangle, float, int,
                               JanusRaylibColor);
  void (*DrawRectangleRoundedLinesEx)(JanusRaylibRectangle, float, int, float,
                                      JanusRaylibColor);
  void (*DrawTriangle)(JanusRaylibVector2, JanusRaylibVector2,
                       JanusRaylibVector2, JanusRaylibColor);
  void (*DrawTriangleLines)(JanusRaylibVector2, JanusRaylibVector2,
                            JanusRaylibVector2, JanusRaylibColor);
  void (*DrawTriangleFan)(const JanusRaylibVector2 *, int, JanusRaylibColor);
  void (*DrawTriangleStrip)(const JanusRaylibVector2 *, int, JanusRaylibColor);
  void (*DrawPoly)(JanusRaylibVector2, int, float, float, JanusRaylibColor);
  void (*DrawPolyLinesEx)(JanusRaylibVector2, int, float, float, float,
                          JanusRaylibColor);
  void (*DrawSplineLinear)(const JanusRaylibVector2 *, int, float,
                           JanusRaylibColor);
  void (*DrawSplineBasis)(const JanusRaylibVector2 *, int, float,
                          JanusRaylibColor);
  void (*DrawSplineCatmullRom)(const JanusRaylibVector2 *, int, float,
                               JanusRaylibColor);
  void (*DrawSplineBezierQuadratic)(const JanusRaylibVector2 *, int, float,
                                    JanusRaylibColor);
  void (*DrawSplineBezierCubic)(const JanusRaylibVector2 *, int, float,
                                JanusRaylibColor);
  void (*DrawSplineSegmentLinear)(JanusRaylibVector2, JanusRaylibVector2, float,
                                  JanusRaylibColor);
  void (*DrawSplineSegmentBasis)(JanusRaylibVector2, JanusRaylibVector2,
                                 JanusRaylibVector2, JanusRaylibVector2, float,
                                 JanusRaylibColor);
  void (*DrawSplineSegmentCatmullRom)(JanusRaylibVector2, JanusRaylibVector2,
                                      JanusRaylibVector2, JanusRaylibVector2,
                                      float, JanusRaylibColor);
  void (*DrawSplineSegmentBezierQuadratic)(JanusRaylibVector2,
                                           JanusRaylibVector2,
                                           JanusRaylibVector2, float,
                                           JanusRaylibColor);
  void (*DrawSplineSegmentBezierCubic)(JanusRaylibVector2, JanusRaylibVector2,
                                       JanusRaylibVector2, JanusRaylibVector2,
                                       float, JanusRaylibColor);
  JanusRaylibVector2 (*GetSplinePointLinear)(JanusRaylibVector2,
                                             JanusRaylibVector2, float);
  JanusRaylibVector2 (*GetSplinePointBasis)(JanusRaylibVector2,
                                            JanusRaylibVector2,
                                            JanusRaylibVector2,
                                            JanusRaylibVector2, float);
  JanusRaylibVector2 (*GetSplinePointCatmullRom)(JanusRaylibVector2,
                                                 JanusRaylibVector2,
                                                 JanusRaylibVector2,
                                                 JanusRaylibVector2, float);
  JanusRaylibVector2 (*GetSplinePointBezierQuad)(JanusRaylibVector2,
                                                 JanusRaylibVector2,
                                                 JanusRaylibVector2, float);
  JanusRaylibVector2 (*GetSplinePointBezierCubic)(JanusRaylibVector2,
                                                  JanusRaylibVector2,
                                                  JanusRaylibVector2,
                                                  JanusRaylibVector2, float);
  bool (*CheckCollisionRecs)(JanusRaylibRectangle, JanusRaylibRectangle);
  bool (*CheckCollisionCircles)(JanusRaylibVector2, float, JanusRaylibVector2,
                                float);
  bool (*CheckCollisionCircleRec)(JanusRaylibVector2, float,
                                  JanusRaylibRectangle);
  bool (*CheckCollisionCircleLine)(JanusRaylibVector2, float,
                                   JanusRaylibVector2, JanusRaylibVector2);
  bool (*CheckCollisionPointRec)(JanusRaylibVector2, JanusRaylibRectangle);
  bool (*CheckCollisionPointCircle)(JanusRaylibVector2, JanusRaylibVector2,
                                    float);
  bool (*CheckCollisionPointTriangle)(JanusRaylibVector2, JanusRaylibVector2,
                                      JanusRaylibVector2, JanusRaylibVector2);
  bool (*CheckCollisionPointLine)(JanusRaylibVector2, JanusRaylibVector2,
                                  JanusRaylibVector2, int);
  bool (*CheckCollisionPointPoly)(JanusRaylibVector2,
                                  const JanusRaylibVector2 *, int);
  bool (*CheckCollisionLines)(JanusRaylibVector2, JanusRaylibVector2,
                              JanusRaylibVector2, JanusRaylibVector2,
                              JanusRaylibVector2 *);
  JanusRaylibRectangle (*GetCollisionRec)(JanusRaylibRectangle,
                                          JanusRaylibRectangle);
  void (*DrawText)(const char *, int, int, int, JanusRaylibColor);
  JanusRaylibFont (*LoadFontEx)(const char *, int, int *, int);
  int *(*LoadCodepoints)(const char *, int *);
  void (*UnloadCodepoints)(int *);
  bool (*IsFontValid)(JanusRaylibFont);
  void (*UnloadFont)(JanusRaylibFont);
  void (*DrawTextEx)(JanusRaylibFont, const char *, JanusRaylibVector2, float,
                     float, JanusRaylibColor);
  JanusRaylibVector2 (*MeasureTextEx)(JanusRaylibFont, const char *, float,
                                      float);
  JanusRaylibImage (*LoadImage)(const char *);
  JanusRaylibImage (*LoadImageRaw)(const char *, int, int, int, int);
  JanusRaylibImage (*LoadImageFromMemory)(const char *, const unsigned char *,
                                          int);
  JanusRaylibImage (*LoadImageFromTexture)(JanusRaylibTexture);
  JanusRaylibImage (*LoadImageFromScreen)(void);
  bool (*IsImageValid)(JanusRaylibImage);
  void (*UnloadImage)(JanusRaylibImage);
  bool (*ExportImage)(JanusRaylibImage, const char *);
  bool (*ExportImageAsCode)(JanusRaylibImage, const char *);
  JanusRaylibImage (*GenImageColor)(int, int, JanusRaylibColor);
  JanusRaylibImage (*GenImageGradientLinear)(int, int, int, JanusRaylibColor,
                                             JanusRaylibColor);
  JanusRaylibImage (*GenImageGradientRadial)(int, int, float, JanusRaylibColor,
                                             JanusRaylibColor);
  JanusRaylibImage (*GenImageGradientSquare)(int, int, float, JanusRaylibColor,
                                             JanusRaylibColor);
  JanusRaylibImage (*GenImageChecked)(int, int, int, int, JanusRaylibColor,
                                      JanusRaylibColor);
  JanusRaylibImage (*GenImageWhiteNoise)(int, int, float);
  JanusRaylibImage (*GenImagePerlinNoise)(int, int, int, int, float);
  JanusRaylibImage (*GenImageCellular)(int, int, int);
  JanusRaylibImage (*GenImageText)(int, int, const char *);
  JanusRaylibImage (*ImageCopy)(JanusRaylibImage);
  JanusRaylibImage (*ImageFromImage)(JanusRaylibImage, JanusRaylibRectangle);
  JanusRaylibImage (*ImageFromChannel)(JanusRaylibImage, int);
  JanusRaylibImage (*ImageText)(const char *, int, JanusRaylibColor);
  void (*ImageFormat)(JanusRaylibImage *, int);
  void (*ImageToPOT)(JanusRaylibImage *, JanusRaylibColor);
  void (*ImageCrop)(JanusRaylibImage *, JanusRaylibRectangle);
  void (*ImageAlphaCrop)(JanusRaylibImage *, float);
  void (*ImageAlphaClear)(JanusRaylibImage *, JanusRaylibColor, float);
  void (*ImageAlphaMask)(JanusRaylibImage *, JanusRaylibImage);
  void (*ImageAlphaPremultiply)(JanusRaylibImage *);
  void (*ImageBlurGaussian)(JanusRaylibImage *, int);
  void (*ImageKernelConvolution)(JanusRaylibImage *, const float *, int);
  void (*ImageResize)(JanusRaylibImage *, int, int);
  void (*ImageResizeNN)(JanusRaylibImage *, int, int);
  void (*ImageResizeCanvas)(JanusRaylibImage *, int, int, int, int,
                            JanusRaylibColor);
  void (*ImageMipmaps)(JanusRaylibImage *);
  void (*ImageDither)(JanusRaylibImage *, int, int, int, int);
  void (*ImageFlipVertical)(JanusRaylibImage *);
  void (*ImageFlipHorizontal)(JanusRaylibImage *);
  void (*ImageRotate)(JanusRaylibImage *, int);
  void (*ImageRotateCW)(JanusRaylibImage *);
  void (*ImageRotateCCW)(JanusRaylibImage *);
  void (*ImageColorTint)(JanusRaylibImage *, JanusRaylibColor);
  void (*ImageColorInvert)(JanusRaylibImage *);
  void (*ImageColorGrayscale)(JanusRaylibImage *);
  void (*ImageColorContrast)(JanusRaylibImage *, float);
  void (*ImageColorBrightness)(JanusRaylibImage *, int);
  void (*ImageColorReplace)(JanusRaylibImage *, JanusRaylibColor,
                            JanusRaylibColor);
  JanusRaylibRectangle (*GetImageAlphaBorder)(JanusRaylibImage, float);
  JanusRaylibColor (*GetImageColor)(JanusRaylibImage, int, int);
  void (*ImageClearBackground)(JanusRaylibImage *, JanusRaylibColor);
  void (*ImageDrawPixel)(JanusRaylibImage *, int, int, JanusRaylibColor);
  void (*ImageDrawLineEx)(JanusRaylibImage *, JanusRaylibVector2,
                          JanusRaylibVector2, int, JanusRaylibColor);
  void (*ImageDrawCircle)(JanusRaylibImage *, int, int, int, JanusRaylibColor);
  void (*ImageDrawCircleLines)(JanusRaylibImage *, int, int, int,
                               JanusRaylibColor);
  void (*ImageDrawRectangleRec)(JanusRaylibImage *, JanusRaylibRectangle,
                                JanusRaylibColor);
  void (*ImageDrawRectangleLines)(JanusRaylibImage *, JanusRaylibRectangle, int,
                                  JanusRaylibColor);
  void (*ImageDrawTriangle)(JanusRaylibImage *, JanusRaylibVector2,
                            JanusRaylibVector2, JanusRaylibVector2,
                            JanusRaylibColor);
  void (*ImageDrawTriangleEx)(JanusRaylibImage *, JanusRaylibVector2,
                              JanusRaylibVector2, JanusRaylibVector2,
                              JanusRaylibColor, JanusRaylibColor,
                              JanusRaylibColor);
  void (*ImageDrawTriangleLines)(JanusRaylibImage *, JanusRaylibVector2,
                                 JanusRaylibVector2, JanusRaylibVector2,
                                 JanusRaylibColor);
  void (*ImageDrawTriangleFan)(JanusRaylibImage *, const JanusRaylibVector2 *,
                               int, JanusRaylibColor);
  void (*ImageDrawTriangleStrip)(JanusRaylibImage *, const JanusRaylibVector2 *,
                                 int, JanusRaylibColor);
  void (*ImageDraw)(JanusRaylibImage *, JanusRaylibImage, JanusRaylibRectangle,
                    JanusRaylibRectangle, JanusRaylibColor);
  void (*ImageDrawText)(JanusRaylibImage *, const char *, int, int, int,
                        JanusRaylibColor);
  JanusRaylibTexture (*LoadTexture)(const char *);
  JanusRaylibTexture (*LoadTextureFromImage)(JanusRaylibImage);
  bool (*IsTextureValid)(JanusRaylibTexture);
  void (*UnloadTexture)(JanusRaylibTexture);
  void (*DrawTexture)(JanusRaylibTexture, int, int, JanusRaylibColor);
  void (*DrawTextureV)(JanusRaylibTexture, JanusRaylibVector2,
                       JanusRaylibColor);
  void (*DrawTextureEx)(JanusRaylibTexture, JanusRaylibVector2, float, float,
                        JanusRaylibColor);
  void (*DrawTextureRec)(JanusRaylibTexture, JanusRaylibRectangle,
                         JanusRaylibVector2, JanusRaylibColor);
  void (*DrawTexturePro)(JanusRaylibTexture, JanusRaylibRectangle,
                         JanusRaylibRectangle, JanusRaylibVector2, float,
                         JanusRaylibColor);
  void (*DrawTextureNPatch)(JanusRaylibTexture, JanusRaylibNPatchInfo,
                            JanusRaylibRectangle, JanusRaylibVector2, float,
                            JanusRaylibColor);
  void (*SetShapesTexture)(JanusRaylibTexture, JanusRaylibRectangle);
  void (*SetTextureFilter)(JanusRaylibTexture, int);
  void (*SetTextureWrap)(JanusRaylibTexture, int);
  void (*GenTextureMipmaps)(JanusRaylibTexture *);
  void (*UpdateTexture)(JanusRaylibTexture, const void *);
  void (*UpdateTextureRec)(JanusRaylibTexture, JanusRaylibRectangle,
                           const void *);
  JanusRaylibRenderTexture (*LoadRenderTexture)(int, int);
  bool (*IsRenderTextureValid)(JanusRaylibRenderTexture);
  void (*UnloadRenderTexture)(JanusRaylibRenderTexture);
  void (*BeginTextureMode)(JanusRaylibRenderTexture);
  void (*EndTextureMode)(void);
  JanusRaylibShader (*LoadShader)(const char *, const char *);
  bool (*IsShaderValid)(JanusRaylibShader);
  void (*UnloadShader)(JanusRaylibShader);
  void (*BeginShaderMode)(JanusRaylibShader);
  void (*EndShaderMode)(void);
  int (*GetShaderLocation)(JanusRaylibShader, const char *);
  void (*SetShaderValue)(JanusRaylibShader, int, const void *, int);
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
  bool (*IsGamepadAvailable)(int);
  const char *(*GetGamepadName)(int);
  bool (*IsGamepadButtonDown)(int, int);
  bool (*IsGamepadButtonPressed)(int, int);
  bool (*IsGamepadButtonReleased)(int, int);
  int (*GetGamepadButtonPressed)(void);
  int (*GetGamepadAxisCount)(int);
  float (*GetGamepadAxisMovement)(int, int);
  void (*SetGamepadVibration)(int, float, float, float);
} JanusGraphicsApi;

static JanusGraphicsApi graphics_api;
static bool graphics_loaded;
static const char *graphics_error = "raylib has not been loaded";
enum {
  JANUS_GRAPHICS_BLEND_ALPHA = 0,
  JANUS_GRAPHICS_BLEND_CUSTOM_SEPARATE = 7,
  JANUS_GRAPHICS_BLEND_STACK_CAPACITY = 32
};
static int graphics_blend_mode = JANUS_GRAPHICS_BLEND_ALPHA;
static int graphics_blend_stack[JANUS_GRAPHICS_BLEND_STACK_CAPACITY];
static size_t graphics_blend_depth;
static bool graphics_drawing_active;
static bool graphics_camera_active;
static bool graphics_render_texture_active;
static bool graphics_shader_active;
static bool graphics_scissor_active;

void janus_graphics_end_blend(void);

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
  graphics_error = "raylib 6 shared library not found; set JANUS_RAYLIB_PATH";
  return false;
}

static bool load_graphics_api(void) {
  if (graphics_loaded)
    return true;
  if (!open_graphics_library())
    return false;

#define JANUS_LOAD_GRAPHICS_SYMBOL(name)                                       \
  do {                                                                         \
    if (!load_graphics_symbol(&graphics_api.name, sizeof(graphics_api.name),   \
                              #name)) {                                        \
      graphics_error = "raylib is missing the required symbol " #name;         \
      close_graphics_library();                                                \
      memset(&graphics_api, 0, sizeof(graphics_api));                          \
      return false;                                                            \
    }                                                                          \
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
  JANUS_LOAD_GRAPHICS_SYMBOL(BeginBlendMode);
  JANUS_LOAD_GRAPHICS_SYMBOL(EndBlendMode);
  JANUS_LOAD_GRAPHICS_SYMBOL(BeginScissorMode);
  JANUS_LOAD_GRAPHICS_SYMBOL(EndScissorMode);
  JANUS_LOAD_GRAPHICS_SYMBOL(BeginDrawing);
  JANUS_LOAD_GRAPHICS_SYMBOL(EndDrawing);
  JANUS_LOAD_GRAPHICS_SYMBOL(BeginMode2D);
  JANUS_LOAD_GRAPHICS_SYMBOL(EndMode2D);
  JANUS_LOAD_GRAPHICS_SYMBOL(GetScreenToWorld2D);
  JANUS_LOAD_GRAPHICS_SYMBOL(GetWorldToScreen2D);
  JANUS_LOAD_GRAPHICS_SYMBOL(ClearBackground);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawPixel);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawLine);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawLineEx);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawLineBezier);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawLineDashed);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawLineStrip);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawCircle);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawCircleGradient);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawCircleSector);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawCircleSectorLines);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawCircleLines);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawEllipse);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawEllipseLines);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawRing);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawRingLines);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawRectangle);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawRectanglePro);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawRectangleGradientV);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawRectangleGradientH);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawRectangleGradientEx);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawRectangleLinesEx);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawRectangleRounded);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawRectangleRoundedLinesEx);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawTriangle);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawTriangleLines);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawTriangleFan);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawTriangleStrip);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawPoly);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawPolyLinesEx);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawSplineLinear);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawSplineBasis);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawSplineCatmullRom);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawSplineBezierQuadratic);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawSplineBezierCubic);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawSplineSegmentLinear);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawSplineSegmentBasis);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawSplineSegmentCatmullRom);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawSplineSegmentBezierQuadratic);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawSplineSegmentBezierCubic);
  JANUS_LOAD_GRAPHICS_SYMBOL(GetSplinePointLinear);
  JANUS_LOAD_GRAPHICS_SYMBOL(GetSplinePointBasis);
  JANUS_LOAD_GRAPHICS_SYMBOL(GetSplinePointCatmullRom);
  JANUS_LOAD_GRAPHICS_SYMBOL(GetSplinePointBezierQuad);
  JANUS_LOAD_GRAPHICS_SYMBOL(GetSplinePointBezierCubic);
  JANUS_LOAD_GRAPHICS_SYMBOL(CheckCollisionRecs);
  JANUS_LOAD_GRAPHICS_SYMBOL(CheckCollisionCircles);
  JANUS_LOAD_GRAPHICS_SYMBOL(CheckCollisionCircleRec);
  JANUS_LOAD_GRAPHICS_SYMBOL(CheckCollisionCircleLine);
  JANUS_LOAD_GRAPHICS_SYMBOL(CheckCollisionPointRec);
  JANUS_LOAD_GRAPHICS_SYMBOL(CheckCollisionPointCircle);
  JANUS_LOAD_GRAPHICS_SYMBOL(CheckCollisionPointTriangle);
  JANUS_LOAD_GRAPHICS_SYMBOL(CheckCollisionPointLine);
  JANUS_LOAD_GRAPHICS_SYMBOL(CheckCollisionPointPoly);
  JANUS_LOAD_GRAPHICS_SYMBOL(CheckCollisionLines);
  JANUS_LOAD_GRAPHICS_SYMBOL(GetCollisionRec);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawText);
  JANUS_LOAD_GRAPHICS_SYMBOL(LoadFontEx);
  JANUS_LOAD_GRAPHICS_SYMBOL(LoadCodepoints);
  JANUS_LOAD_GRAPHICS_SYMBOL(UnloadCodepoints);
  JANUS_LOAD_GRAPHICS_SYMBOL(IsFontValid);
  JANUS_LOAD_GRAPHICS_SYMBOL(UnloadFont);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawTextEx);
  JANUS_LOAD_GRAPHICS_SYMBOL(MeasureTextEx);
  JANUS_LOAD_GRAPHICS_SYMBOL(LoadImage);
  JANUS_LOAD_GRAPHICS_SYMBOL(LoadImageRaw);
  JANUS_LOAD_GRAPHICS_SYMBOL(LoadImageFromMemory);
  JANUS_LOAD_GRAPHICS_SYMBOL(LoadImageFromTexture);
  JANUS_LOAD_GRAPHICS_SYMBOL(LoadImageFromScreen);
  JANUS_LOAD_GRAPHICS_SYMBOL(IsImageValid);
  JANUS_LOAD_GRAPHICS_SYMBOL(UnloadImage);
  JANUS_LOAD_GRAPHICS_SYMBOL(ExportImage);
  JANUS_LOAD_GRAPHICS_SYMBOL(ExportImageAsCode);
  JANUS_LOAD_GRAPHICS_SYMBOL(GenImageColor);
  JANUS_LOAD_GRAPHICS_SYMBOL(GenImageGradientLinear);
  JANUS_LOAD_GRAPHICS_SYMBOL(GenImageGradientRadial);
  JANUS_LOAD_GRAPHICS_SYMBOL(GenImageGradientSquare);
  JANUS_LOAD_GRAPHICS_SYMBOL(GenImageChecked);
  JANUS_LOAD_GRAPHICS_SYMBOL(GenImageWhiteNoise);
  JANUS_LOAD_GRAPHICS_SYMBOL(GenImagePerlinNoise);
  JANUS_LOAD_GRAPHICS_SYMBOL(GenImageCellular);
  JANUS_LOAD_GRAPHICS_SYMBOL(GenImageText);
  JANUS_LOAD_GRAPHICS_SYMBOL(ImageCopy);
  JANUS_LOAD_GRAPHICS_SYMBOL(ImageFromImage);
  JANUS_LOAD_GRAPHICS_SYMBOL(ImageFromChannel);
  JANUS_LOAD_GRAPHICS_SYMBOL(ImageText);
  JANUS_LOAD_GRAPHICS_SYMBOL(ImageFormat);
  JANUS_LOAD_GRAPHICS_SYMBOL(ImageToPOT);
  JANUS_LOAD_GRAPHICS_SYMBOL(ImageCrop);
  JANUS_LOAD_GRAPHICS_SYMBOL(ImageAlphaCrop);
  JANUS_LOAD_GRAPHICS_SYMBOL(ImageAlphaClear);
  JANUS_LOAD_GRAPHICS_SYMBOL(ImageAlphaMask);
  JANUS_LOAD_GRAPHICS_SYMBOL(ImageAlphaPremultiply);
  JANUS_LOAD_GRAPHICS_SYMBOL(ImageBlurGaussian);
  JANUS_LOAD_GRAPHICS_SYMBOL(ImageKernelConvolution);
  JANUS_LOAD_GRAPHICS_SYMBOL(ImageResize);
  JANUS_LOAD_GRAPHICS_SYMBOL(ImageResizeNN);
  JANUS_LOAD_GRAPHICS_SYMBOL(ImageResizeCanvas);
  JANUS_LOAD_GRAPHICS_SYMBOL(ImageMipmaps);
  JANUS_LOAD_GRAPHICS_SYMBOL(ImageDither);
  JANUS_LOAD_GRAPHICS_SYMBOL(ImageFlipVertical);
  JANUS_LOAD_GRAPHICS_SYMBOL(ImageFlipHorizontal);
  JANUS_LOAD_GRAPHICS_SYMBOL(ImageRotate);
  JANUS_LOAD_GRAPHICS_SYMBOL(ImageRotateCW);
  JANUS_LOAD_GRAPHICS_SYMBOL(ImageRotateCCW);
  JANUS_LOAD_GRAPHICS_SYMBOL(ImageColorTint);
  JANUS_LOAD_GRAPHICS_SYMBOL(ImageColorInvert);
  JANUS_LOAD_GRAPHICS_SYMBOL(ImageColorGrayscale);
  JANUS_LOAD_GRAPHICS_SYMBOL(ImageColorContrast);
  JANUS_LOAD_GRAPHICS_SYMBOL(ImageColorBrightness);
  JANUS_LOAD_GRAPHICS_SYMBOL(ImageColorReplace);
  JANUS_LOAD_GRAPHICS_SYMBOL(GetImageAlphaBorder);
  JANUS_LOAD_GRAPHICS_SYMBOL(GetImageColor);
  JANUS_LOAD_GRAPHICS_SYMBOL(ImageClearBackground);
  JANUS_LOAD_GRAPHICS_SYMBOL(ImageDrawPixel);
  JANUS_LOAD_GRAPHICS_SYMBOL(ImageDrawLineEx);
  JANUS_LOAD_GRAPHICS_SYMBOL(ImageDrawCircle);
  JANUS_LOAD_GRAPHICS_SYMBOL(ImageDrawCircleLines);
  JANUS_LOAD_GRAPHICS_SYMBOL(ImageDrawRectangleRec);
  JANUS_LOAD_GRAPHICS_SYMBOL(ImageDrawRectangleLines);
  JANUS_LOAD_GRAPHICS_SYMBOL(ImageDrawTriangle);
  JANUS_LOAD_GRAPHICS_SYMBOL(ImageDrawTriangleEx);
  JANUS_LOAD_GRAPHICS_SYMBOL(ImageDrawTriangleLines);
  JANUS_LOAD_GRAPHICS_SYMBOL(ImageDrawTriangleFan);
  JANUS_LOAD_GRAPHICS_SYMBOL(ImageDrawTriangleStrip);
  JANUS_LOAD_GRAPHICS_SYMBOL(ImageDraw);
  JANUS_LOAD_GRAPHICS_SYMBOL(ImageDrawText);
  JANUS_LOAD_GRAPHICS_SYMBOL(LoadTexture);
  JANUS_LOAD_GRAPHICS_SYMBOL(LoadTextureFromImage);
  JANUS_LOAD_GRAPHICS_SYMBOL(IsTextureValid);
  JANUS_LOAD_GRAPHICS_SYMBOL(UnloadTexture);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawTexture);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawTextureV);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawTextureEx);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawTextureRec);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawTexturePro);
  JANUS_LOAD_GRAPHICS_SYMBOL(DrawTextureNPatch);
  JANUS_LOAD_GRAPHICS_SYMBOL(SetShapesTexture);
  JANUS_LOAD_GRAPHICS_SYMBOL(SetTextureFilter);
  JANUS_LOAD_GRAPHICS_SYMBOL(SetTextureWrap);
  JANUS_LOAD_GRAPHICS_SYMBOL(GenTextureMipmaps);
  JANUS_LOAD_GRAPHICS_SYMBOL(UpdateTexture);
  JANUS_LOAD_GRAPHICS_SYMBOL(UpdateTextureRec);
  JANUS_LOAD_GRAPHICS_SYMBOL(LoadRenderTexture);
  JANUS_LOAD_GRAPHICS_SYMBOL(IsRenderTextureValid);
  JANUS_LOAD_GRAPHICS_SYMBOL(UnloadRenderTexture);
  JANUS_LOAD_GRAPHICS_SYMBOL(BeginTextureMode);
  JANUS_LOAD_GRAPHICS_SYMBOL(EndTextureMode);
  JANUS_LOAD_GRAPHICS_SYMBOL(LoadShader);
  JANUS_LOAD_GRAPHICS_SYMBOL(IsShaderValid);
  JANUS_LOAD_GRAPHICS_SYMBOL(UnloadShader);
  JANUS_LOAD_GRAPHICS_SYMBOL(BeginShaderMode);
  JANUS_LOAD_GRAPHICS_SYMBOL(EndShaderMode);
  JANUS_LOAD_GRAPHICS_SYMBOL(GetShaderLocation);
  JANUS_LOAD_GRAPHICS_SYMBOL(SetShaderValue);
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
  JANUS_LOAD_GRAPHICS_SYMBOL(IsGamepadAvailable);
  JANUS_LOAD_GRAPHICS_SYMBOL(GetGamepadName);
  JANUS_LOAD_GRAPHICS_SYMBOL(IsGamepadButtonDown);
  JANUS_LOAD_GRAPHICS_SYMBOL(IsGamepadButtonPressed);
  JANUS_LOAD_GRAPHICS_SYMBOL(IsGamepadButtonReleased);
  JANUS_LOAD_GRAPHICS_SYMBOL(GetGamepadButtonPressed);
  JANUS_LOAD_GRAPHICS_SYMBOL(GetGamepadAxisCount);
  JANUS_LOAD_GRAPHICS_SYMBOL(GetGamepadAxisMovement);
  JANUS_LOAD_GRAPHICS_SYMBOL(SetGamepadVibration);

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
  if (!graphics_loaded)
    return;
  if (graphics_shader_active) {
    graphics_api.EndShaderMode();
    graphics_shader_active = false;
  }
  if (graphics_scissor_active) {
    graphics_api.EndScissorMode();
    graphics_scissor_active = false;
  }
  if (graphics_render_texture_active) {
    graphics_api.EndTextureMode();
    graphics_render_texture_active = false;
  }
  if (graphics_camera_active) {
    graphics_api.EndMode2D();
    graphics_camera_active = false;
  }
  if (graphics_drawing_active) {
    graphics_api.EndDrawing();
    graphics_drawing_active = false;
  }
  while (graphics_blend_depth != 0)
    janus_graphics_end_blend();
  if (graphics_api.IsWindowReady())
    graphics_api.CloseWindow();
  graphics_blend_depth = 0;
  graphics_blend_mode = JANUS_GRAPHICS_BLEND_ALPHA;
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

void janus_graphics_begin_blend(int mode) {
  if (!graphics_loaded || mode < JANUS_GRAPHICS_BLEND_ALPHA ||
      mode > JANUS_GRAPHICS_BLEND_CUSTOM_SEPARATE ||
      graphics_blend_depth == JANUS_GRAPHICS_BLEND_STACK_CAPACITY)
    return;
  graphics_blend_stack[graphics_blend_depth++] = graphics_blend_mode;
  graphics_blend_mode = mode;
  graphics_api.BeginBlendMode(mode);
}

void janus_graphics_begin_scissor(int x, int y, int width, int height) {
  if (!graphics_loaded || graphics_scissor_active || width < 0 || height < 0)
    return;
  graphics_api.BeginScissorMode(x, y, width, height);
  graphics_scissor_active = true;
}

void janus_graphics_end_scissor(void) {
  if (!graphics_loaded || !graphics_scissor_active)
    return;
  graphics_api.EndScissorMode();
  graphics_scissor_active = false;
}

void janus_graphics_end_blend(void) {
  if (!graphics_loaded || graphics_blend_depth == 0)
    return;
  const int previous_mode = graphics_blend_stack[--graphics_blend_depth];
  graphics_api.EndBlendMode();
  graphics_blend_mode = JANUS_GRAPHICS_BLEND_ALPHA;
  if (previous_mode != JANUS_GRAPHICS_BLEND_ALPHA) {
    graphics_api.BeginBlendMode(previous_mode);
    graphics_blend_mode = previous_mode;
  }
}

void janus_graphics_begin_drawing(void) {
  if (!graphics_loaded || graphics_drawing_active)
    return;
  graphics_api.BeginDrawing();
  graphics_drawing_active = true;
}

void janus_graphics_end_drawing(void) {
  if (!graphics_loaded || !graphics_drawing_active || graphics_camera_active)
    return;
  graphics_api.EndDrawing();
  graphics_drawing_active = false;
}

static JanusRaylibCamera2D make_camera(float offset_x, float offset_y,
                                       float target_x, float target_y,
                                       float rotation, float zoom) {
  JanusRaylibCamera2D camera = {
      {offset_x, offset_y}, {target_x, target_y}, rotation, zoom};
  return camera;
}

void janus_graphics_begin_camera(float offset_x, float offset_y, float target_x,
                                 float target_y, float rotation, float zoom) {
  if (!graphics_loaded || !graphics_drawing_active || graphics_camera_active)
    return;
  graphics_api.BeginMode2D(
      make_camera(offset_x, offset_y, target_x, target_y, rotation, zoom));
  graphics_camera_active = true;
}

void janus_graphics_end_camera(void) {
  if (!graphics_loaded || !graphics_camera_active)
    return;
  graphics_api.EndMode2D();
  graphics_camera_active = false;
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

void janus_graphics_draw_line_ex(float start_x, float start_y, float end_x,
                                 float end_y, float thickness, uint32_t color) {
  if (graphics_loaded)
    graphics_api.DrawLineEx((JanusRaylibVector2){start_x, start_y},
                            (JanusRaylibVector2){end_x, end_y}, thickness,
                            unpack_color(color));
}

void janus_graphics_draw_line_bezier(float start_x, float start_y, float end_x,
                                     float end_y, float thickness,
                                     uint32_t color) {
  if (graphics_loaded)
    graphics_api.DrawLineBezier((JanusRaylibVector2){start_x, start_y},
                                (JanusRaylibVector2){end_x, end_y}, thickness,
                                unpack_color(color));
}

void janus_graphics_draw_line_dashed(float start_x, float start_y, float end_x,
                                     float end_y, int dash_size, int space_size,
                                     uint32_t color) {
  if (graphics_loaded)
    graphics_api.DrawLineDashed((JanusRaylibVector2){start_x, start_y},
                                (JanusRaylibVector2){end_x, end_y}, dash_size,
                                space_size, unpack_color(color));
}

void janus_graphics_draw_circle(int center_x, int center_y, float radius,
                                uint32_t color) {
  if (graphics_loaded)
    graphics_api.DrawCircle(center_x, center_y, radius, unpack_color(color));
}

void janus_graphics_draw_circle_gradient(float x, float y, float radius,
                                         uint32_t inner, uint32_t outer) {
  if (graphics_loaded)
    graphics_api.DrawCircleGradient((JanusRaylibVector2){x, y}, radius,
                                    unpack_color(inner), unpack_color(outer));
}

void janus_graphics_draw_circle_sector(float x, float y, float radius,
                                       float start_angle, float end_angle,
                                       int segments, uint32_t color,
                                       bool lines) {
  if (!graphics_loaded)
    return;
  if (lines)
    graphics_api.DrawCircleSectorLines((JanusRaylibVector2){x, y}, radius,
                                       start_angle, end_angle, segments,
                                       unpack_color(color));
  else
    graphics_api.DrawCircleSector((JanusRaylibVector2){x, y}, radius,
                                  start_angle, end_angle, segments,
                                  unpack_color(color));
}

void janus_graphics_draw_circle_lines(int x, int y, float radius,
                                      uint32_t color) {
  if (graphics_loaded)
    graphics_api.DrawCircleLines(x, y, radius, unpack_color(color));
}

void janus_graphics_draw_ellipse(int x, int y, float radius_x, float radius_y,
                                 uint32_t color, bool lines) {
  if (!graphics_loaded)
    return;
  if (lines)
    graphics_api.DrawEllipseLines(x, y, radius_x, radius_y,
                                  unpack_color(color));
  else
    graphics_api.DrawEllipse(x, y, radius_x, radius_y, unpack_color(color));
}

void janus_graphics_draw_ring(float x, float y, float inner_radius,
                              float outer_radius, float start_angle,
                              float end_angle, int segments, uint32_t color,
                              bool lines) {
  if (!graphics_loaded)
    return;
  if (lines)
    graphics_api.DrawRingLines((JanusRaylibVector2){x, y}, inner_radius,
                               outer_radius, start_angle, end_angle, segments,
                               unpack_color(color));
  else
    graphics_api.DrawRing((JanusRaylibVector2){x, y}, inner_radius,
                          outer_radius, start_angle, end_angle, segments,
                          unpack_color(color));
}

void janus_graphics_draw_rectangle(int x, int y, int width, int height,
                                   uint32_t color) {
  if (graphics_loaded)
    graphics_api.DrawRectangle(x, y, width, height, unpack_color(color));
}

void janus_graphics_draw_rectangle_pro(float x, float y, float width,
                                       float height, float origin_x,
                                       float origin_y, float rotation,
                                       uint32_t color) {
  if (graphics_loaded)
    graphics_api.DrawRectanglePro((JanusRaylibRectangle){x, y, width, height},
                                  (JanusRaylibVector2){origin_x, origin_y},
                                  rotation, unpack_color(color));
}

void janus_graphics_draw_rectangle_gradient(int x, int y, int width, int height,
                                            uint32_t first, uint32_t second,
                                            bool horizontal) {
  if (!graphics_loaded)
    return;
  if (horizontal)
    graphics_api.DrawRectangleGradientH(
        x, y, width, height, unpack_color(first), unpack_color(second));
  else
    graphics_api.DrawRectangleGradientV(
        x, y, width, height, unpack_color(first), unpack_color(second));
}

void janus_graphics_draw_rectangle_gradient_ex(float x, float y, float width,
                                               float height, uint32_t top_left,
                                               uint32_t bottom_left,
                                               uint32_t bottom_right,
                                               uint32_t top_right) {
  if (graphics_loaded)
    graphics_api.DrawRectangleGradientEx(
        (JanusRaylibRectangle){x, y, width, height}, unpack_color(top_left),
        unpack_color(bottom_left), unpack_color(bottom_right),
        unpack_color(top_right));
}

void janus_graphics_draw_rectangle_lines(float x, float y, float width,
                                         float height, float thickness,
                                         uint32_t color) {
  if (graphics_loaded)
    graphics_api.DrawRectangleLinesEx(
        (JanusRaylibRectangle){x, y, width, height}, thickness,
        unpack_color(color));
}

void janus_graphics_draw_rectangle_rounded(float x, float y, float width,
                                           float height, float roundness,
                                           int segments, float thickness,
                                           uint32_t color, bool lines) {
  if (!graphics_loaded)
    return;
  JanusRaylibRectangle rectangle = {x, y, width, height};
  if (lines)
    graphics_api.DrawRectangleRoundedLinesEx(rectangle, roundness, segments,
                                             thickness, unpack_color(color));
  else
    graphics_api.DrawRectangleRounded(rectangle, roundness, segments,
                                      unpack_color(color));
}

void janus_graphics_draw_triangle(float x1, float y1, float x2, float y2,
                                  float x3, float y3, uint32_t color,
                                  bool lines) {
  if (!graphics_loaded)
    return;
  if (lines)
    graphics_api.DrawTriangleLines(
        (JanusRaylibVector2){x1, y1}, (JanusRaylibVector2){x2, y2},
        (JanusRaylibVector2){x3, y3}, unpack_color(color));
  else
    graphics_api.DrawTriangle(
        (JanusRaylibVector2){x1, y1}, (JanusRaylibVector2){x2, y2},
        (JanusRaylibVector2){x3, y3}, unpack_color(color));
}

void janus_graphics_draw_polygon(float x, float y, int sides, float radius,
                                 float rotation, float thickness,
                                 uint32_t color, bool lines) {
  if (!graphics_loaded)
    return;
  if (lines)
    graphics_api.DrawPolyLinesEx((JanusRaylibVector2){x, y}, sides, radius,
                                 rotation, thickness, unpack_color(color));
  else
    graphics_api.DrawPoly((JanusRaylibVector2){x, y}, sides, radius, rotation,
                          unpack_color(color));
}

void janus_graphics_draw_point_sequence(const void *points, int count,
                                        float thickness, uint32_t color,
                                        int kind) {
  if (!graphics_loaded || points == NULL || count <= 0)
    return;
  const JanusRaylibVector2 *values = points;
  switch (kind) {
  case 0:
    graphics_api.DrawLineStrip(values, count, unpack_color(color));
    break;
  case 1:
    graphics_api.DrawTriangleFan(values, count, unpack_color(color));
    break;
  case 2:
    graphics_api.DrawTriangleStrip(values, count, unpack_color(color));
    break;
  case 3:
    graphics_api.DrawSplineLinear(values, count, thickness,
                                  unpack_color(color));
    break;
  case 4:
    graphics_api.DrawSplineBasis(values, count, thickness, unpack_color(color));
    break;
  case 5:
    graphics_api.DrawSplineCatmullRom(values, count, thickness,
                                      unpack_color(color));
    break;
  case 6:
    graphics_api.DrawSplineBezierQuadratic(values, count, thickness,
                                           unpack_color(color));
    break;
  case 7:
    graphics_api.DrawSplineBezierCubic(values, count, thickness,
                                       unpack_color(color));
    break;
  default:
    break;
  }
}

void janus_graphics_draw_spline_segment(float x1, float y1, float x2, float y2,
                                        float x3, float y3, float x4, float y4,
                                        float thickness, uint32_t color,
                                        int kind) {
  if (!graphics_loaded)
    return;
  JanusRaylibVector2 first = {x1, y1};
  JanusRaylibVector2 second = {x2, y2};
  JanusRaylibVector2 third = {x3, y3};
  JanusRaylibVector2 fourth = {x4, y4};
  switch (kind) {
  case 0:
    graphics_api.DrawSplineSegmentLinear(first, second, thickness,
                                         unpack_color(color));
    break;
  case 1:
    graphics_api.DrawSplineSegmentBasis(first, second, third, fourth, thickness,
                                        unpack_color(color));
    break;
  case 2:
    graphics_api.DrawSplineSegmentCatmullRom(first, second, third, fourth,
                                             thickness, unpack_color(color));
    break;
  case 3:
    graphics_api.DrawSplineSegmentBezierQuadratic(
        first, second, third, thickness, unpack_color(color));
    break;
  case 4:
    graphics_api.DrawSplineSegmentBezierCubic(first, second, third, fourth,
                                              thickness, unpack_color(color));
    break;
  default:
    break;
  }
}

static JanusRaylibVector2 spline_point(float x1, float y1, float x2, float y2,
                                       float x3, float y3, float x4, float y4,
                                       float amount, int kind) {
  JanusRaylibVector2 first = {x1, y1};
  JanusRaylibVector2 second = {x2, y2};
  JanusRaylibVector2 third = {x3, y3};
  JanusRaylibVector2 fourth = {x4, y4};
  if (!graphics_loaded)
    return (JanusRaylibVector2){0};
  switch (kind) {
  case 0:
    return graphics_api.GetSplinePointLinear(first, second, amount);
  case 1:
    return graphics_api.GetSplinePointBasis(first, second, third, fourth,
                                            amount);
  case 2:
    return graphics_api.GetSplinePointCatmullRom(first, second, third, fourth,
                                                 amount);
  case 3:
    return graphics_api.GetSplinePointBezierQuad(first, second, third, amount);
  case 4:
    return graphics_api.GetSplinePointBezierCubic(first, second, third, fourth,
                                                  amount);
  default:
    return (JanusRaylibVector2){0};
  }
}

float janus_graphics_spline_point_component(float x1, float y1, float x2,
                                            float y2, float x3, float y3,
                                            float x4, float y4, float amount,
                                            int kind, bool y_component) {
  JanusRaylibVector2 result =
      spline_point(x1, y1, x2, y2, x3, y3, x4, y4, amount, kind);
  return y_component ? result.y : result.x;
}

bool janus_graphics_collision_rectangles(float x1, float y1, float width1,
                                         float height1, float x2, float y2,
                                         float width2, float height2) {
  return graphics_loaded &&
         graphics_api.CheckCollisionRecs(
             (JanusRaylibRectangle){x1, y1, width1, height1},
             (JanusRaylibRectangle){x2, y2, width2, height2});
}

bool janus_graphics_collision_circles(float x1, float y1, float radius1,
                                      float x2, float y2, float radius2) {
  return graphics_loaded && graphics_api.CheckCollisionCircles(
                                (JanusRaylibVector2){x1, y1}, radius1,
                                (JanusRaylibVector2){x2, y2}, radius2);
}

bool janus_graphics_collision_circle_rectangle(float x, float y, float radius,
                                               float rectangle_x,
                                               float rectangle_y, float width,
                                               float height) {
  return graphics_loaded &&
         graphics_api.CheckCollisionCircleRec(
             (JanusRaylibVector2){x, y}, radius,
             (JanusRaylibRectangle){rectangle_x, rectangle_y, width, height});
}

bool janus_graphics_collision_circle_line(float x, float y, float radius,
                                          float x1, float y1, float x2,
                                          float y2) {
  return graphics_loaded &&
         graphics_api.CheckCollisionCircleLine(
             (JanusRaylibVector2){x, y}, radius, (JanusRaylibVector2){x1, y1},
             (JanusRaylibVector2){x2, y2});
}

bool janus_graphics_collision_point_rectangle(float x, float y,
                                              float rectangle_x,
                                              float rectangle_y, float width,
                                              float height) {
  return graphics_loaded &&
         graphics_api.CheckCollisionPointRec(
             (JanusRaylibVector2){x, y},
             (JanusRaylibRectangle){rectangle_x, rectangle_y, width, height});
}

bool janus_graphics_collision_point_circle(float x, float y, float center_x,
                                           float center_y, float radius) {
  return graphics_loaded &&
         graphics_api.CheckCollisionPointCircle(
             (JanusRaylibVector2){x, y},
             (JanusRaylibVector2){center_x, center_y}, radius);
}

bool janus_graphics_collision_point_triangle(float x, float y, float x1,
                                             float y1, float x2, float y2,
                                             float x3, float y3) {
  return graphics_loaded &&
         graphics_api.CheckCollisionPointTriangle(
             (JanusRaylibVector2){x, y}, (JanusRaylibVector2){x1, y1},
             (JanusRaylibVector2){x2, y2}, (JanusRaylibVector2){x3, y3});
}

bool janus_graphics_collision_point_line(float x, float y, float x1, float y1,
                                         float x2, float y2, int threshold) {
  return graphics_loaded &&
         graphics_api.CheckCollisionPointLine(
             (JanusRaylibVector2){x, y}, (JanusRaylibVector2){x1, y1},
             (JanusRaylibVector2){x2, y2}, threshold);
}

bool janus_graphics_collision_point_polygon(float x, float y,
                                            const void *points, int count) {
  return graphics_loaded && points != NULL && count > 0 &&
         graphics_api.CheckCollisionPointPoly((JanusRaylibVector2){x, y},
                                              points, count);
}

static bool collision_lines(float x1, float y1, float x2, float y2, float x3,
                            float y3, float x4, float y4,
                            JanusRaylibVector2 *point) {
  return graphics_loaded &&
         graphics_api.CheckCollisionLines(
             (JanusRaylibVector2){x1, y1}, (JanusRaylibVector2){x2, y2},
             (JanusRaylibVector2){x3, y3}, (JanusRaylibVector2){x4, y4}, point);
}

bool janus_graphics_collision_lines(float x1, float y1, float x2, float y2,
                                    float x3, float y3, float x4, float y4) {
  JanusRaylibVector2 point = {0};
  return collision_lines(x1, y1, x2, y2, x3, y3, x4, y4, &point);
}

float janus_graphics_collision_lines_component(float x1, float y1, float x2,
                                               float y2, float x3, float y3,
                                               float x4, float y4,
                                               bool y_component) {
  JanusRaylibVector2 point = {0};
  (void)collision_lines(x1, y1, x2, y2, x3, y3, x4, y4, &point);
  return y_component ? point.y : point.x;
}

static JanusRaylibRectangle collision_rectangle(float x1, float y1,
                                                float width1, float height1,
                                                float x2, float y2,
                                                float width2, float height2) {
  if (!graphics_loaded)
    return (JanusRaylibRectangle){0};
  return graphics_api.GetCollisionRec(
      (JanusRaylibRectangle){x1, y1, width1, height1},
      (JanusRaylibRectangle){x2, y2, width2, height2});
}

#define JANUS_COLLISION_RECTANGLE_COMPONENT(name, member)                      \
  float name(float x1, float y1, float width1, float height1, float x2,        \
             float y2, float width2, float height2) {                          \
    return collision_rectangle(x1, y1, width1, height1, x2, y2, width2,        \
                               height2)                                        \
        .member;                                                               \
  }

JANUS_COLLISION_RECTANGLE_COMPONENT(janus_graphics_collision_rectangle_x, x)
JANUS_COLLISION_RECTANGLE_COMPONENT(janus_graphics_collision_rectangle_y, y)
JANUS_COLLISION_RECTANGLE_COMPONENT(janus_graphics_collision_rectangle_width,
                                    width)
JANUS_COLLISION_RECTANGLE_COMPONENT(janus_graphics_collision_rectangle_height,
                                    height)

#undef JANUS_COLLISION_RECTANGLE_COMPONENT

void janus_graphics_draw_text(const void *text, int x, int y, int font_size,
                              uint32_t color) {
  if (graphics_loaded && text != NULL)
    graphics_api.DrawText((const char *)text, x, y, font_size,
                          unpack_color(color));
}

void *janus_graphics_load_font(const void *file_name, int font_size) {
  if (!graphics_loaded || file_name == NULL || font_size <= 0)
    return NULL;
  JanusRaylibFont *font = malloc(sizeof(*font));
  if (font == NULL)
    return NULL;
  *font = graphics_api.LoadFontEx((const char *)file_name, font_size, NULL, 0);
  if (!graphics_api.IsFontValid(*font)) {
    free(font);
    return NULL;
  }
  return font;
}

void *janus_graphics_load_font_utf8(const void *file_name, int font_size,
                                    const void *characters) {
  if (!graphics_loaded || file_name == NULL || characters == NULL ||
      font_size <= 0)
    return NULL;
  int codepoint_count = 0;
  int *codepoints =
      graphics_api.LoadCodepoints((const char *)characters, &codepoint_count);
  if (codepoints == NULL)
    return NULL;
  if (codepoint_count <= 0) {
    graphics_api.UnloadCodepoints(codepoints);
    return NULL;
  }
  JanusRaylibFont *font = malloc(sizeof(*font));
  if (font == NULL) {
    graphics_api.UnloadCodepoints(codepoints);
    return NULL;
  }
  *font = graphics_api.LoadFontEx((const char *)file_name, font_size,
                                  codepoints, codepoint_count);
  graphics_api.UnloadCodepoints(codepoints);
  if (!graphics_api.IsFontValid(*font)) {
    free(font);
    return NULL;
  }
  return font;
}

bool janus_graphics_font_is_valid(const void *handle) {
  return graphics_loaded && handle != NULL &&
         graphics_api.IsFontValid(*(const JanusRaylibFont *)handle);
}

void janus_graphics_unload_font(void *handle) {
  if (handle == NULL)
    return;
  if (graphics_loaded) {
    JanusRaylibFont *font = handle;
    if (graphics_api.IsFontValid(*font))
      graphics_api.UnloadFont(*font);
  }
  free(handle);
}

void janus_graphics_draw_text_font(const void *handle, const void *text,
                                   float x, float y, float font_size,
                                   float spacing, uint32_t color) {
  if (janus_graphics_font_is_valid(handle) && text != NULL)
    graphics_api.DrawTextEx(*(const JanusRaylibFont *)handle,
                            (const char *)text, (JanusRaylibVector2){x, y},
                            font_size, spacing, unpack_color(color));
}

float janus_graphics_measure_text_width(const void *handle, const void *text,
                                        float font_size, float spacing) {
  if (!janus_graphics_font_is_valid(handle) || text == NULL)
    return 0.0f;
  return graphics_api
      .MeasureTextEx(*(const JanusRaylibFont *)handle, (const char *)text,
                     font_size, spacing)
      .x;
}

float janus_graphics_measure_text_height(const void *handle, const void *text,
                                         float font_size, float spacing) {
  if (!janus_graphics_font_is_valid(handle) || text == NULL)
    return 0.0f;
  return graphics_api
      .MeasureTextEx(*(const JanusRaylibFont *)handle, (const char *)text,
                     font_size, spacing)
      .y;
}

bool janus_graphics_texture_is_valid(const void *handle);

static void *own_image(JanusRaylibImage image) {
  if (!graphics_loaded || !graphics_api.IsImageValid(image))
    return NULL;
  JanusRaylibImage *owned = malloc(sizeof(*owned));
  if (owned == NULL) {
    graphics_api.UnloadImage(image);
    return NULL;
  }
  *owned = image;
  return owned;
}

void *janus_graphics_load_image(const void *file_name) {
  if (!graphics_loaded || file_name == NULL)
    return NULL;
  return own_image(graphics_api.LoadImage((const char *)file_name));
}

void *janus_graphics_load_raw_image(const void *file_name, int width,
                                    int height, int format, int header_size) {
  if (!graphics_loaded || file_name == NULL || width <= 0 || height <= 0)
    return NULL;
  return own_image(graphics_api.LoadImageRaw((const char *)file_name, width,
                                             height, format, header_size));
}

void *janus_graphics_load_image_memory(const void *file_type, const void *data,
                                       int data_size) {
  if (!graphics_loaded || file_type == NULL || data == NULL || data_size <= 0)
    return NULL;
  return own_image(graphics_api.LoadImageFromMemory(
      (const char *)file_type, (const unsigned char *)data, data_size));
}

void *janus_graphics_load_image_texture(const void *texture_handle) {
  if (!janus_graphics_texture_is_valid(texture_handle))
    return NULL;
  return own_image(graphics_api.LoadImageFromTexture(
      *(const JanusRaylibTexture *)texture_handle));
}

void *janus_graphics_load_image_screen(void) {
  if (!graphics_loaded)
    return NULL;
  return own_image(graphics_api.LoadImageFromScreen());
}

void *janus_graphics_generate_image(int width, int height, int kind, int first,
                                    int second, float amount,
                                    uint32_t first_color, uint32_t second_color,
                                    const void *text) {
  if (!graphics_loaded || width <= 0 || height <= 0)
    return NULL;
  JanusRaylibImage image = {0};
  switch (kind) {
  case 0:
    image =
        graphics_api.GenImageColor(width, height, unpack_color(first_color));
    break;
  case 1:
    image = graphics_api.GenImageGradientLinear(width, height, first,
                                                unpack_color(first_color),
                                                unpack_color(second_color));
    break;
  case 2:
    image = graphics_api.GenImageGradientRadial(width, height, amount,
                                                unpack_color(first_color),
                                                unpack_color(second_color));
    break;
  case 3:
    image = graphics_api.GenImageGradientSquare(width, height, amount,
                                                unpack_color(first_color),
                                                unpack_color(second_color));
    break;
  case 4:
    image = graphics_api.GenImageChecked(width, height, first, second,
                                         unpack_color(first_color),
                                         unpack_color(second_color));
    break;
  case 5:
    image = graphics_api.GenImageWhiteNoise(width, height, amount);
    break;
  case 6:
    image =
        graphics_api.GenImagePerlinNoise(width, height, first, second, amount);
    break;
  case 7:
    image = graphics_api.GenImageCellular(width, height, first);
    break;
  case 8:
    if (text != NULL)
      image = graphics_api.GenImageText(width, height, (const char *)text);
    break;
  default:
    break;
  }
  return own_image(image);
}

bool janus_graphics_image_is_valid(const void *handle) {
  return graphics_loaded && handle != NULL &&
         graphics_api.IsImageValid(*(const JanusRaylibImage *)handle);
}

int janus_graphics_image_width(const void *handle) {
  return handle != NULL ? ((const JanusRaylibImage *)handle)->width : 0;
}

int janus_graphics_image_height(const void *handle) {
  return handle != NULL ? ((const JanusRaylibImage *)handle)->height : 0;
}

int janus_graphics_image_mipmaps(const void *handle) {
  return handle != NULL ? ((const JanusRaylibImage *)handle)->mipmaps : 0;
}

int janus_graphics_image_format(const void *handle) {
  return handle != NULL ? ((const JanusRaylibImage *)handle)->format : 0;
}

void janus_graphics_unload_image(void *handle) {
  if (handle == NULL)
    return;
  JanusRaylibImage image = *(JanusRaylibImage *)handle;
  if (graphics_loaded && graphics_api.IsImageValid(image))
    graphics_api.UnloadImage(image);
  free(handle);
}

bool janus_graphics_export_image(const void *handle, const void *file_name) {
  return janus_graphics_image_is_valid(handle) && file_name != NULL &&
         graphics_api.ExportImage(*(const JanusRaylibImage *)handle,
                                  (const char *)file_name);
}

bool janus_graphics_export_image_code(const void *handle,
                                      const void *file_name) {
  return janus_graphics_image_is_valid(handle) && file_name != NULL &&
         graphics_api.ExportImageAsCode(*(const JanusRaylibImage *)handle,
                                        (const char *)file_name);
}

void *janus_graphics_copy_image(const void *handle, float x, float y,
                                float width, float height, bool region) {
  if (!janus_graphics_image_is_valid(handle))
    return NULL;
  JanusRaylibImage image = *(const JanusRaylibImage *)handle;
  return own_image(region
                       ? graphics_api.ImageFromImage(
                             image, (JanusRaylibRectangle){x, y, width, height})
                       : graphics_api.ImageCopy(image));
}

void *janus_graphics_image_channel(const void *handle, int channel) {
  if (!janus_graphics_image_is_valid(handle) || channel < 0 || channel > 3)
    return NULL;
  return own_image(graphics_api.ImageFromChannel(
      *(const JanusRaylibImage *)handle, channel));
}

void *janus_graphics_image_text(const void *text, int font_size,
                                uint32_t color) {
  if (!graphics_loaded || text == NULL || font_size <= 0)
    return NULL;
  return own_image(graphics_api.ImageText((const char *)text, font_size,
                                          unpack_color(color)));
}

void janus_graphics_image_alpha_mask(void *handle, const void *mask_handle) {
  if (janus_graphics_image_is_valid(handle) &&
      janus_graphics_image_is_valid(mask_handle))
    graphics_api.ImageAlphaMask((JanusRaylibImage *)handle,
                                *(const JanusRaylibImage *)mask_handle);
}

void janus_graphics_image_convolution(void *handle, const void *kernel,
                                      int kernel_size) {
  if (janus_graphics_image_is_valid(handle) && kernel != NULL &&
      kernel_size > 0)
    graphics_api.ImageKernelConvolution((JanusRaylibImage *)handle,
                                        (const float *)kernel, kernel_size);
}

void janus_graphics_transform_image(void *handle, int kind, int first,
                                    int second, int third, int fourth, float x,
                                    float y, float width, float height,
                                    float amount, uint32_t first_color,
                                    uint32_t second_color) {
  if (!janus_graphics_image_is_valid(handle))
    return;
  JanusRaylibImage *image = handle;
  switch (kind) {
  case 0:
    graphics_api.ImageFormat(image, first);
    break;
  case 1:
    graphics_api.ImageToPOT(image, unpack_color(first_color));
    break;
  case 2:
    graphics_api.ImageCrop(image, (JanusRaylibRectangle){x, y, width, height});
    break;
  case 3:
    graphics_api.ImageAlphaCrop(image, amount);
    break;
  case 4:
    graphics_api.ImageAlphaClear(image, unpack_color(first_color), amount);
    break;
  case 5:
    graphics_api.ImageAlphaPremultiply(image);
    break;
  case 6:
    graphics_api.ImageBlurGaussian(image, first);
    break;
  case 7:
    graphics_api.ImageResize(image, first, second);
    break;
  case 8:
    graphics_api.ImageResizeNN(image, first, second);
    break;
  case 9:
    graphics_api.ImageResizeCanvas(image, first, second, third, fourth,
                                   unpack_color(first_color));
    break;
  case 10:
    graphics_api.ImageMipmaps(image);
    break;
  case 11:
    graphics_api.ImageDither(image, first, second, third, fourth);
    break;
  case 12:
    graphics_api.ImageFlipVertical(image);
    break;
  case 13:
    graphics_api.ImageFlipHorizontal(image);
    break;
  case 14:
    graphics_api.ImageRotate(image, first);
    break;
  case 15:
    graphics_api.ImageRotateCW(image);
    break;
  case 16:
    graphics_api.ImageRotateCCW(image);
    break;
  case 17:
    graphics_api.ImageColorTint(image, unpack_color(first_color));
    break;
  case 18:
    graphics_api.ImageColorInvert(image);
    break;
  case 19:
    graphics_api.ImageColorGrayscale(image);
    break;
  case 20:
    graphics_api.ImageColorContrast(image, amount);
    break;
  case 21:
    graphics_api.ImageColorBrightness(image, first);
    break;
  case 22:
    graphics_api.ImageColorReplace(image, unpack_color(first_color),
                                   unpack_color(second_color));
    break;
  default:
    break;
  }
}

static JanusRaylibRectangle image_alpha_border(const void *handle,
                                               float threshold) {
  if (!janus_graphics_image_is_valid(handle))
    return (JanusRaylibRectangle){0};
  return graphics_api.GetImageAlphaBorder(*(const JanusRaylibImage *)handle,
                                          threshold);
}

#define JANUS_IMAGE_BORDER_COMPONENT(name, member)                             \
  float name(const void *handle, float threshold) {                            \
    return image_alpha_border(handle, threshold).member;                       \
  }
JANUS_IMAGE_BORDER_COMPONENT(janus_graphics_image_alpha_border_x, x)
JANUS_IMAGE_BORDER_COMPONENT(janus_graphics_image_alpha_border_y, y)
JANUS_IMAGE_BORDER_COMPONENT(janus_graphics_image_alpha_border_width, width)
JANUS_IMAGE_BORDER_COMPONENT(janus_graphics_image_alpha_border_height, height)
#undef JANUS_IMAGE_BORDER_COMPONENT

uint32_t janus_graphics_image_color(const void *handle, int x, int y) {
  if (!janus_graphics_image_is_valid(handle))
    return 0;
  JanusRaylibColor color =
      graphics_api.GetImageColor(*(const JanusRaylibImage *)handle, x, y);
  return janus_graphics_rgba(color.red, color.green, color.blue, color.alpha);
}

void janus_graphics_draw_on_image(void *handle, int kind, float x1, float y1,
                                  float x2, float y2, float x3, float y3,
                                  int thickness, uint32_t color) {
  if (!janus_graphics_image_is_valid(handle))
    return;
  JanusRaylibImage *image = handle;
  switch (kind) {
  case 0:
    graphics_api.ImageClearBackground(image, unpack_color(color));
    break;
  case 1:
    graphics_api.ImageDrawPixel(image, (int)x1, (int)y1, unpack_color(color));
    break;
  case 2:
    graphics_api.ImageDrawLineEx(image, (JanusRaylibVector2){x1, y1},
                                 (JanusRaylibVector2){x2, y2}, thickness,
                                 unpack_color(color));
    break;
  case 3:
    graphics_api.ImageDrawCircle(image, (int)x1, (int)y1, (int)x2,
                                 unpack_color(color));
    break;
  case 4:
    graphics_api.ImageDrawCircleLines(image, (int)x1, (int)y1, (int)x2,
                                      unpack_color(color));
    break;
  case 5:
    graphics_api.ImageDrawRectangleRec(
        image, (JanusRaylibRectangle){x1, y1, x2, y2}, unpack_color(color));
    break;
  case 6:
    graphics_api.ImageDrawRectangleLines(image,
                                         (JanusRaylibRectangle){x1, y1, x2, y2},
                                         thickness, unpack_color(color));
    break;
  case 7:
    graphics_api.ImageDrawTriangle(
        image, (JanusRaylibVector2){x1, y1}, (JanusRaylibVector2){x2, y2},
        (JanusRaylibVector2){x3, y3}, unpack_color(color));
    break;
  case 8:
    graphics_api.ImageDrawTriangleLines(
        image, (JanusRaylibVector2){x1, y1}, (JanusRaylibVector2){x2, y2},
        (JanusRaylibVector2){x3, y3}, unpack_color(color));
    break;
  default:
    break;
  }
}

void janus_graphics_draw_triangle_colors_on_image(
    void *handle, float x1, float y1, float x2, float y2, float x3, float y3,
    uint32_t first_color, uint32_t second_color, uint32_t third_color) {
  if (janus_graphics_image_is_valid(handle))
    graphics_api.ImageDrawTriangleEx(
        (JanusRaylibImage *)handle, (JanusRaylibVector2){x1, y1},
        (JanusRaylibVector2){x2, y2}, (JanusRaylibVector2){x3, y3},
        unpack_color(first_color), unpack_color(second_color),
        unpack_color(third_color));
}

void janus_graphics_draw_points_on_image(void *handle, const void *points,
                                         int count, uint32_t color,
                                         bool strip) {
  if (!janus_graphics_image_is_valid(handle) || points == NULL || count <= 0)
    return;
  if (strip)
    graphics_api.ImageDrawTriangleStrip((JanusRaylibImage *)handle, points,
                                        count, unpack_color(color));
  else
    graphics_api.ImageDrawTriangleFan((JanusRaylibImage *)handle, points, count,
                                      unpack_color(color));
}

void janus_graphics_draw_image(void *handle, const void *source_handle,
                               float source_x, float source_y,
                               float source_width, float source_height,
                               float destination_x, float destination_y,
                               float destination_width,
                               float destination_height, uint32_t tint) {
  if (janus_graphics_image_is_valid(handle) &&
      janus_graphics_image_is_valid(source_handle))
    graphics_api.ImageDraw(
        (JanusRaylibImage *)handle, *(const JanusRaylibImage *)source_handle,
        (JanusRaylibRectangle){source_x, source_y, source_width, source_height},
        (JanusRaylibRectangle){destination_x, destination_y, destination_width,
                               destination_height},
        unpack_color(tint));
}

void janus_graphics_draw_text_on_image(void *handle, const void *text, int x,
                                       int y, int font_size, uint32_t color) {
  if (janus_graphics_image_is_valid(handle) && text != NULL)
    graphics_api.ImageDrawText((JanusRaylibImage *)handle, (const char *)text,
                               x, y, font_size, unpack_color(color));
}

void *janus_graphics_texture_from_image(const void *handle) {
  if (!janus_graphics_image_is_valid(handle))
    return NULL;
  JanusRaylibTexture *texture = malloc(sizeof(*texture));
  if (texture == NULL)
    return NULL;
  *texture =
      graphics_api.LoadTextureFromImage(*(const JanusRaylibImage *)handle);
  if (!graphics_api.IsTextureValid(*texture)) {
    free(texture);
    return NULL;
  }
  return texture;
}

void janus_graphics_update_texture_from_image(void *texture_handle,
                                              const void *image_handle, float x,
                                              float y, float width,
                                              float height, bool region) {
  if (!janus_graphics_texture_is_valid(texture_handle) ||
      !janus_graphics_image_is_valid(image_handle))
    return;
  JanusRaylibTexture texture = *(const JanusRaylibTexture *)texture_handle;
  const JanusRaylibImage *image = image_handle;
  if (region)
    graphics_api.UpdateTextureRec(
        texture, (JanusRaylibRectangle){x, y, width, height}, image->data);
  else
    graphics_api.UpdateTexture(texture, image->data);
}

void *janus_graphics_load_texture(const void *file_name) {
  if (!graphics_loaded || file_name == NULL)
    return NULL;
  JanusRaylibTexture *texture = malloc(sizeof(*texture));
  if (texture == NULL)
    return NULL;
  *texture = graphics_api.LoadTexture((const char *)file_name);
  if (!graphics_api.IsTextureValid(*texture)) {
    free(texture);
    return NULL;
  }
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

void janus_graphics_draw_texture_at(const void *handle, float x, float y,
                                    uint32_t tint) {
  const JanusRaylibTexture *texture = handle;
  if (graphics_loaded && texture != NULL)
    graphics_api.DrawTextureV(*texture, (JanusRaylibVector2){x, y},
                              unpack_color(tint));
}

void janus_graphics_draw_texture_ex(const void *handle, float x, float y,
                                    float rotation, float scale,
                                    uint32_t tint) {
  const JanusRaylibTexture *texture = handle;
  if (graphics_loaded && texture != NULL)
    graphics_api.DrawTextureEx(*texture, (JanusRaylibVector2){x, y}, rotation,
                               scale, unpack_color(tint));
}

void janus_graphics_draw_texture_rec(const void *handle, float source_x,
                                     float source_y, float source_width,
                                     float source_height, float x, float y,
                                     uint32_t tint) {
  const JanusRaylibTexture *texture = handle;
  if (graphics_loaded && texture != NULL)
    graphics_api.DrawTextureRec(
        *texture,
        (JanusRaylibRectangle){source_x, source_y, source_width, source_height},
        (JanusRaylibVector2){x, y}, unpack_color(tint));
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

void janus_graphics_draw_texture_npatch(
    const void *handle, float source_x, float source_y, float source_width,
    float source_height, int left, int top, int right, int bottom, int layout,
    float destination_x, float destination_y, float destination_width,
    float destination_height, float origin_x, float origin_y, float rotation,
    uint32_t tint) {
  const JanusRaylibTexture *texture = handle;
  if (!graphics_loaded || texture == NULL)
    return;
  JanusRaylibNPatchInfo info = {
      {source_x, source_y, source_width, source_height},
      left,
      top,
      right,
      bottom,
      layout};
  graphics_api.DrawTextureNPatch(
      *texture, info,
      (JanusRaylibRectangle){destination_x, destination_y, destination_width,
                             destination_height},
      (JanusRaylibVector2){origin_x, origin_y}, rotation, unpack_color(tint));
}

void janus_graphics_set_shapes_texture(const void *handle, float source_x,
                                       float source_y, float source_width,
                                       float source_height) {
  const JanusRaylibTexture *texture = handle;
  if (graphics_loaded && texture != NULL)
    graphics_api.SetShapesTexture(
        *texture, (JanusRaylibRectangle){source_x, source_y, source_width,
                                         source_height});
}

void janus_graphics_set_texture_filter(const void *handle, int filter) {
  if (janus_graphics_texture_is_valid(handle))
    graphics_api.SetTextureFilter(*(const JanusRaylibTexture *)handle, filter);
}

void janus_graphics_set_texture_wrap(const void *handle, int wrap) {
  const JanusRaylibTexture *texture = handle;
  if (graphics_loaded && texture != NULL && wrap >= 0 && wrap <= 3)
    graphics_api.SetTextureWrap(*texture, wrap);
}

void janus_graphics_generate_texture_mipmaps(void *handle) {
  JanusRaylibTexture *texture = handle;
  if (graphics_loaded && texture != NULL)
    graphics_api.GenTextureMipmaps(texture);
}

void *janus_graphics_load_render_texture(int width, int height) {
  if (!graphics_loaded || width <= 0 || height <= 0)
    return NULL;
  JanusRaylibRenderTexture *target = malloc(sizeof(*target));
  if (target == NULL)
    return NULL;
  *target = graphics_api.LoadRenderTexture(width, height);
  if (!graphics_api.IsRenderTextureValid(*target)) {
    free(target);
    return NULL;
  }
  return target;
}

bool janus_graphics_render_texture_is_valid(const void *handle) {
  return graphics_loaded && handle != NULL &&
         graphics_api.IsRenderTextureValid(
             *(const JanusRaylibRenderTexture *)handle);
}

int janus_graphics_render_texture_width(const void *handle) {
  return janus_graphics_render_texture_is_valid(handle)
             ? ((const JanusRaylibRenderTexture *)handle)->texture.width
             : 0;
}

int janus_graphics_render_texture_height(const void *handle) {
  return janus_graphics_render_texture_is_valid(handle)
             ? ((const JanusRaylibRenderTexture *)handle)->texture.height
             : 0;
}

void janus_graphics_unload_render_texture(void *handle) {
  if (handle == NULL)
    return;
  if (graphics_loaded) {
    JanusRaylibRenderTexture *target = handle;
    if (graphics_api.IsRenderTextureValid(*target))
      graphics_api.UnloadRenderTexture(*target);
  }
  free(handle);
}

void janus_graphics_begin_render_texture(const void *handle) {
  if (!janus_graphics_render_texture_is_valid(handle) ||
      graphics_render_texture_active)
    return;
  graphics_api.BeginTextureMode(*(const JanusRaylibRenderTexture *)handle);
  graphics_render_texture_active = true;
}

void janus_graphics_end_render_texture(void) {
  if (!graphics_loaded || !graphics_render_texture_active)
    return;
  graphics_api.EndTextureMode();
  graphics_render_texture_active = false;
}

void janus_graphics_draw_render_texture_pro(
    const void *handle, float source_x, float source_y, float source_width,
    float source_height, float destination_x, float destination_y,
    float destination_width, float destination_height, float origin_x,
    float origin_y, float rotation, uint32_t tint) {
  if (!janus_graphics_render_texture_is_valid(handle))
    return;
  const JanusRaylibRenderTexture *target = handle;
  graphics_api.DrawTexturePro(
      target->texture,
      (JanusRaylibRectangle){source_x, source_y, source_width, source_height},
      (JanusRaylibRectangle){destination_x, destination_y, destination_width,
                             destination_height},
      (JanusRaylibVector2){origin_x, origin_y}, rotation, unpack_color(tint));
}

static void *load_shader(const char *vertex_file, const char *fragment_file) {
  if (!graphics_loaded || fragment_file == NULL)
    return NULL;
  JanusRaylibShader *shader = malloc(sizeof(*shader));
  if (shader == NULL)
    return NULL;
  *shader = graphics_api.LoadShader(vertex_file, fragment_file);
  if (!graphics_api.IsShaderValid(*shader)) {
    free(shader);
    return NULL;
  }
  return shader;
}

void *janus_graphics_load_fragment_shader(const void *fragment_file) {
  return load_shader(NULL, (const char *)fragment_file);
}

void *janus_graphics_load_shader(const void *vertex_file,
                                 const void *fragment_file) {
  if (vertex_file == NULL)
    return NULL;
  return load_shader((const char *)vertex_file, (const char *)fragment_file);
}

bool janus_graphics_shader_is_valid(const void *handle) {
  return graphics_loaded && handle != NULL &&
         graphics_api.IsShaderValid(*(const JanusRaylibShader *)handle);
}

void janus_graphics_unload_shader(void *handle) {
  if (handle == NULL)
    return;
  if (graphics_loaded) {
    JanusRaylibShader *shader = handle;
    if (graphics_api.IsShaderValid(*shader))
      graphics_api.UnloadShader(*shader);
  }
  free(handle);
}

void janus_graphics_begin_shader(const void *handle) {
  if (!janus_graphics_shader_is_valid(handle) || graphics_shader_active)
    return;
  graphics_api.BeginShaderMode(*(const JanusRaylibShader *)handle);
  graphics_shader_active = true;
}

void janus_graphics_end_shader(void) {
  if (!graphics_loaded || !graphics_shader_active)
    return;
  graphics_api.EndShaderMode();
  graphics_shader_active = false;
}

int janus_graphics_shader_location(const void *handle, const void *name) {
  if (!janus_graphics_shader_is_valid(handle) || name == NULL)
    return -1;
  return graphics_api.GetShaderLocation(*(const JanusRaylibShader *)handle,
                                        (const char *)name);
}

void janus_graphics_set_shader_float(const void *handle, int location,
                                     float value) {
  if (janus_graphics_shader_is_valid(handle) && location >= 0)
    graphics_api.SetShaderValue(*(const JanusRaylibShader *)handle, location,
                                &value, 0);
}

void janus_graphics_set_shader_vector2(const void *handle, int location,
                                       float x, float y) {
  if (!janus_graphics_shader_is_valid(handle) || location < 0)
    return;
  const float value[2] = {x, y};
  graphics_api.SetShaderValue(*(const JanusRaylibShader *)handle, location,
                              value, 1);
}

void janus_graphics_set_shader_color(const void *handle, int location,
                                     uint32_t color) {
  if (!janus_graphics_shader_is_valid(handle) || location < 0)
    return;
  JanusRaylibColor unpacked = unpack_color(color);
  const float value[4] = {
      (float)unpacked.red / 255.0f, (float)unpacked.green / 255.0f,
      (float)unpacked.blue / 255.0f, (float)unpacked.alpha / 255.0f};
  graphics_api.SetShaderValue(*(const JanusRaylibShader *)handle, location,
                              value, 3);
}

void janus_graphics_set_shader_int(const void *handle, int location,
                                   int value) {
  if (janus_graphics_shader_is_valid(handle) && location >= 0)
    graphics_api.SetShaderValue(*(const JanusRaylibShader *)handle, location,
                                &value, 4);
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
  if (!graphics_api.IsSoundValid(*sound)) {
    free(sound);
    return NULL;
  }
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
  if (!graphics_api.IsMusicValid(*music)) {
    free(music);
    return NULL;
  }
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

bool janus_graphics_is_gamepad_available(int gamepad) {
  return graphics_loaded && graphics_api.IsGamepadAvailable(gamepad);
}

const void *janus_graphics_gamepad_name(int gamepad) {
  return graphics_loaded ? graphics_api.GetGamepadName(gamepad) : NULL;
}

bool janus_graphics_is_gamepad_button_down(int gamepad, int button) {
  return graphics_loaded && graphics_api.IsGamepadButtonDown(gamepad, button);
}

bool janus_graphics_is_gamepad_button_pressed(int gamepad, int button) {
  return graphics_loaded &&
         graphics_api.IsGamepadButtonPressed(gamepad, button);
}

bool janus_graphics_is_gamepad_button_released(int gamepad, int button) {
  return graphics_loaded &&
         graphics_api.IsGamepadButtonReleased(gamepad, button);
}

int janus_graphics_gamepad_button_pressed(void) {
  return graphics_loaded ? graphics_api.GetGamepadButtonPressed() : 0;
}

int janus_graphics_gamepad_axis_count(int gamepad) {
  return graphics_loaded ? graphics_api.GetGamepadAxisCount(gamepad) : 0;
}

float janus_graphics_gamepad_axis(int gamepad, int axis) {
  return graphics_loaded ? graphics_api.GetGamepadAxisMovement(gamepad, axis)
                         : 0.0f;
}

void janus_graphics_set_gamepad_vibration(int gamepad, float left_motor,
                                          float right_motor, float duration) {
  if (graphics_loaded)
    graphics_api.SetGamepadVibration(gamepad, left_motor, right_motor,
                                     duration);
}
