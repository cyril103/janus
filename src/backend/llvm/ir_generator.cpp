#include "janus/backend/llvm/ir_generator.hpp"

#include "janus/backend/llvm/type_lowering.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>

namespace {

const janus::Type *builtin_type(std::string_view name) {
  if (name == "int")
    return &janus::Type::int_type();
  if (name == "double")
    return &janus::Type::double_type();
  if (name == "byte")
    return &janus::Type::byte_type();
  if (name == "char")
    return &janus::Type::char_type();
  if (name == "bool")
    return &janus::Type::bool_type();
  if (name == "string")
    return &janus::Type::string_type();
  return nullptr;
}

class Generator {
public:
  Generator(::llvm::LLVMContext &context, const janus::ast::Program &program,
            std::string_view module_name)
      : context_{context}, module_{std::make_unique<::llvm::Module>(
                               std::string{module_name}, context)} {
    module_->setTargetTriple(
        ::llvm::Triple{::llvm::sys::getDefaultTargetTriple()});
    module_->setPICLevel(::llvm::PICLevel::BigPIC);
    module_->setPIELevel(::llvm::PIELevel::Large);
    for (const janus::ast::ClassDeclaration &class_declaration :
         program.classes) {
      classes_.emplace(class_declaration.name, &class_declaration);
      class_types_.emplace(class_declaration.name,
                           janus::Type::class_type(class_declaration.name));
      llvm_class_types_.emplace(
          class_declaration.name,
          ::llvm::StructType::create(context_,
                                     "class." + class_declaration.name));
    }
    for (const janus::ast::ClassDeclaration &class_declaration :
         program.classes) {
      std::vector<::llvm::Type *> fields;
      for (const auto &field : class_declaration.constructor_fields)
        fields.push_back(janus::backend::llvm::lower_type(
            resolve(field.declared_type, {}), context_));
      for (const auto &field : class_declaration.fields)
        fields.push_back(janus::backend::llvm::lower_type(
            resolve(field.declared_type, {}), context_));
      llvm_class_types_.at(class_declaration.name)->setBody(fields);
    }
    for (const janus::ast::FunctionDeclaration &function : program.functions)
      functions_.emplace(function.name, &function);
  }

  std::unique_ptr<::llvm::Module> generate() {
    for (const auto &[name, function] : functions_) {
      static_cast<void>(name);
      if (function->type_parameters.empty())
        static_cast<void>(emit_function(*function, {}));
    }
    for (const auto &[class_name, class_declaration] : classes_) {
      static_cast<void>(class_name);
      for (const janus::ast::FunctionDeclaration &method :
           class_declaration->methods) {
        if (method.type_parameters.empty())
          static_cast<void>(emit_function(method, {}, class_declaration));
      }
    }
    return std::move(module_);
  }

private:
  using Substitutions = std::unordered_map<std::string, const janus::Type *>;

  struct Local {
    ::llvm::Value *storage;
    const janus::Type *type;
  };

  const janus::Type &resolve(const janus::ast::TypeReference &reference,
                             const Substitutions &substitutions) const {
    if (const janus::Type *type = builtin_type(reference.name))
      return *type;
    if (const auto iterator = class_types_.find(reference.name);
        iterator != class_types_.end())
      return iterator->second;
    return *substitutions.at(reference.name);
  }

  std::string mangle(const janus::ast::FunctionDeclaration &function,
                     const std::vector<const janus::Type *> &type_arguments) {
    if (type_arguments.empty())
      return function.name;
    std::string name = function.name;
    for (const janus::Type *type : type_arguments)
      name += "__" + std::string{type->name()};
    return name;
  }

  ::llvm::Function *
  emit_function(const janus::ast::FunctionDeclaration &function,
                const std::vector<const janus::Type *> &type_arguments,
                const janus::ast::ClassDeclaration *owner = nullptr) {
    const std::string llvm_name =
        (owner == nullptr ? std::string{} : owner->name + "__") +
        mangle(function, type_arguments);
    if (const auto iterator = emitted_.find(llvm_name);
        iterator != emitted_.end())
      return iterator->second;

    Substitutions substitutions;
    for (std::size_t index = 0; index < type_arguments.size(); ++index)
      substitutions.emplace(function.type_parameters[index],
                            type_arguments[index]);

    const janus::Type &return_type =
        resolve(function.return_type, substitutions);
    std::vector<::llvm::Type *> parameter_types;
    parameter_types.reserve(function.parameters.size() +
                            (owner == nullptr ? 0 : 1));
    if (owner != nullptr)
      parameter_types.push_back(::llvm::PointerType::getUnqual(context_));
    for (const auto &parameter : function.parameters)
      parameter_types.push_back(janus::backend::llvm::lower_type(
          resolve(parameter.type, substitutions), context_));

    auto *function_type = ::llvm::FunctionType::get(
        janus::backend::llvm::lower_type(return_type, context_),
        parameter_types, false);
    auto *llvm_function = ::llvm::Function::Create(
        function_type, ::llvm::Function::ExternalLinkage, llvm_name, *module_);
    emitted_.emplace(llvm_name, llvm_function);

    auto *entry = ::llvm::BasicBlock::Create(context_, "entry", llvm_function);
    ::llvm::IRBuilder<> builder{entry};
    std::unordered_map<std::string, Local> locals;

    auto argument_iterator = llvm_function->arg_begin();
    if (owner != nullptr) {
      ::llvm::Argument &this_argument = *argument_iterator++;
      this_argument.setName("this");
      const janus::Type &owner_type = class_types_.at(owner->name);
      ::llvm::Value *this_storage =
          builder.CreateAlloca(builder.getPtrTy(), nullptr, "this.addr");
      builder.CreateStore(&this_argument, this_storage);
      locals.emplace("this", Local{this_storage, &owner_type});

      unsigned field_index = 0;
      for (const janus::ast::ValueDeclaration &field :
           owner->constructor_fields) {
        const janus::Type &field_type = resolve(field.declared_type, {});
        locals.emplace(field.name, Local{builder.CreateStructGEP(
                                             llvm_class_types_.at(owner->name),
                                             &this_argument, field_index++,
                                             field.name + ".addr"),
                                         &field_type});
      }
      for (const janus::ast::ValueDeclaration &field : owner->fields) {
        const janus::Type &field_type = resolve(field.declared_type, {});
        locals.emplace(field.name, Local{builder.CreateStructGEP(
                                             llvm_class_types_.at(owner->name),
                                             &this_argument, field_index++,
                                             field.name + ".addr"),
                                         &field_type});
      }
    }

    std::size_t parameter_index = 0;
    for (; argument_iterator != llvm_function->arg_end(); ++argument_iterator) {
      ::llvm::Argument &argument = *argument_iterator;
      const auto &parameter = function.parameters[parameter_index++];
      const janus::Type &type = resolve(parameter.type, substitutions);
      argument.setName(parameter.name);
      ::llvm::Value *storage =
          builder.CreateAlloca(janus::backend::llvm::lower_type(type, context_),
                               nullptr, parameter.name);
      builder.CreateStore(&argument, storage);
      locals.emplace(parameter.name, Local{storage, &type});
    }

    for (const janus::ast::Statement &statement : function.body) {
      if (const auto *declaration =
              std::get_if<janus::ast::ValueDeclaration>(&statement)) {
        const janus::Type &type =
            resolve(declaration->declared_type, substitutions);
        ::llvm::Value *storage = builder.CreateAlloca(
            janus::backend::llvm::lower_type(type, context_), nullptr,
            declaration->name);
        if (declaration->initializer.has_value()) {
          ::llvm::Value *initializer = emit_expression(
              *declaration->initializer, type, substitutions, locals, builder);
          builder.CreateStore(initializer, storage);
        }
        locals.emplace(declaration->name, Local{storage, &type});
        continue;
      }

      if (const auto *assignment =
              std::get_if<janus::ast::AssignmentStatement>(&statement)) {
        if (!assignment->object.empty()) {
          const Local &object = locals.at(assignment->object);
          ::llvm::Value *object_pointer = builder.CreateLoad(
              ::llvm::PointerType::getUnqual(context_), object.storage,
              assignment->object + ".object");
          const auto [field_index, field_type] =
              find_field(object.type->name(), assignment->name);
          ::llvm::Value *field_pointer = builder.CreateStructGEP(
              llvm_class_types_.at(std::string{object.type->name()}),
              object_pointer, field_index);
          builder.CreateStore(emit_expression(assignment->expression,
                                              *field_type, substitutions,
                                              locals, builder),
                              field_pointer);
          continue;
        }
        const Local &local = locals.at(assignment->name);
        ::llvm::Value *value =
            emit_expression(assignment->expression, *local.type, substitutions,
                            locals, builder);
        builder.CreateStore(value, local.storage);
        continue;
      }

      if (const auto *deletion =
              std::get_if<janus::ast::DeleteStatement>(&statement)) {
        const auto *identifier = std::get_if<janus::ast::IdentifierExpression>(
            &deletion->expression.value);
        const Local &local = locals.at(identifier->name);
        ::llvm::Value *pointer =
            builder.CreateLoad(::llvm::PointerType::getUnqual(context_),
                               local.storage, identifier->name + ".object");
        builder.CreateCall(emit_destructor(std::string{local.type->name()}),
                           {pointer});
        ::llvm::FunctionCallee free_function = module_->getOrInsertFunction(
            "free", ::llvm::FunctionType::get(builder.getVoidTy(),
                                              {builder.getPtrTy()}, false));
        builder.CreateCall(free_function, {pointer});
        continue;
      }

      const auto &return_statement =
          std::get<janus::ast::ReturnStatement>(statement);
      builder.CreateRet(emit_expression(return_statement.expression,
                                        return_type, substitutions, locals,
                                        builder));
    }
    return llvm_function;
  }

  std::pair<unsigned, const janus::Type *>
  find_field(std::string_view class_name, std::string_view field_name) {
    const auto &class_declaration = *classes_.at(std::string{class_name});
    unsigned index = 0;
    for (const auto &field : class_declaration.constructor_fields) {
      if (field.name == field_name)
        return {index, &resolve(field.declared_type, {})};
      ++index;
    }
    for (const auto &field : class_declaration.fields) {
      if (field.name == field_name)
        return {index, &resolve(field.declared_type, {})};
      ++index;
    }
    return {0, nullptr};
  }

  ::llvm::Function *emit_destructor(const std::string &class_name) {
    const std::string name = class_name + "__destructor";
    if (const auto iterator = emitted_.find(name); iterator != emitted_.end())
      return iterator->second;
    auto *type = ::llvm::FunctionType::get(
        ::llvm::Type::getVoidTy(context_),
        {::llvm::PointerType::getUnqual(context_)}, false);
    auto *function = ::llvm::Function::Create(
        type, ::llvm::Function::InternalLinkage, name, *module_);
    emitted_.emplace(name, function);
    auto *entry = ::llvm::BasicBlock::Create(context_, "entry", function);
    ::llvm::IRBuilder<> builder{entry};
    builder.CreateRetVoid();
    return function;
  }

  ::llvm::Value *
  emit_expression(const janus::ast::Expression &expression,
                  const janus::Type &expected_type,
                  const Substitutions &substitutions,
                  const std::unordered_map<std::string, Local> &locals,
                  ::llvm::IRBuilder<> &builder) {
    return std::visit(
        [&](const auto &node) -> ::llvm::Value * {
          using Node = std::decay_t<decltype(node)>;
          ::llvm::Type *llvm_type =
              janus::backend::llvm::lower_type(expected_type, context_);
          if constexpr (std::is_same_v<Node,
                                       janus::ast::StringLiteralExpression>) {
            ::llvm::Constant *data = ::llvm::ConstantDataArray::getString(
                context_, node.value, true);
            auto *global = new ::llvm::GlobalVariable(
                *module_, data->getType(), true,
                ::llvm::GlobalValue::PrivateLinkage, data,
                ".str." + std::to_string(string_literal_index_++));
            global->setUnnamedAddr(::llvm::GlobalValue::UnnamedAddr::Global);
            ::llvm::Constant *zero = builder.getInt32(0);
            const std::array<::llvm::Constant *, 2> indices{zero, zero};
            ::llvm::Constant *pointer =
                ::llvm::ConstantExpr::getInBoundsGetElementPtr(data->getType(),
                                                               global, indices);
            ::llvm::Constant *length = ::llvm::ConstantInt::get(
                builder.getInt64Ty(), node.value.size(), false);
            return ::llvm::ConstantStruct::get(
                ::llvm::cast<::llvm::StructType>(llvm_type), {pointer, length});
          } else if constexpr (std::is_same_v<
                                   Node, janus::ast::DoubleLiteralExpression>) {
            return ::llvm::ConstantFP::get(llvm_type, node.value);
          } else if constexpr (std::is_same_v<
                                   Node,
                                   janus::ast::CharacterLiteralExpression>) {
            return ::llvm::ConstantInt::get(
                llvm_type, static_cast<std::uint32_t>(node.value), false);
          } else if constexpr (std::is_same_v<
                                   Node,
                                   janus::ast::BooleanLiteralExpression>) {
            return ::llvm::ConstantInt::get(llvm_type, node.value, false);
          } else if constexpr (std::is_same_v<
                                   Node,
                                   janus::ast::IntegerLiteralExpression>) {
            return ::llvm::ConstantInt::get(
                llvm_type, static_cast<std::uint64_t>(node.value),
                expected_type.is_signed());
          } else if constexpr (std::is_same_v<
                                   Node, janus::ast::IdentifierExpression>) {
            const Local &local = locals.at(node.name);
            return builder.CreateLoad(
                janus::backend::llvm::lower_type(*local.type, context_),
                local.storage, node.name + ".value");
          } else if constexpr (std::is_same_v<Node,
                                              janus::ast::CallExpression>) {
            const janus::ast::FunctionDeclaration &callee =
                *functions_.at(node.callee);
            std::vector<const janus::Type *> type_arguments;
            type_arguments.reserve(node.type_arguments.size());
            for (const janus::ast::TypeReference &argument :
                 node.type_arguments)
              type_arguments.push_back(&resolve(argument, substitutions));
            ::llvm::Function *target = emit_function(callee, type_arguments);

            Substitutions callee_substitutions;
            for (std::size_t index = 0; index < type_arguments.size(); ++index)
              callee_substitutions.emplace(callee.type_parameters[index],
                                           type_arguments[index]);
            std::vector<::llvm::Value *> arguments;
            arguments.reserve(node.arguments.size());
            for (std::size_t index = 0; index < node.arguments.size();
                 ++index) {
              const janus::Type &parameter_type =
                  resolve(callee.parameters[index].type, callee_substitutions);
              arguments.push_back(emit_expression(*node.arguments[index],
                                                  parameter_type, substitutions,
                                                  locals, builder));
            }
            return builder.CreateCall(target, arguments,
                                      node.callee + ".result");
          } else if constexpr (std::is_same_v<Node,
                                              janus::ast::NewExpression>) {
            const auto &class_declaration = *classes_.at(node.class_name);
            ::llvm::StructType *class_type =
                llvm_class_types_.at(node.class_name);
            ::llvm::FunctionCallee malloc_function =
                module_->getOrInsertFunction(
                    "malloc",
                    ::llvm::FunctionType::get(builder.getPtrTy(),
                                              {builder.getInt64Ty()}, false));
            ::llvm::Value *object = builder.CreateCall(
                malloc_function, {::llvm::ConstantExpr::getSizeOf(class_type)},
                node.class_name + ".new");
            unsigned field_index = 0;
            for (std::size_t index = 0; index < node.arguments.size();
                 ++index) {
              const janus::Type &field_type = resolve(
                  class_declaration.constructor_fields[index].declared_type,
                  {});
              ::llvm::Value *field =
                  builder.CreateStructGEP(class_type, object, field_index++);
              builder.CreateStore(emit_expression(*node.arguments[index],
                                                  field_type, substitutions,
                                                  locals, builder),
                                  field);
            }
            for (const auto &field_declaration : class_declaration.fields) {
              ::llvm::Value *field =
                  builder.CreateStructGEP(class_type, object, field_index++);
              if (field_declaration.initializer.has_value()) {
                const janus::Type &field_type =
                    resolve(field_declaration.declared_type, {});
                builder.CreateStore(
                    emit_expression(*field_declaration.initializer, field_type,
                                    substitutions, locals, builder),
                    field);
              }
            }
            return object;
          } else if constexpr (std::is_same_v<
                                   Node, janus::ast::MemberAccessExpression>) {
            const auto *identifier =
                std::get_if<janus::ast::IdentifierExpression>(
                    &node.object->value);
            const Local &object = locals.at(identifier->name);
            ::llvm::Value *object_pointer =
                builder.CreateLoad(builder.getPtrTy(), object.storage,
                                   identifier->name + ".object");
            const auto [field_index, field_type] =
                find_field(object.type->name(), node.member);
            ::llvm::Value *field_pointer = builder.CreateStructGEP(
                llvm_class_types_.at(std::string{object.type->name()}),
                object_pointer, field_index);
            return builder.CreateLoad(
                janus::backend::llvm::lower_type(*field_type, context_),
                field_pointer, node.member + ".value");
          } else {
            const auto *identifier =
                std::get_if<janus::ast::IdentifierExpression>(
                    &node.object->value);
            const Local &object = locals.at(identifier->name);
            const auto &class_declaration =
                *classes_.at(std::string{object.type->name()});
            const janus::ast::FunctionDeclaration *method = nullptr;
            for (const janus::ast::FunctionDeclaration &candidate :
                 class_declaration.methods) {
              if (candidate.name == node.method)
                method = &candidate;
            }
            ::llvm::Function *target =
                emit_function(*method, {}, &class_declaration);
            std::vector<::llvm::Value *> arguments;
            arguments.push_back(
                builder.CreateLoad(builder.getPtrTy(), object.storage,
                                   identifier->name + ".object"));
            for (std::size_t index = 0; index < node.arguments.size();
                 ++index) {
              const janus::Type &parameter_type =
                  resolve(method->parameters[index].type, {});
              arguments.push_back(emit_expression(*node.arguments[index],
                                                  parameter_type, substitutions,
                                                  locals, builder));
            }
            return builder.CreateCall(target, arguments,
                                      node.method + ".result");
          }
        },
        expression.value);
  }

  ::llvm::LLVMContext &context_;
  std::unique_ptr<::llvm::Module> module_;
  std::unordered_map<std::string, const janus::ast::FunctionDeclaration *>
      functions_;
  std::unordered_map<std::string, const janus::ast::ClassDeclaration *>
      classes_;
  std::unordered_map<std::string, janus::Type> class_types_;
  std::unordered_map<std::string, ::llvm::StructType *> llvm_class_types_;
  std::unordered_map<std::string, ::llvm::Function *> emitted_;
  std::size_t string_literal_index_{};
};

} // namespace

namespace janus::backend::llvm {

IrGenerator::IrGenerator(::llvm::LLVMContext &context) noexcept
    : context_{context} {}

std::unique_ptr<::llvm::Module>
IrGenerator::generate(const ast::Program &program,
                      std::string_view module_name) {
  return Generator{context_, program, module_name}.generate();
}

} // namespace janus::backend::llvm
