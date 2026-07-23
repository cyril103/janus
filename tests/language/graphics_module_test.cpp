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
    val keyDown : bool = isKeyDown(Key.Left)
    val mouseDown : bool = isMouseButtonDown(MouseButton.Left)
    beginDrawing()
    clearBackground(color)
    drawPixel(1, 2, color)
    drawLine(1, 2, 3, 4, color)
    drawCircle(5, 6, float(7.0), color)
    drawRectangle(8, 9, 10, 11, color)
    drawText("Janus", 12, 13, 14, color)
    val texture : Texture = loadTexture("sprite.png")
    val textureWidth : int = texture.width()
    texture.draw(textureWidth, texture.height(), white())
    delete texture
    endDrawing()
    if keyDown || mouseDown {
        return mouseX() + mouseY()
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
  expect(ir.find("call i1 @isKeyDown(%enum.Key { i32 263 })") !=
             std::string::npos,
         "graphics keys retain their raylib-compatible code");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }

  std::cout << "std.graphics exposes typed drawing and input primitives\n";
  return 0;
}
