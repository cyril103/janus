#include "janus/backend/llvm/ir_generator.hpp"

#include "janus/backend/llvm/type_lowering.hpp"

#include <cstdint>
#include <string>
#include <variant>

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>

namespace janus::backend::llvm {

IrGenerator::IrGenerator(::llvm::LLVMContext &context) noexcept
    : context_{context} {}

std::unique_ptr<::llvm::Module>
IrGenerator::generate(const ast::Program &program,
                      std::string_view module_name) {
  auto module =
      std::make_unique<::llvm::Module>(std::string{module_name}, context_);

  for (const ast::FunctionDeclaration &function : program.functions) {
    ::llvm::IRBuilder<> builder{context_};
    ::llvm::Type *return_type = lower_type(*function.return_type, context_);
    auto *function_type = ::llvm::FunctionType::get(return_type, false);
    auto *llvm_function = ::llvm::Function::Create(
        function_type, ::llvm::Function::ExternalLinkage, function.name,
        *module);
    auto *entry = ::llvm::BasicBlock::Create(context_, "entry", llvm_function);
    builder.SetInsertPoint(entry);

    for (const ast::Statement &statement : function.body) {
      if (const auto *declaration =
              std::get_if<ast::ValueDeclaration>(&statement)) {
        ::llvm::Type *storage_type =
            lower_type(*declaration->declared_type, context_);
        ::llvm::Value *storage =
            builder.CreateAlloca(storage_type, nullptr, declaration->name);

        ::llvm::Value *initializer = std::visit(
            [&builder](const auto &expression) -> ::llvm::Value * {
              return builder.getInt32(expression.value);
            },
            declaration->initializer);

        builder.CreateStore(initializer, storage);
        continue;
      }

      const auto &return_statement = std::get<ast::ReturnStatement>(statement);
      ::llvm::Value *return_value = std::visit(
          [&builder](const auto &expression) -> ::llvm::Value * {
            return builder.getInt32(expression.value);
          },
          return_statement.expression);
      builder.CreateRet(return_value);
    }
  }

  return module;
}

} // namespace janus::backend::llvm
