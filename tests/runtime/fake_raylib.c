#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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
  float x;
  float y;
  float width;
  float height;
} Rectangle;
typedef struct {
  Rectangle source;
  int left;
  int top;
  int right;
  int bottom;
  int layout;
} NPatchInfo;

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
  void *data;
  int width;
  int height;
  int mipmaps;
  int format;
} Image;

typedef struct {
  uint32_t id;
  Texture2D texture;
  Texture2D depth;
} RenderTexture2D;

typedef struct {
  uint32_t id;
  int *locations;
} Shader;

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

typedef struct {
  int base_size;
  int glyph_count;
  int glyph_padding;
  Texture2D texture;
  Rectangle *recs;
  void *glyphs;
} Font;

static bool window_ready;
static bool audio_ready;
static bool window_fullscreen;
static bool window_maximized;
static bool cursor_hidden;
static bool blend_active;
static bool drawing_active;
static bool camera_active;
static bool render_texture_active;
static bool shader_active;
static bool scissor_active;
static int font_unloads;
static int texture_unloads;
static int render_texture_unloads;
static int shader_unloads;
static int sound_unloads;
static int music_unloads;

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

RAYLIB_EXPORT void CloseWindow(void) {
  if (blend_active || drawing_active || camera_active || scissor_active ||
      render_texture_active || shader_active || font_unloads != 2 ||
      texture_unloads != 1 || render_texture_unloads != 1 ||
      shader_unloads != 1)
    abort();
  window_ready = false;
}

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

RAYLIB_EXPORT void BeginBlendMode(int mode) {
  if (mode < 0 || mode > 7)
    abort();
  blend_active = true;
}

RAYLIB_EXPORT void EndBlendMode(void) {
  if (!blend_active)
    abort();
  blend_active = false;
}
RAYLIB_EXPORT void BeginScissorMode(int x, int y, int width, int height) {
  (void)x;
  (void)y;
  (void)width;
  (void)height;
  if (scissor_active)
    abort();
  scissor_active = true;
}
RAYLIB_EXPORT void EndScissorMode(void) {
  if (!scissor_active)
    abort();
  scissor_active = false;
}

RAYLIB_EXPORT void BeginDrawing(void) {
  if (drawing_active)
    abort();
  drawing_active = true;
}

RAYLIB_EXPORT void EndDrawing(void) {
  if (!drawing_active || camera_active)
    abort();
  drawing_active = false;
}

RAYLIB_EXPORT void BeginMode2D(Camera2D camera) {
  if (!drawing_active || camera_active)
    abort();
  (void)camera;
  camera_active = true;
}

RAYLIB_EXPORT void EndMode2D(void) {
  if (!camera_active)
    abort();
  camera_active = false;
}

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

RAYLIB_EXPORT void DrawLineEx(Vector2 start, Vector2 end, float thick,
                              Color color) {
  (void)start;
  (void)end;
  (void)thick;
  (void)color;
}
RAYLIB_EXPORT void DrawLineBezier(Vector2 start, Vector2 end, float thick,
                                  Color color) {
  (void)start;
  (void)end;
  (void)thick;
  (void)color;
}
RAYLIB_EXPORT void DrawLineDashed(Vector2 start, Vector2 end, int dash,
                                  int space, Color color) {
  (void)start;
  (void)end;
  (void)dash;
  (void)space;
  (void)color;
}

RAYLIB_EXPORT void DrawCircle(int center_x, int center_y, float radius,
                              Color color) {
  (void)center_x;
  (void)center_y;
  (void)radius;
  (void)color;
}

RAYLIB_EXPORT void DrawCircleGradient(Vector2 center, float radius, Color inner,
                                      Color outer) {
  (void)center;
  (void)radius;
  (void)inner;
  (void)outer;
}
RAYLIB_EXPORT void DrawCircleSector(Vector2 center, float radius, float start,
                                    float end, int segments, Color color) {
  (void)center;
  (void)radius;
  (void)start;
  (void)end;
  (void)segments;
  (void)color;
}
RAYLIB_EXPORT void DrawCircleSectorLines(Vector2 center, float radius,
                                         float start, float end, int segments,
                                         Color color) {
  DrawCircleSector(center, radius, start, end, segments, color);
}
RAYLIB_EXPORT void DrawCircleLines(int x, int y, float radius, Color color) {
  (void)x;
  (void)y;
  (void)radius;
  (void)color;
}
RAYLIB_EXPORT void DrawEllipse(int x, int y, float radius_x, float radius_y,
                               Color color) {
  (void)x;
  (void)y;
  (void)radius_x;
  (void)radius_y;
  (void)color;
}
RAYLIB_EXPORT void DrawEllipseLines(int x, int y, float radius_x,
                                    float radius_y, Color color) {
  DrawEllipse(x, y, radius_x, radius_y, color);
}
RAYLIB_EXPORT void DrawRing(Vector2 center, float inner, float outer,
                            float start, float end, int segments, Color color) {
  (void)center;
  (void)inner;
  (void)outer;
  (void)start;
  (void)end;
  (void)segments;
  (void)color;
}
RAYLIB_EXPORT void DrawRingLines(Vector2 center, float inner, float outer,
                                 float start, float end, int segments,
                                 Color color) {
  DrawRing(center, inner, outer, start, end, segments, color);
}

RAYLIB_EXPORT void DrawRectangle(int x, int y, int width, int height,
                                 Color color) {
  (void)x;
  (void)y;
  (void)width;
  (void)height;
  (void)color;
}

RAYLIB_EXPORT void DrawRectanglePro(Rectangle rec, Vector2 origin,
                                    float rotation, Color color) {
  (void)rec;
  (void)origin;
  (void)rotation;
  (void)color;
}
RAYLIB_EXPORT void DrawRectangleGradientV(int x, int y, int width, int height,
                                          Color first, Color second) {
  (void)x;
  (void)y;
  (void)width;
  (void)height;
  (void)first;
  (void)second;
}
RAYLIB_EXPORT void DrawRectangleGradientH(int x, int y, int width, int height,
                                          Color first, Color second) {
  DrawRectangleGradientV(x, y, width, height, first, second);
}
RAYLIB_EXPORT void DrawRectangleGradientEx(Rectangle rec, Color top_left,
                                           Color bottom_left,
                                           Color bottom_right,
                                           Color top_right) {
  (void)rec;
  (void)top_left;
  (void)bottom_left;
  (void)bottom_right;
  (void)top_right;
}
RAYLIB_EXPORT void DrawRectangleLinesEx(Rectangle rec, float thick,
                                        Color color) {
  (void)rec;
  (void)thick;
  (void)color;
}
RAYLIB_EXPORT void DrawRectangleRounded(Rectangle rec, float roundness,
                                        int segments, Color color) {
  (void)rec;
  (void)roundness;
  (void)segments;
  (void)color;
}
RAYLIB_EXPORT void DrawRectangleRoundedLinesEx(Rectangle rec, float roundness,
                                               int segments, float thick,
                                               Color color) {
  (void)rec;
  (void)roundness;
  (void)segments;
  (void)thick;
  (void)color;
}
RAYLIB_EXPORT void DrawTriangle(Vector2 a, Vector2 b, Vector2 c, Color color) {
  (void)a;
  (void)b;
  (void)c;
  (void)color;
}
RAYLIB_EXPORT void DrawTriangleLines(Vector2 a, Vector2 b, Vector2 c,
                                     Color color) {
  DrawTriangle(a, b, c, color);
}
RAYLIB_EXPORT void DrawPoly(Vector2 center, int sides, float radius,
                            float rotation, Color color) {
  (void)center;
  (void)sides;
  (void)radius;
  (void)rotation;
  (void)color;
}
RAYLIB_EXPORT void DrawPolyLinesEx(Vector2 center, int sides, float radius,
                                   float rotation, float thick, Color color) {
  (void)thick;
  DrawPoly(center, sides, radius, rotation, color);
}
RAYLIB_EXPORT void DrawLineStrip(const Vector2 *points, int count,
                                 Color color) {
  (void)points;
  (void)count;
  (void)color;
}
RAYLIB_EXPORT void DrawTriangleFan(const Vector2 *points, int count,
                                   Color color) {
  (void)points;
  (void)count;
  (void)color;
}
RAYLIB_EXPORT void DrawTriangleStrip(const Vector2 *points, int count,
                                     Color color) {
  (void)points;
  (void)count;
  (void)color;
}
RAYLIB_EXPORT void DrawSplineLinear(const Vector2 *points, int count,
                                    float thick, Color color) {
  (void)points;
  (void)count;
  (void)thick;
  (void)color;
}
RAYLIB_EXPORT void DrawSplineBasis(const Vector2 *points, int count,
                                   float thick, Color color) {
  DrawSplineLinear(points, count, thick, color);
}
RAYLIB_EXPORT void DrawSplineCatmullRom(const Vector2 *points, int count,
                                        float thick, Color color) {
  DrawSplineLinear(points, count, thick, color);
}
RAYLIB_EXPORT void DrawSplineBezierQuadratic(const Vector2 *points, int count,
                                             float thick, Color color) {
  DrawSplineLinear(points, count, thick, color);
}
RAYLIB_EXPORT void DrawSplineBezierCubic(const Vector2 *points, int count,
                                         float thick, Color color) {
  DrawSplineLinear(points, count, thick, color);
}
RAYLIB_EXPORT void DrawSplineSegmentLinear(Vector2 a, Vector2 b, float thick,
                                           Color color) {
  (void)a;
  (void)b;
  (void)thick;
  (void)color;
}
RAYLIB_EXPORT void DrawSplineSegmentBasis(Vector2 a, Vector2 b, Vector2 c,
                                          Vector2 d, float thick, Color color) {
  (void)c;
  (void)d;
  DrawSplineSegmentLinear(a, b, thick, color);
}
RAYLIB_EXPORT void DrawSplineSegmentCatmullRom(Vector2 a, Vector2 b, Vector2 c,
                                               Vector2 d, float thick,
                                               Color color) {
  DrawSplineSegmentBasis(a, b, c, d, thick, color);
}
RAYLIB_EXPORT void DrawSplineSegmentBezierQuadratic(Vector2 a, Vector2 b,
                                                    Vector2 c, float thick,
                                                    Color color) {
  (void)c;
  DrawSplineSegmentLinear(a, b, thick, color);
}
RAYLIB_EXPORT void DrawSplineSegmentBezierCubic(Vector2 a, Vector2 b, Vector2 c,
                                                Vector2 d, float thick,
                                                Color color) {
  DrawSplineSegmentBasis(a, b, c, d, thick, color);
}
RAYLIB_EXPORT Vector2 GetSplinePointLinear(Vector2 a, Vector2 b, float t) {
  Vector2 result = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
  return result;
}
RAYLIB_EXPORT Vector2 GetSplinePointBasis(Vector2 a, Vector2 b, Vector2 c,
                                          Vector2 d, float t) {
  (void)a;
  (void)d;
  return GetSplinePointLinear(b, c, t);
}
RAYLIB_EXPORT Vector2 GetSplinePointCatmullRom(Vector2 a, Vector2 b, Vector2 c,
                                               Vector2 d, float t) {
  return GetSplinePointBasis(a, b, c, d, t);
}
RAYLIB_EXPORT Vector2 GetSplinePointBezierQuad(Vector2 a, Vector2 b, Vector2 c,
                                               float t) {
  Vector2 ab = GetSplinePointLinear(a, b, t);
  Vector2 bc = GetSplinePointLinear(b, c, t);
  return GetSplinePointLinear(ab, bc, t);
}
RAYLIB_EXPORT Vector2 GetSplinePointBezierCubic(Vector2 a, Vector2 b, Vector2 c,
                                                Vector2 d, float t) {
  Vector2 ab = GetSplinePointBezierQuad(a, b, c, t);
  Vector2 bc = GetSplinePointBezierQuad(b, c, d, t);
  return GetSplinePointLinear(ab, bc, t);
}

RAYLIB_EXPORT bool CheckCollisionRecs(Rectangle a, Rectangle b) {
  return a.x < b.x + b.width && a.x + a.width > b.x && a.y < b.y + b.height &&
         a.y + a.height > b.y;
}
RAYLIB_EXPORT bool CheckCollisionCircles(Vector2 a, float radius_a, Vector2 b,
                                         float radius_b) {
  const float x = a.x - b.x;
  const float y = a.y - b.y;
  const float radius = radius_a + radius_b;
  return x * x + y * y <= radius * radius;
}
RAYLIB_EXPORT bool CheckCollisionCircleRec(Vector2 center, float radius,
                                           Rectangle rec) {
  (void)radius;
  return center.x >= rec.x && center.x <= rec.x + rec.width &&
         center.y >= rec.y && center.y <= rec.y + rec.height;
}
RAYLIB_EXPORT bool CheckCollisionCircleLine(Vector2 center, float radius,
                                            Vector2 a, Vector2 b) {
  (void)center;
  (void)radius;
  (void)a;
  (void)b;
  return true;
}
RAYLIB_EXPORT bool CheckCollisionPointRec(Vector2 point, Rectangle rec) {
  return point.x >= rec.x && point.x <= rec.x + rec.width && point.y >= rec.y &&
         point.y <= rec.y + rec.height;
}
RAYLIB_EXPORT bool CheckCollisionPointCircle(Vector2 point, Vector2 center,
                                             float radius) {
  return CheckCollisionCircles(point, 0.0f, center, radius);
}
RAYLIB_EXPORT bool CheckCollisionPointTriangle(Vector2 point, Vector2 a,
                                               Vector2 b, Vector2 c) {
  (void)point;
  (void)a;
  (void)b;
  (void)c;
  return true;
}
RAYLIB_EXPORT bool CheckCollisionPointLine(Vector2 point, Vector2 a, Vector2 b,
                                           int threshold) {
  (void)point;
  (void)a;
  (void)b;
  return threshold >= 0;
}
RAYLIB_EXPORT bool CheckCollisionPointPoly(Vector2 point, const Vector2 *points,
                                           int count) {
  (void)point;
  return points != NULL && count >= 3;
}
RAYLIB_EXPORT bool CheckCollisionLines(Vector2 a1, Vector2 a2, Vector2 b1,
                                       Vector2 b2, Vector2 *collision) {
  (void)a1;
  (void)b1;
  (void)b2;
  if (collision != NULL)
    *collision = a2;
  return true;
}
RAYLIB_EXPORT Rectangle GetCollisionRec(Rectangle a, Rectangle b) {
  Rectangle result = {0};
  if (!CheckCollisionRecs(a, b))
    return result;
  result.x = a.x > b.x ? a.x : b.x;
  result.y = a.y > b.y ? a.y : b.y;
  const float right_a = a.x + a.width;
  const float right_b = b.x + b.width;
  const float bottom_a = a.y + a.height;
  const float bottom_b = b.y + b.height;
  result.width = (right_a < right_b ? right_a : right_b) - result.x;
  result.height = (bottom_a < bottom_b ? bottom_a : bottom_b) - result.y;
  return result;
}

RAYLIB_EXPORT void DrawText(const char *text, int x, int y, int font_size,
                            Color color) {
  (void)text;
  (void)x;
  (void)y;
  (void)font_size;
  (void)color;
}

RAYLIB_EXPORT Font LoadFontEx(const char *file_name, int font_size,
                              int *codepoints, int codepoint_count) {
  (void)codepoints;
  (void)codepoint_count;
  Font font = {font_size, 95, 0, {2, 256, 256, 1, 7}, 0, 0};
  if (file_name == 0 || strcmp(file_name, "missing-font.ttf") == 0)
    font.texture.id = 0;
  return font;
}

RAYLIB_EXPORT int *LoadCodepoints(const char *text, int *count) {
  if (text == 0 || count == 0)
    return 0;
  *count = 3;
  int *codepoints = malloc(sizeof(int) * 3);
  if (codepoints != 0) {
    codepoints[0] = 'A';
    codepoints[1] = 0x00e9;
    codepoints[2] = 0x4e16;
  }
  return codepoints;
}

RAYLIB_EXPORT void UnloadCodepoints(int *codepoints) { free(codepoints); }

RAYLIB_EXPORT bool IsFontValid(Font font) { return font.texture.id != 0; }

RAYLIB_EXPORT void UnloadFont(Font font) {
  if (!IsFontValid(font))
    abort();
  ++font_unloads;
}

RAYLIB_EXPORT void DrawTextEx(Font font, const char *text, Vector2 position,
                              float font_size, float spacing, Color tint) {
  (void)font;
  (void)text;
  (void)position;
  (void)font_size;
  (void)spacing;
  (void)tint;
}

RAYLIB_EXPORT Vector2 MeasureTextEx(Font font, const char *text,
                                    float font_size, float spacing) {
  (void)font;
  (void)text;
  (void)spacing;
  return (Vector2){font_size * 4.0f, font_size};
}

static Image fake_image(int width, int height) {
  Image image = {0, width, height, 1, 7};
  if (width > 0 && height > 0)
    image.data = malloc(1);
  return image;
}
RAYLIB_EXPORT Image LoadImage(const char *file_name) {
  if (file_name == NULL || strcmp(file_name, "missing-image.png") == 0)
    return (Image){0};
  return fake_image(64, 32);
}
RAYLIB_EXPORT Image LoadImageRaw(const char *file_name, int width, int height,
                                 int format, int header_size) {
  (void)format;
  (void)header_size;
  return file_name != NULL ? fake_image(width, height) : (Image){0};
}
RAYLIB_EXPORT Image LoadImageFromMemory(const char *file_type,
                                        const unsigned char *data,
                                        int data_size) {
  return file_type != NULL && data != NULL && data_size > 0 ? fake_image(8, 8)
                                                            : (Image){0};
}
RAYLIB_EXPORT Image LoadImageFromTexture(Texture2D texture) {
  return texture.id != 0 ? fake_image(texture.width, texture.height)
                         : (Image){0};
}
RAYLIB_EXPORT Image LoadImageFromScreen(void) { return fake_image(800, 450); }
RAYLIB_EXPORT bool IsImageValid(Image image) { return image.data != NULL; }
RAYLIB_EXPORT void UnloadImage(Image image) { free(image.data); }
RAYLIB_EXPORT bool ExportImage(Image image, const char *file_name) {
  return IsImageValid(image) && file_name != NULL;
}
RAYLIB_EXPORT bool ExportImageAsCode(Image image, const char *file_name) {
  return ExportImage(image, file_name);
}
RAYLIB_EXPORT Image GenImageColor(int width, int height, Color color) {
  (void)color;
  return fake_image(width, height);
}
RAYLIB_EXPORT Image GenImageGradientLinear(int width, int height, int direction,
                                           Color start, Color end) {
  (void)direction;
  (void)start;
  (void)end;
  return fake_image(width, height);
}
RAYLIB_EXPORT Image GenImageGradientRadial(int width, int height, float density,
                                           Color inner, Color outer) {
  (void)density;
  (void)inner;
  (void)outer;
  return fake_image(width, height);
}
RAYLIB_EXPORT Image GenImageGradientSquare(int width, int height, float density,
                                           Color inner, Color outer) {
  return GenImageGradientRadial(width, height, density, inner, outer);
}
RAYLIB_EXPORT Image GenImageChecked(int width, int height, int checks_x,
                                    int checks_y, Color first, Color second) {
  (void)checks_x;
  (void)checks_y;
  (void)first;
  (void)second;
  return fake_image(width, height);
}
RAYLIB_EXPORT Image GenImageWhiteNoise(int width, int height, float factor) {
  (void)factor;
  return fake_image(width, height);
}
RAYLIB_EXPORT Image GenImagePerlinNoise(int width, int height, int offset_x,
                                        int offset_y, float scale) {
  (void)offset_x;
  (void)offset_y;
  (void)scale;
  return fake_image(width, height);
}
RAYLIB_EXPORT Image GenImageCellular(int width, int height, int tile_size) {
  (void)tile_size;
  return fake_image(width, height);
}
RAYLIB_EXPORT Image GenImageText(int width, int height, const char *text) {
  if (text == NULL)
    return (Image){0};
  return fake_image(width, height);
}
RAYLIB_EXPORT Image ImageCopy(Image image) {
  return IsImageValid(image) ? fake_image(image.width, image.height)
                             : (Image){0};
}
RAYLIB_EXPORT Image ImageFromImage(Image image, Rectangle rec) {
  return IsImageValid(image) ? fake_image((int)rec.width, (int)rec.height)
                             : (Image){0};
}
RAYLIB_EXPORT Image ImageFromChannel(Image image, int channel) {
  Image result = ImageCopy(image);
  result.format = 1;
  (void)channel;
  return result;
}
RAYLIB_EXPORT Image ImageText(const char *text, int font_size, Color color) {
  (void)color;
  return text != NULL ? fake_image(font_size * 4, font_size) : (Image){0};
}
RAYLIB_EXPORT void ImageFormat(Image *image, int format) {
  image->format = format;
}
RAYLIB_EXPORT void ImageToPOT(Image *image, Color fill) {
  (void)image;
  (void)fill;
}
RAYLIB_EXPORT void ImageCrop(Image *image, Rectangle crop) {
  image->width = (int)crop.width;
  image->height = (int)crop.height;
}
RAYLIB_EXPORT void ImageAlphaCrop(Image *image, float threshold) {
  (void)image;
  (void)threshold;
}
RAYLIB_EXPORT void ImageAlphaClear(Image *image, Color color, float threshold) {
  (void)image;
  (void)color;
  (void)threshold;
}
RAYLIB_EXPORT void ImageAlphaMask(Image *image, Image mask) {
  (void)image;
  (void)mask;
}
RAYLIB_EXPORT void ImageAlphaPremultiply(Image *image) { (void)image; }
RAYLIB_EXPORT void ImageBlurGaussian(Image *image, int size) {
  (void)image;
  (void)size;
}
RAYLIB_EXPORT void ImageKernelConvolution(Image *image, const float *kernel,
                                          int kernel_size) {
  (void)image;
  (void)kernel;
  (void)kernel_size;
}
RAYLIB_EXPORT void ImageResize(Image *image, int width, int height) {
  image->width = width;
  image->height = height;
}
RAYLIB_EXPORT void ImageResizeNN(Image *image, int width, int height) {
  ImageResize(image, width, height);
}
RAYLIB_EXPORT void ImageResizeCanvas(Image *image, int width, int height,
                                     int offset_x, int offset_y, Color fill) {
  (void)offset_x;
  (void)offset_y;
  (void)fill;
  ImageResize(image, width, height);
}
RAYLIB_EXPORT void ImageMipmaps(Image *image) { image->mipmaps = 4; }
RAYLIB_EXPORT void ImageDither(Image *image, int r, int g, int b, int a) {
  (void)image;
  (void)r;
  (void)g;
  (void)b;
  (void)a;
}
RAYLIB_EXPORT void ImageFlipVertical(Image *image) { (void)image; }
RAYLIB_EXPORT void ImageFlipHorizontal(Image *image) { (void)image; }
RAYLIB_EXPORT void ImageRotate(Image *image, int degrees) {
  (void)image;
  (void)degrees;
}
RAYLIB_EXPORT void ImageRotateCW(Image *image) {
  int width = image->width;
  image->width = image->height;
  image->height = width;
}
RAYLIB_EXPORT void ImageRotateCCW(Image *image) { ImageRotateCW(image); }
RAYLIB_EXPORT void ImageColorTint(Image *image, Color color) {
  (void)image;
  (void)color;
}
RAYLIB_EXPORT void ImageColorInvert(Image *image) { (void)image; }
RAYLIB_EXPORT void ImageColorGrayscale(Image *image) { (void)image; }
RAYLIB_EXPORT void ImageColorContrast(Image *image, float contrast) {
  (void)image;
  (void)contrast;
}
RAYLIB_EXPORT void ImageColorBrightness(Image *image, int brightness) {
  (void)image;
  (void)brightness;
}
RAYLIB_EXPORT void ImageColorReplace(Image *image, Color color, Color replace) {
  (void)image;
  (void)color;
  (void)replace;
}
RAYLIB_EXPORT Rectangle GetImageAlphaBorder(Image image, float threshold) {
  (void)threshold;
  return (Rectangle){0, 0, (float)image.width, (float)image.height};
}
RAYLIB_EXPORT Color GetImageColor(Image image, int x, int y) {
  (void)image;
  (void)x;
  (void)y;
  return (Color){1, 2, 3, 255};
}
RAYLIB_EXPORT void ImageClearBackground(Image *image, Color color) {
  (void)image;
  (void)color;
}
RAYLIB_EXPORT void ImageDrawPixel(Image *image, int x, int y, Color color) {
  (void)image;
  (void)x;
  (void)y;
  (void)color;
}
RAYLIB_EXPORT void ImageDrawLineEx(Image *image, Vector2 start, Vector2 end,
                                   int thick, Color color) {
  (void)image;
  (void)start;
  (void)end;
  (void)thick;
  (void)color;
}
RAYLIB_EXPORT void ImageDrawCircle(Image *image, int x, int y, int radius,
                                   Color color) {
  (void)image;
  (void)x;
  (void)y;
  (void)radius;
  (void)color;
}
RAYLIB_EXPORT void ImageDrawCircleLines(Image *image, int x, int y, int radius,
                                        Color color) {
  ImageDrawCircle(image, x, y, radius, color);
}
RAYLIB_EXPORT void ImageDrawRectangleRec(Image *image, Rectangle rec,
                                         Color color) {
  (void)image;
  (void)rec;
  (void)color;
}
RAYLIB_EXPORT void ImageDrawRectangleLines(Image *image, Rectangle rec,
                                           int thick, Color color) {
  (void)thick;
  ImageDrawRectangleRec(image, rec, color);
}
RAYLIB_EXPORT void ImageDrawTriangle(Image *image, Vector2 a, Vector2 b,
                                     Vector2 c, Color color) {
  (void)image;
  (void)a;
  (void)b;
  (void)c;
  (void)color;
}
RAYLIB_EXPORT void ImageDrawTriangleEx(Image *image, Vector2 a, Vector2 b,
                                       Vector2 c, Color first, Color second,
                                       Color third) {
  (void)second;
  (void)third;
  ImageDrawTriangle(image, a, b, c, first);
}
RAYLIB_EXPORT void ImageDrawTriangleLines(Image *image, Vector2 a, Vector2 b,
                                          Vector2 c, Color color) {
  ImageDrawTriangle(image, a, b, c, color);
}
RAYLIB_EXPORT void ImageDrawTriangleFan(Image *image, const Vector2 *points,
                                        int count, Color color) {
  (void)image;
  (void)points;
  (void)count;
  (void)color;
}
RAYLIB_EXPORT void ImageDrawTriangleStrip(Image *image, const Vector2 *points,
                                          int count, Color color) {
  ImageDrawTriangleFan(image, points, count, color);
}
RAYLIB_EXPORT void ImageDraw(Image *image, Image source, Rectangle source_rec,
                             Rectangle destination, Color tint) {
  (void)image;
  (void)source;
  (void)source_rec;
  (void)destination;
  (void)tint;
}
RAYLIB_EXPORT void ImageDrawText(Image *image, const char *text, int x, int y,
                                 int font_size, Color color) {
  (void)image;
  (void)text;
  (void)x;
  (void)y;
  (void)font_size;
  (void)color;
}

RAYLIB_EXPORT Texture2D LoadTexture(const char *file_name) {
  Texture2D texture = {1, 64, 32, 1, 7};
  if (file_name == 0 || strcmp(file_name, "missing-texture.png") == 0)
    texture.id = 0;
  return texture;
}
RAYLIB_EXPORT Texture2D LoadTextureFromImage(Image image) {
  Texture2D texture = {2, image.width, image.height, image.mipmaps,
                       image.format};
  if (!IsImageValid(image))
    texture.id = 0;
  return texture;
}

RAYLIB_EXPORT bool IsTextureValid(Texture2D texture) { return texture.id != 0; }

RAYLIB_EXPORT void UnloadTexture(Texture2D texture) {
  if (!IsTextureValid(texture))
    abort();
  ++texture_unloads;
}

RAYLIB_EXPORT void DrawTexture(Texture2D texture, int x, int y, Color tint) {
  (void)texture;
  (void)x;
  (void)y;
  (void)tint;
}
RAYLIB_EXPORT void DrawTextureV(Texture2D texture, Vector2 position,
                                Color tint) {
  DrawTexture(texture, (int)position.x, (int)position.y, tint);
}
RAYLIB_EXPORT void DrawTextureEx(Texture2D texture, Vector2 position,
                                 float rotation, float scale, Color tint) {
  (void)rotation;
  (void)scale;
  DrawTextureV(texture, position, tint);
}
RAYLIB_EXPORT void DrawTextureRec(Texture2D texture, Rectangle source,
                                  Vector2 position, Color tint) {
  (void)source;
  DrawTextureV(texture, position, tint);
}

RAYLIB_EXPORT void DrawTexturePro(Texture2D texture, Rectangle source,
                                  Rectangle destination, Vector2 origin,
                                  float rotation, Color tint) {
  (void)texture;
  (void)source;
  (void)destination;
  (void)origin;
  (void)rotation;
  (void)tint;
}
RAYLIB_EXPORT void DrawTextureNPatch(Texture2D texture, NPatchInfo info,
                                     Rectangle destination, Vector2 origin,
                                     float rotation, Color tint) {
  (void)info;
  DrawTexturePro(texture, info.source, destination, origin, rotation, tint);
}
RAYLIB_EXPORT void SetShapesTexture(Texture2D texture, Rectangle source) {
  (void)texture;
  (void)source;
}

RAYLIB_EXPORT void SetTextureFilter(Texture2D texture, int filter) {
  (void)texture;
  (void)filter;
}
RAYLIB_EXPORT void SetTextureWrap(Texture2D texture, int wrap) {
  (void)texture;
  if (wrap < 0 || wrap > 3)
    abort();
}
RAYLIB_EXPORT void GenTextureMipmaps(Texture2D *texture) {
  if (texture != 0)
    texture->mipmaps = 4;
}
RAYLIB_EXPORT void UpdateTexture(Texture2D texture, const void *pixels) {
  (void)texture;
  (void)pixels;
}
RAYLIB_EXPORT void UpdateTextureRec(Texture2D texture, Rectangle rec,
                                    const void *pixels) {
  (void)rec;
  UpdateTexture(texture, pixels);
}

RAYLIB_EXPORT RenderTexture2D LoadRenderTexture(int width, int height) {
  return (RenderTexture2D){
      3, {4, width, height, 1, 7}, {5, width, height, 1, 19}};
}

RAYLIB_EXPORT bool IsRenderTextureValid(RenderTexture2D target) {
  return target.id != 0;
}

RAYLIB_EXPORT void UnloadRenderTexture(RenderTexture2D target) {
  if (!IsRenderTextureValid(target))
    abort();
  ++render_texture_unloads;
}

RAYLIB_EXPORT void BeginTextureMode(RenderTexture2D target) {
  if (!IsRenderTextureValid(target) || render_texture_active)
    abort();
  render_texture_active = true;
}

RAYLIB_EXPORT void EndTextureMode(void) {
  if (!render_texture_active)
    abort();
  render_texture_active = false;
}

RAYLIB_EXPORT Shader LoadShader(const char *vertex_file,
                                const char *fragment_file) {
  (void)vertex_file;
  return (Shader){fragment_file == 0 ||
                          strcmp(fragment_file, "missing-shader.fs") == 0
                      ? 0u
                      : 6u,
                  0};
}

RAYLIB_EXPORT bool IsShaderValid(Shader shader) { return shader.id != 0; }

RAYLIB_EXPORT void UnloadShader(Shader shader) {
  if (!IsShaderValid(shader))
    abort();
  ++shader_unloads;
}

RAYLIB_EXPORT void BeginShaderMode(Shader shader) {
  if (!IsShaderValid(shader) || shader_active)
    abort();
  shader_active = true;
}

RAYLIB_EXPORT void EndShaderMode(void) {
  if (!shader_active)
    abort();
  shader_active = false;
}

RAYLIB_EXPORT int GetShaderLocation(Shader shader, const char *name) {
  return shader.id != 0 && name != 0 ? 7 : -1;
}

RAYLIB_EXPORT void SetShaderValue(Shader shader, int location,
                                  const void *value, int uniform_type) {
  (void)shader;
  (void)location;
  (void)value;
  (void)uniform_type;
}

RAYLIB_EXPORT void InitAudioDevice(void) { audio_ready = true; }

RAYLIB_EXPORT void CloseAudioDevice(void) {
  if (sound_unloads != 1 || music_unloads != 1)
    abort();
  audio_ready = false;
}

RAYLIB_EXPORT bool IsAudioDeviceReady(void) { return audio_ready; }

RAYLIB_EXPORT void SetMasterVolume(float volume) { (void)volume; }

RAYLIB_EXPORT Sound LoadSound(const char *file_name) {
  Sound sound = {{(void *)1, 0, 44100, 16, 2}, 128};
  if (file_name == 0 || strcmp(file_name, "missing-sound.wav") == 0)
    sound.stream.buffer = 0;
  return sound;
}

RAYLIB_EXPORT bool IsSoundValid(Sound sound) {
  return sound.stream.buffer != 0;
}

RAYLIB_EXPORT void UnloadSound(Sound sound) {
  if (!IsSoundValid(sound))
    abort();
  ++sound_unloads;
}

RAYLIB_EXPORT void PlaySound(Sound sound) { (void)sound; }

RAYLIB_EXPORT void StopSound(Sound sound) { (void)sound; }

RAYLIB_EXPORT bool IsSoundPlaying(Sound sound) { return IsSoundValid(sound); }

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
  if (file_name == 0 || strcmp(file_name, "missing-music.ogg") == 0)
    music.context_data = 0;
  return music;
}

RAYLIB_EXPORT bool IsMusicValid(Music music) { return music.context_data != 0; }

RAYLIB_EXPORT void UnloadMusicStream(Music music) {
  if (!IsMusicValid(music))
    abort();
  ++music_unloads;
}

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

RAYLIB_EXPORT int GetCharPressed(void) { return 233; }

RAYLIB_EXPORT void SetExitKey(int key) { (void)key; }

RAYLIB_EXPORT int GetMouseX(void) { return 123; }

RAYLIB_EXPORT int GetMouseY(void) { return 234; }

RAYLIB_EXPORT void SetMousePosition(int x, int y) {
  (void)x;
  (void)y;
}

RAYLIB_EXPORT void SetMouseCursor(int cursor) { (void)cursor; }

RAYLIB_EXPORT float GetMouseWheelMove(void) { return 1.5f; }

RAYLIB_EXPORT bool IsMouseButtonDown(int button) { return button == 0; }

RAYLIB_EXPORT bool IsMouseButtonPressed(int button) { return button == 1; }

RAYLIB_EXPORT void ShowCursor(void) { cursor_hidden = false; }
RAYLIB_EXPORT void HideCursor(void) { cursor_hidden = true; }
RAYLIB_EXPORT bool IsCursorHidden(void) { return cursor_hidden; }
RAYLIB_EXPORT void EnableCursor(void) { cursor_hidden = false; }
RAYLIB_EXPORT void DisableCursor(void) { cursor_hidden = true; }

RAYLIB_EXPORT bool IsGamepadAvailable(int gamepad) { return gamepad == 0; }

RAYLIB_EXPORT const char *GetGamepadName(int gamepad) {
  return gamepad == 0 ? "Fake Gamepad" : 0;
}

RAYLIB_EXPORT bool IsGamepadButtonDown(int gamepad, int button) {
  return gamepad == 0 && button == 7;
}

RAYLIB_EXPORT bool IsGamepadButtonPressed(int gamepad, int button) {
  return gamepad == 0 && button == 8;
}

RAYLIB_EXPORT bool IsGamepadButtonReleased(int gamepad, int button) {
  return gamepad == 0 && button == 9;
}

RAYLIB_EXPORT int GetGamepadButtonPressed(void) { return 8; }

RAYLIB_EXPORT int GetGamepadAxisCount(int gamepad) {
  return gamepad == 0 ? 6 : 0;
}

RAYLIB_EXPORT float GetGamepadAxisMovement(int gamepad, int axis) {
  return gamepad == 0 && axis == 0 ? 0.75f : 0.0f;
}

RAYLIB_EXPORT void SetGamepadVibration(int gamepad, float left_motor,
                                       float right_motor, float duration) {
  (void)gamepad;
  (void)left_motor;
  (void)right_motor;
  (void)duration;
}
