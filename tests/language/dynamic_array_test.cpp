#include "janus/backend/llvm/ir_generator.hpp"
#include "janus/frontend/parser.hpp"
#include "janus/semantic/analyzer.hpp"

#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/raw_ostream.h>

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
class Array[T](initialCapacity : usize) {
    private var data : Ptr[T] = alloc[T](initialCapacity)
    private var length : usize = usize(0)
    private var allocated : usize = initialCapacity
    private def resize(requested : usize) : Unit {
        var next : usize = requested
        if next == usize(0) {
            next = usize(1)
        }
        val resized : Ptr[T] = realloc[T](data, next)
        if resized == null[T]() {
            panic("Array allocation failed\n")
        }
        data = resized
        allocated = next
    }
    def size() : usize {
        return length
    }
    def capacity() : usize {
        return allocated
    }
    def get(index : usize) : T {
        if index >= length {
            panic("Array index out of bounds\n")
        }
        return data.load(index)
    }
    def set(index : usize, value : T) : Unit {
        if index >= length {
            panic("Array index out of bounds\n")
        }
        data.store(index, value)
    }
    def push(value : T) : usize {
        if length == allocated {
            this.resize(allocated * usize(2))
        }
        data.store(length, value)
        length = length + usize(1)
        return length
    }
    def pop() : T {
        if length == usize(0) {
            panic("cannot pop an empty Array\n")
        }
        length = length - usize(1)
        return data.load(length)
    }
    def reserve(requested : usize) : Unit {
        if requested > allocated {
            this.resize(requested)
        }
    }
    def clear() : Unit {
        length = usize(0)
    }
    destructor {
        free(data)
    }
}
def main() : int {
    val values : Array[int] = new Array[int](usize(1))
    values.push(10)
    values.push(20)
    values.push(12)
    values.set(usize(0), values.get(usize(0)) + values.pop())
    val result : int = values.get(usize(0)) + values.get(usize(1))
    delete values
    return result
}
)";

  janus::frontend::Parser parser{source};
  const janus::ast::Program program = parser.parse_program();
  janus::semantic::Analyzer analyzer;
  static_cast<void>(analyzer.analyze(program));

  llvm::LLVMContext context;
  janus::backend::llvm::IrGenerator generator{context};
  const std::unique_ptr<llvm::Module> module =
      generator.generate(program, "dynamic_array");
  std::string ir;
  llvm::raw_string_ostream output{ir};
  module->print(output, nullptr);
  output.flush();

  expect(ir.find("%class.Array__int = type { ptr, i64, i64 }") !=
             std::string::npos,
         "Array[int] stores a buffer, length, and capacity");
  expect(ir.find("define internal void @Array__int__resize") !=
             std::string::npos,
         "Array growth is emitted as a private specialized method");
  expect(ir.find("call ptr @janus_realloc") != std::string::npos,
         "Array growth reallocates its contiguous buffer");
  expect(ir.find("define i32 @Array__int__get") != std::string::npos,
         "Array[int].get returns an int");
  expect(ir.find("call void @janus_panic") != std::string::npos,
         "Array bounds checks panic on failure");
  expect(ir.find("define internal void @Array__int__destructor") !=
             std::string::npos,
         "Array has a specialized destructor");
  expect(ir.find("call void @janus_free") != std::string::npos,
         "Array destruction releases its buffer");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }

  std::cout << "Array[T] grows dynamically and checks its bounds\n";
  return 0;
}
