#include "janus/backend/llvm/ir_generator.hpp"
#include "janus/diagnostics/compile_error.hpp"
#include "janus/frontend/module_loader.hpp"
#include "janus/semantic/analyzer.hpp"

#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/raw_ostream.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
  }
}

} // namespace

int main() {
  constexpr std::string_view source = R"(
import std.graphics

def main() : int {
    val color : Color = rgba(18, 52, 86, 120)
    val typedColor : Color = colorRgba(18, 52, 86, 120)
    val start : Vector2 = vector2(float(5.0), float(6.0))
    val end : Vector2 = vector2(float(10.0), float(12.0))
    val points : Ptr[Vector2] = alloc[Vector2](usize(4))
    points.store(usize(0), start)
    points.store(usize(1), end)
    points.store(usize(2), vector2(float(20.0), float(18.0)))
    points.store(usize(3), vector2(float(25.0), float(5.0)))
    val kernel : Ptr[float] = alloc[float](usize(9))
    kernel.store(usize(4), float(1.0))
    val area : Rectangle = rectangle(
        float(8.0),
        float(9.0),
        float(10.0),
        float(11.0)
    )
    val camera : Camera2D = new Camera2D(
        float(400.0),
        float(225.0),
        float(0.0),
        float(0.0),
        float(0.0),
        float(1.0)
    )
    val world : Vector2 = screenToWorld(start, camera)
    val screen : Vector2 = worldToScreen(world, camera)
    val keyDown : bool = isKeyDown(Key.Left)
    val mouseDown : bool = isMouseButtonDown(MouseButton.Left)
    setWindowTitle("Janus graphics")
    setWindowPosition(10, 20)
    setWindowSize(800, 450)
    setWindowOpacity(float(0.9))
    setMousePosition(100, 120)
    hideCursor()
    showCursor()
    val gamepadReady : bool = isGamepadAvailable(0)
    val gamepadDown : bool = isGamepadButtonDown(
        0,
        GamepadButton.RightFaceDown
    )
    val leftX : float = gamepadAxis(0, GamepadAxis.LeftX)
    setGamepadVibration(0, float(0.5), float(0.5), float(0.1))
    val frameDuration : Duration = frameTime()
    val totalDuration : Duration = elapsedTime()
    beginDrawing()
    beginScissor(area)
    endScissor()
    beginBlend(BlendMode.Additive)
    defer endBlend()
    beginCamera(camera)
    clearBackground(Black)
    drawPixel(1, 2, White)
    drawLine(1, 2, 3, 4, Red)
    drawCircle(5, 6, float(7.0), Green)
    drawRectangle(8, 9, 10, 11, Blue)
    drawText("Janus", 12, 13, 14, color)
    drawLineBetween(start, end, typedColor)
    drawCircleAt(start, float(7.0), typedColor)
    drawRectangleArea(area, typedColor)
    drawTextAt("typed", start, 14, typedColor)
    drawLineEx(start, end, float(2.0), typedColor)
    drawLineBezier(start, end, float(2.0), typedColor)
    drawLineDashed(start, end, 4, 2, typedColor)
    drawCircleGradient(start, float(8.0), Red, Blue)
    drawCircleSector(start, float(8.0), float(0.0), float(90.0), 12, typedColor)
    drawCircleSectorLines(start, float(8.0), float(0.0), float(90.0), 12, typedColor)
    drawCircleLines(start, float(8.0), typedColor)
    drawEllipse(start, float(8.0), float(4.0), typedColor)
    drawEllipseLines(start, float(8.0), float(4.0), typedColor)
    drawRing(start, float(4.0), float(8.0), float(0.0), float(180.0), 16, typedColor)
    drawRingLines(start, float(4.0), float(8.0), float(0.0), float(180.0), 16, typedColor)
    drawRectanglePro(area, start, float(15.0), typedColor)
    drawRectangleGradientVertical(area, Red, Blue)
    drawRectangleGradientHorizontal(area, Red, Blue)
    drawRectangleGradient(area, Red, Green, Blue, White)
    drawRectangleLines(area, float(2.0), typedColor)
    drawRectangleRounded(area, float(0.25), 8, typedColor)
    drawRectangleRoundedLines(area, float(0.25), 8, float(2.0), typedColor)
    drawTriangle(start, end, vector2(float(7.0), float(20.0)), typedColor)
    drawTriangleLines(start, end, vector2(float(7.0), float(20.0)), typedColor)
    drawPolygon(start, 6, float(10.0), float(0.0), typedColor)
    drawPolygonLines(start, 6, float(10.0), float(0.0), float(2.0), typedColor)
    drawLineStrip(points, 4, typedColor)
    drawTriangleFan(points, 4, typedColor)
    drawTriangleStrip(points, 4, typedColor)
    drawSplineLinear(points, 4, float(2.0), typedColor)
    drawSplineBasis(points, 4, float(2.0), typedColor)
    drawSplineCatmullRom(points, 4, float(2.0), typedColor)
    drawSplineBezierQuadratic(points, 4, float(2.0), typedColor)
    drawSplineBezierCubic(points, 4, float(2.0), typedColor)
    drawSplineSegmentLinear(start, end, float(2.0), typedColor)
    drawSplineSegmentBasis(start, end, start, end, float(2.0), typedColor)
    drawSplineSegmentCatmullRom(start, end, start, end, float(2.0), typedColor)
    drawSplineSegmentBezierQuadratic(start, end, start, float(2.0), typedColor)
    drawSplineSegmentBezierCubic(start, end, start, end, float(2.0), typedColor)
    val linearPoint : Vector2 = splinePointLinear(start, end, float(0.5))
    val basisPoint : Vector2 = splinePointBasis(start, end, start, end, float(0.5))
    val catmullPoint : Vector2 = splinePointCatmullRom(start, end, start, end, float(0.5))
    val quadPoint : Vector2 = splinePointBezierQuadratic(start, end, start, float(0.5))
    val cubicPoint : Vector2 = splinePointBezierCubic(start, end, start, end, float(0.5))
    val overlaps : bool = collisionRectangles(area, area)
    val circlesOverlap : bool = collisionCircles(start, float(3.0), end, float(4.0))
    disableExitKey()
    val circleArea : bool = collisionCircleRectangle(start, float(3.0), area)
    val circleLine : bool = collisionCircleLine(start, float(3.0), start, end)
    val pointArea : bool = collisionPointRectangle(start, area)
    val pointCircle : bool = collisionPointCircle(start, end, float(4.0))
    val pointTriangle : bool = collisionPointTriangle(start, start, end, vector2(float(7.0), float(20.0)))
    val pointLine : bool = collisionPointLine(start, start, end, 2)
    val pointPolygon : bool = collisionPointPolygon(start, points, 4)
    val linesCross : bool = collisionLines(start, end, end, start)
    val lineCrossing : Vector2 = collisionLinesPoint(start, end, end, start)
    val overlapArea : Rectangle = collisionRectangle(area, area)
    val font : Font = loadFont("font.ttf", 24)
    val textSize : Vector2 = font.measure(
        "Hé Janus",
        float(24.0),
        float(1.0)
    )
    font.draw(
        "Hé Janus",
        textSize,
        float(24.0),
        float(1.0),
        typedColor
    )
    delete font
    val unicodeFont : Font = loadFontUtf8(
        "font.ttf",
        24,
        "Hé 世界"
    )
    delete unicodeFont
    val target : RenderTexture = loadRenderTexture(320, 180)
    target.begin()
    clearBackground(black())
    endRenderTexture()
    val shader : Shader = loadFragmentShader("post.fs")
    val timeLocation : int = shader.location("time")
    shader.setFloat(timeLocation, float(1.0))
    shader.setVector2(timeLocation, start)
    shader.setColor(timeLocation, typedColor)
    shader.setInt(timeLocation, 2)
    shader.begin()
    target.drawPro(
        rectangle(float(0.0), float(0.0), float(320.0), float(-180.0)),
        rectangle(float(0.0), float(0.0), float(640.0), float(360.0)),
        vector2(float(0.0), float(0.0)),
        float(0.0),
        typedColor
    )
    endShader()
    delete shader
    delete target
    val texture : Texture = loadTexture("sprite.png")
    texture.setFilter(TextureFilter.Point)
    texture.setWrap(TextureWrap.Repeat)
    texture.generateMipmaps()
    val textureWidth : int = texture.width()
    texture.draw(textureWidth, texture.height(), white())
    texture.drawAt(start, typedColor)
    texture.drawEx(start, float(15.0), float(2.0), typedColor)
    texture.drawRegion(area, start, typedColor)
    texture.useForShapes(area)
    val patch : NPatchInfo = new NPatchInfo(
        area, 2, 2, 2, 2, NPatchLayout.NinePatch
    )
    texture.drawNPatch(patch, area, start, float(0.0), typedColor)
    val animation : SpriteAnimation = new SpriteAnimation(
        texture,
        16,
        16,
        4,
        8,
        0
    )
    animation.draw(
        start,
        float(2.0),
        float(15.0),
        true,
        false,
        typedColor
    )
    val nextFrame : int = animation.advance()
    delete animation
    val image : Image = generateImageChecked(32, 32, 4, 4, White, Black)
    image.convert(PixelFormat.UncompressedR8G8B8A8)
    image.drawLine(start, end, 2, Red)
    image.drawCircle(start, 4, Green)
    image.drawRectangle(area, Blue)
    image.drawTriangleColors(start, end, start, Red, Green, Blue)
    image.drawTriangleFan(points, 4, White)
    image.drawTriangleStrip(points, 4, White)
    image.drawText("CPU", 1, 2, 12, White)
    image.convolve(kernel, 3)
    image.flipHorizontal()
    image.rotateClockwise()
    image.generateMipmaps()
    val pixel : Color = image.colorAt(0, 0)
    val imageArea : Rectangle = image.alphaBorder(float(0.1))
    val imageCopy : Image = image.copy()
    val redChannel : Image = image.channel(0)
    val imageTexture : Texture = image.toTexture()
    delete imageTexture
    delete redChannel
    delete imageCopy
    delete image
    endCamera()
    delete texture
    val sound : Sound = loadSound("effect.wav")
    sound.setVolume(float(0.5))
    sound.play()
    val music : Music = loadMusic("music.ogg")
    music.play()
    music.update()
    delete sound
    delete music
    delete camera
    endDrawing()
    free(kernel)
    free(points)
    if keyDown || mouseDown || gamepadReady || gamepadDown || overlaps ||
    circlesOverlap || circleArea || circleLine || pointArea || pointCircle ||
    pointTriangle || pointLine || pointPolygon || linesCross ||
    lineCrossing.x + linearPoint.x + basisPoint.x + catmullPoint.x +
    quadPoint.x + cubicPoint.x + imageArea.width + float(pixel.packed()) +
    overlapArea.width > float(0.0) {
        return mouseX() + mouseY() + screenWidth() + screenHeight() +
            keyPressed() + characterPressed()
    }
    return 0
}
)";

  janus::frontend::ModuleLoader loader{
      {std::filesystem::path{JANUS_STDLIB_DIR}}};
  const janus::ast::Program program =
      loader.load(std::filesystem::path{"graphics_test/main.janus"}, source);

  janus::semantic::Analyzer analyzer;
  static_cast<void>(analyzer.analyze(program));

  try {
    const janus::ast::Program private_program =
        loader.load(std::filesystem::path{"graphics_private_test/main.janus"},
                    "import std.graphics "
                    "def main() : int { "
                    "return if std.graphics.drawing.janus_graphics_available() "
                    "{ 1 } else { 0 } "
                    "}");
    static_cast<void>(analyzer.analyze(private_program));
    expect(false, "graphics native primitives must remain module-private");
  } catch (const janus::CompileError &error) {
    expect(std::string_view{error.what()}.find(
               "function 'std.graphics.drawing.janus_graphics_available' "
               "is private") != std::string_view::npos,
           "graphics native primitive access reports private visibility");
  }

  llvm::LLVMContext context;
  janus::backend::llvm::IrGenerator generator{context};
  const std::unique_ptr<llvm::Module> module =
      generator.generate(program, "graphics_module");
  std::string ir;
  llvm::raw_string_ostream output{ir};
  module->print(output, nullptr);
  output.flush();

  expect(ir.find("declare i32 @janus_graphics_rgba(i8, i8, i8, i8)") !=
             std::string::npos,
         "graphics colors use the native RGBA helper");
  expect(ir.find("@__janus_global_std_graphics_types__Black = constant "
                 "%struct.Color { i32 255 }") != std::string::npos &&
             ir.find("@__janus_global_std_graphics_types__White = constant "
                     "%struct.Color { i32 -1 }") != std::string::npos &&
             ir.find("@__janus_global_std_graphics_types__Red = constant "
                     "%struct.Color { i32 -433506305 }") != std::string::npos &&
             ir.find("@__janus_global_std_graphics_types__Green = constant "
                     "%struct.Color { i32 14954751 }") != std::string::npos &&
             ir.find("@__janus_global_std_graphics_types__Blue = constant "
                     "%struct.Color { i32 7991807 }") != std::string::npos,
         "graphics exposes statically initialized global color values");
  expect(ir.find("call void @janus_graphics_draw_circle") != std::string::npos,
         "graphics circles lower through the native backend");
  expect(ir.find("call void @janus_graphics_draw_text") != std::string::npos,
         "graphics text lowers through the native backend");
  expect(ir.find("call ptr @janus_graphics_load_texture") !=
                 std::string::npos &&
             ir.find("call void @janus_graphics_unload_texture") !=
                 std::string::npos,
         "graphics textures load and unload through owned handles");
  expect(ir.find("call ptr @janus_graphics_load_sound") != std::string::npos &&
             ir.find("call ptr @janus_graphics_load_music") !=
                 std::string::npos,
         "graphics audio resources lower through owned native handles");
  expect(ir.find("call i1 @isKeyDown(%enum.Key { i32 263 })") !=
             std::string::npos,
         "graphics keys retain their raylib-compatible code");
  expect(ir.find("call void @janus_graphics_set_window_title") !=
                 std::string::npos &&
             ir.find("call i32 @janus_graphics_screen_width") !=
                 std::string::npos,
         "graphics window controls lower through the native backend");
  expect(ir.find("call void @janus_graphics_set_mouse_position") !=
                 std::string::npos &&
             ir.find("call i32 @janus_graphics_key_pressed") !=
                 std::string::npos,
         "expanded graphics input lowers through the native backend");
  expect(ir.find("call void @drawLineBetween(") != std::string::npos &&
             ir.find("call void @drawRectangleArea(") != std::string::npos &&
             ir.find("%struct.Vector2") != std::string::npos,
         "typed vector, rectangle, and color helpers lower successfully");
  expect(ir.find("call void @janus_graphics_begin_camera") !=
                 std::string::npos &&
             ir.find("call float @janus_graphics_screen_to_world_x") !=
                 std::string::npos,
         "typed 2D camera helpers lower through the native backend");
  expect(ir.find("call void @janus_graphics_draw_rectangle_rounded") !=
                 std::string::npos &&
             ir.find("call i1 @janus_graphics_collision_rectangles") !=
                 std::string::npos,
         "extended 2D shapes and collision helpers lower through the backend");
  expect(
      ir.find("call void @janus_graphics_begin_blend") != std::string::npos &&
          ir.find("call void @janus_graphics_end_blend()") != std::string::npos,
      "typed blend scopes lower through the native backend");
  expect(ir.find("call void @janus_graphics_draw_texture_pro") !=
                 std::string::npos &&
             ir.find("call void @janus_graphics_set_texture_filter") !=
                 std::string::npos,
         "advanced sprite drawing lowers through the native backend");
  expect(ir.find("call ptr @janus_graphics_load_font") != std::string::npos &&
             ir.find("call ptr @janus_graphics_load_font_utf8") !=
                 std::string::npos &&
             ir.find("call void @janus_graphics_draw_text_font") !=
                 std::string::npos &&
             ir.find("call void @janus_graphics_unload_font") !=
                 std::string::npos,
         "owned fonts and UTF-8 text lower through the native backend");
  expect(ir.find("call i1 @janus_graphics_is_gamepad_available") !=
                 std::string::npos &&
             ir.find("call float @janus_graphics_gamepad_axis") !=
                 std::string::npos &&
             ir.find("call void @janus_graphics_set_gamepad_vibration") !=
                 std::string::npos,
         "typed gamepad input and vibration lower through the native backend");
  expect(ir.find("call ptr @janus_graphics_load_render_texture") !=
                 std::string::npos &&
             ir.find("call ptr @janus_graphics_load_fragment_shader") !=
                 std::string::npos &&
             ir.find("call void @janus_graphics_set_shader_float") !=
                 std::string::npos,
         "render textures and typed shader uniforms lower successfully");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }

  std::cout << "std.graphics exposes typed drawing and input primitives\n";
  return 0;
}
