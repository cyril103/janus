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
    beginDrawing()
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
    val textureWidth : int = texture.width()
    texture.draw(textureWidth, texture.height(), white())
    texture.drawAt(start, typedColor)
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
    if keyDown || mouseDown || gamepadReady || gamepadDown {
        return mouseX() + mouseY() + screenWidth() + screenHeight() +
            keyPressed()
    }
    return 0
}
)";

  janus::frontend::ModuleLoader loader{
      {std::filesystem::path{JANUS_STDLIB_DIR}}};
  const janus::ast::Program program = loader.load(
      std::filesystem::path{"graphics_test/main.janus"}, source);

  janus::semantic::Analyzer analyzer;
  static_cast<void>(analyzer.analyze(program));

  try {
    const janus::ast::Program private_program = loader.load(
        std::filesystem::path{"graphics_private_test/main.janus"},
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
               "is private") !=
               std::string_view::npos,
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
                 "%struct.Color { i32 255 }") !=
                 std::string::npos &&
             ir.find("@__janus_global_std_graphics_types__White = constant "
                     "%struct.Color { i32 -1 }") !=
                 std::string::npos &&
             ir.find(
                 "@__janus_global_std_graphics_types__Red = constant "
                 "%struct.Color { i32 -433506305 }") !=
                 std::string::npos &&
             ir.find(
                 "@__janus_global_std_graphics_types__Green = constant "
                 "%struct.Color { i32 14954751 }") !=
                 std::string::npos &&
             ir.find(
                 "@__janus_global_std_graphics_types__Blue = constant "
                 "%struct.Color { i32 7991807 }") !=
                 std::string::npos,
         "graphics exposes statically initialized global color values");
  expect(ir.find("call void @janus_graphics_draw_circle") != std::string::npos,
         "graphics circles lower through the native backend");
  expect(ir.find("call void @janus_graphics_draw_text") != std::string::npos,
         "graphics text lowers through the native backend");
  expect(ir.find("call ptr @janus_graphics_load_texture") != std::string::npos &&
             ir.find("call void @janus_graphics_unload_texture") !=
                 std::string::npos,
         "graphics textures load and unload through owned handles");
  expect(ir.find("call ptr @janus_graphics_load_sound") != std::string::npos &&
             ir.find("call ptr @janus_graphics_load_music") != std::string::npos,
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
