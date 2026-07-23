#include "janus/backend/llvm/ir_generator.hpp"
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
    val color : uint = rgba(18, 52, 86, 120)
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
    beginDrawing()
    beginCamera(camera)
    clearBackground(color)
    drawPixel(1, 2, color)
    drawLine(1, 2, 3, 4, color)
    drawCircle(5, 6, float(7.0), color)
    drawRectangle(8, 9, 10, 11, color)
    drawText("Janus", 12, 13, 14, color)
    drawLineBetween(start, end, typedColor)
    drawCircleAt(start, float(7.0), typedColor)
    drawRectangleArea(area, typedColor)
    drawTextAt("typed", start, 14, typedColor)
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
    delete start
    delete end
    delete area
    delete screen
    delete world
    delete camera
    delete typedColor
    endDrawing()
    if keyDown || mouseDown {
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
  expect(ir.find("call void @drawLineBetween(ptr") != std::string::npos &&
             ir.find("call void @drawRectangleArea(ptr") != std::string::npos,
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

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }

  std::cout << "std.graphics exposes typed drawing and input primitives\n";
  return 0;
}
