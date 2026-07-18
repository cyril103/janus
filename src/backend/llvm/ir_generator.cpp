#include "janus/backend/llvm/ir_generator.hpp"

#include "janus/backend/llvm/type_lowering.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
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
    }
    for (const janus::ast::FunctionDeclaration &function : program.functions)
      functions_.emplace(function.name, &function);
  }

  std::unique_ptr<::llvm::Module> generate() {
    for (const auto &[name, class_declaration] : classes_) {
      if (class_declaration->type_parameters.empty())
        static_cast<void>(ensure_class(name, {}));
    }
    for (const auto &[name, function] : functions_) {
      static_cast<void>(name);
      if (function->type_parameters.empty())
        static_cast<void>(emit_function(*function, {}));
    }
    std::unordered_set<std::string> emitted_specializations;
    while (emitted_specializations.size() < class_specializations_.size()) {
      std::vector<std::string> keys;
      for (const auto &[key, specialization] : class_specializations_) {
        static_cast<void>(specialization);
        if (!emitted_specializations.contains(key))
          keys.push_back(key);
      }
      for (const std::string &key : keys) {
        emitted_specializations.insert(key);
        const ClassSpecialization &specialization =
            class_specializations_.at(key);
        for (const janus::ast::FunctionDeclaration &method :
             specialization.declaration->methods) {
          if (method.type_parameters.empty())
            static_cast<void>(
                emit_function(method, {}, specialization.declaration,
                              &specialization.substitutions, key));
        }
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

  struct ClassSpecialization {
    const janus::ast::ClassDeclaration *declaration;
    Substitutions substitutions;
  };

  const janus::Type &resolve(const janus::ast::TypeReference &reference,
                             const Substitutions &substitutions) {
    if (const janus::Type *type = builtin_type(reference.name))
      return *type;
    if (const auto iterator = substitutions.find(reference.name);
        iterator != substitutions.end())
      return *iterator->second;
    std::vector<const janus::Type *> type_arguments;
    type_arguments.reserve(reference.type_arguments.size());
    for (const janus::ast::TypeReference &argument : reference.type_arguments)
      type_arguments.push_back(&resolve(argument, substitutions));
    return ensure_class(reference.name, type_arguments);
  }

  std::string
  class_key(std::string_view name,
            const std::vector<const janus::Type *> &type_arguments) const {
    std::string key{name};
    for (const janus::Type *argument : type_arguments)
      key += "__" + std::string{argument->name()};
    return key;
  }

  const janus::Type &
  ensure_class(std::string_view name,
               const std::vector<const janus::Type *> &type_arguments) {
    const std::string key = class_key(name, type_arguments);
    if (const auto iterator = class_types_.find(key);
        iterator != class_types_.end())
      return iterator->second;

    const janus::ast::ClassDeclaration &declaration =
        *classes_.at(std::string{name});
    Substitutions substitutions;
    for (std::size_t index = 0; index < type_arguments.size(); ++index)
      substitutions.emplace(declaration.type_parameters[index],
                            type_arguments[index]);

    auto [type_iterator, inserted] =
        class_types_.emplace(key, janus::Type::class_type(key));
    static_cast<void>(inserted);
    ::llvm::StructType *llvm_class_type =
        ::llvm::StructType::create(context_, "class." + key);
    llvm_class_types_.emplace(key, llvm_class_type);
    class_specializations_.emplace(
        key, ClassSpecialization{&declaration, substitutions});

    std::vector<::llvm::Type *> fields;
    for (const auto &field : declaration.constructor_fields)
      fields.push_back(janus::backend::llvm::lower_type(
          resolve(field.declared_type, substitutions), context_));
    for (const auto &field : declaration.fields)
      fields.push_back(janus::backend::llvm::lower_type(
          resolve(field.declared_type, substitutions), context_));
    llvm_class_type->setBody(fields);
    return type_iterator->second;
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
                const janus::ast::ClassDeclaration *owner = nullptr,
                const Substitutions *owner_substitutions = nullptr,
                std::string_view owner_key = {}) {
    const std::string llvm_name =
        (owner == nullptr ? std::string{} : std::string{owner_key} + "__") +
        mangle(function, type_arguments);
    if (const auto iterator = emitted_.find(llvm_name);
        iterator != emitted_.end())
      return iterator->second;

    Substitutions substitutions;
    if (owner_substitutions != nullptr)
      substitutions = *owner_substitutions;
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
    const ::llvm::GlobalValue::LinkageTypes linkage =
        owner != nullptr && function.is_private
            ? ::llvm::Function::InternalLinkage
            : ::llvm::Function::ExternalLinkage;
    auto *llvm_function =
        ::llvm::Function::Create(function_type, linkage, llvm_name, *module_);
    emitted_.emplace(llvm_name, llvm_function);

    auto *entry = ::llvm::BasicBlock::Create(context_, "entry", llvm_function);
    ::llvm::IRBuilder<> builder{entry};
    std::unordered_map<std::string, Local> locals;

    auto argument_iterator = llvm_function->arg_begin();
    if (owner != nullptr) {
      ::llvm::Argument &this_argument = *argument_iterator++;
      this_argument.setName("this");
      const janus::Type &owner_type = class_types_.at(std::string{owner_key});
      ::llvm::Value *this_storage =
          builder.CreateAlloca(builder.getPtrTy(), nullptr, "this.addr");
      builder.CreateStore(&this_argument, this_storage);
      locals.emplace("this", Local{this_storage, &owner_type});

      unsigned field_index = 0;
      for (const janus::ast::ValueDeclaration &field :
           owner->constructor_fields) {
        const janus::Type &field_type =
            resolve(field.declared_type, substitutions);
        locals.emplace(
            field.name,
            Local{builder.CreateStructGEP(
                      llvm_class_types_.at(std::string{owner_key}),
                      &this_argument, field_index++, field.name + ".addr"),
                  &field_type});
      }
      for (const janus::ast::ValueDeclaration &field : owner->fields) {
        const janus::Type &field_type =
            resolve(field.declared_type, substitutions);
        locals.emplace(
            field.name,
            Local{builder.CreateStructGEP(
                      llvm_class_types_.at(std::string{owner_key}),
                      &this_argument, field_index++, field.name + ".addr"),
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

    std::function<bool(const std::vector<janus::ast::Statement> &,
                       std::unordered_map<std::string, Local> &)>
        emit_block;
    emit_block = [&](const std::vector<janus::ast::Statement> &statements,
                     std::unordered_map<std::string, Local> &block_locals) {
      for (const janus::ast::Statement &statement : statements) {
        if (const auto *conditional =
                std::get_if<std::shared_ptr<janus::ast::IfStatement>>(
                    &statement)) {
          ::llvm::Value *condition = emit_expression(
              (*conditional)->condition, janus::Type::bool_type(),
              substitutions, block_locals, builder);
          ::llvm::Function *current_function =
              builder.GetInsertBlock()->getParent();
          auto *then_block =
              ::llvm::BasicBlock::Create(context_, "if.then", current_function);
          auto *merge_block =
              ::llvm::BasicBlock::Create(context_, "if.end", current_function);
          ::llvm::BasicBlock *else_block = merge_block;
          if (!(*conditional)->else_body.empty())
            else_block = ::llvm::BasicBlock::Create(
                context_, "if.else", current_function, merge_block);
          builder.CreateCondBr(condition, then_block, else_block);

          builder.SetInsertPoint(then_block);
          auto then_locals = block_locals;
          const bool then_returns =
              emit_block((*conditional)->then_body, then_locals);
          if (!then_returns)
            builder.CreateBr(merge_block);

          bool else_returns = false;
          if (!(*conditional)->else_body.empty()) {
            builder.SetInsertPoint(else_block);
            auto else_locals = block_locals;
            else_returns = emit_block((*conditional)->else_body, else_locals);
            if (!else_returns)
              builder.CreateBr(merge_block);
          }

          builder.SetInsertPoint(merge_block);
          if (then_returns && else_returns) {
            builder.CreateUnreachable();
            return true;
          }
          continue;
        }

        if (const auto *loop =
                std::get_if<std::shared_ptr<janus::ast::WhileStatement>>(
                    &statement)) {
          ::llvm::Function *current_function =
              builder.GetInsertBlock()->getParent();
          auto *condition_block = ::llvm::BasicBlock::Create(
              context_, "while.condition", current_function);
          auto *body_block = ::llvm::BasicBlock::Create(context_, "while.body",
                                                        current_function);
          auto *exit_block = ::llvm::BasicBlock::Create(context_, "while.end",
                                                        current_function);
          builder.CreateBr(condition_block);
          builder.SetInsertPoint(condition_block);
          ::llvm::Value *condition =
              emit_expression((*loop)->condition, janus::Type::bool_type(),
                              substitutions, block_locals, builder);
          builder.CreateCondBr(condition, body_block, exit_block);
          builder.SetInsertPoint(body_block);
          auto body_locals = block_locals;
          const bool body_returns = emit_block((*loop)->body, body_locals);
          if (!body_returns)
            builder.CreateBr(condition_block);
          builder.SetInsertPoint(exit_block);
          continue;
        }

        if (const auto *declaration =
                std::get_if<janus::ast::ValueDeclaration>(&statement)) {
          const janus::Type &type =
              resolve(declaration->declared_type, substitutions);
          ::llvm::Value *storage = builder.CreateAlloca(
              janus::backend::llvm::lower_type(type, context_), nullptr,
              declaration->name);
          if (declaration->initializer.has_value()) {
            ::llvm::Value *initializer =
                emit_expression(*declaration->initializer, type, substitutions,
                                block_locals, builder);
            builder.CreateStore(initializer, storage);
          }
          block_locals.emplace(declaration->name, Local{storage, &type});
          continue;
        }

        if (const auto *assignment =
                std::get_if<janus::ast::AssignmentStatement>(&statement)) {
          if (!assignment->object.empty()) {
            const Local &object = block_locals.at(assignment->object);
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
                                                block_locals, builder),
                                field_pointer);
            continue;
          }
          const Local &local = block_locals.at(assignment->name);
          ::llvm::Value *value =
              emit_expression(assignment->expression, *local.type,
                              substitutions, block_locals, builder);
          builder.CreateStore(value, local.storage);
          continue;
        }

        if (const auto *deletion =
                std::get_if<janus::ast::DeleteStatement>(&statement)) {
          const auto *identifier =
              std::get_if<janus::ast::IdentifierExpression>(
                  &deletion->expression.value);
          const Local &local = block_locals.at(identifier->name);
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
                                          return_type, substitutions,
                                          block_locals, builder));
        return true;
      }
      return false;
    };
    static_cast<void>(emit_block(function.body, locals));
    return llvm_function;
  }

  std::pair<unsigned, const janus::Type *>
  find_field(std::string_view class_name, std::string_view field_name) {
    const ClassSpecialization &specialization =
        class_specializations_.at(std::string{class_name});
    const auto &class_declaration = *specialization.declaration;
    unsigned index = 0;
    for (const auto &field : class_declaration.constructor_fields) {
      if (field.name == field_name)
        return {index,
                &resolve(field.declared_type, specialization.substitutions)};
      ++index;
    }
    for (const auto &field : class_declaration.fields) {
      if (field.name == field_name)
        return {index,
                &resolve(field.declared_type, specialization.substitutions)};
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

  const janus::Type &
  expression_type(const janus::ast::Expression &expression,
                  const Substitutions &substitutions,
                  const std::unordered_map<std::string, Local> &locals) {
    return std::visit(
        [&](const auto &node) -> const janus::Type & {
          using Node = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<Node,
                                       janus::ast::IntegerLiteralExpression>) {
            return janus::Type::int_type();
          } else if constexpr (std::is_same_v<
                                   Node, janus::ast::DoubleLiteralExpression>) {
            return janus::Type::double_type();
          } else if constexpr (std::is_same_v<
                                   Node,
                                   janus::ast::CharacterLiteralExpression>) {
            return janus::Type::char_type();
          } else if constexpr (std::is_same_v<
                                   Node,
                                   janus::ast::BooleanLiteralExpression>) {
            return janus::Type::bool_type();
          } else if constexpr (std::is_same_v<
                                   Node, janus::ast::StringLiteralExpression>) {
            return janus::Type::string_type();
          } else if constexpr (std::is_same_v<
                                   Node, janus::ast::IdentifierExpression>) {
            return *locals.at(node.name).type;
          } else if constexpr (std::is_same_v<Node,
                                              janus::ast::CallExpression>) {
            const auto &callee = *functions_.at(node.callee);
            Substitutions callee_substitutions;
            for (std::size_t index = 0; index < node.type_arguments.size();
                 ++index) {
              callee_substitutions.emplace(
                  callee.type_parameters[index],
                  &resolve(node.type_arguments[index], substitutions));
            }
            return resolve(callee.return_type, callee_substitutions);
          } else if constexpr (std::is_same_v<Node,
                                              janus::ast::NewExpression>) {
            std::vector<const janus::Type *> type_arguments;
            for (const janus::ast::TypeReference &argument :
                 node.type_arguments)
              type_arguments.push_back(&resolve(argument, substitutions));
            return ensure_class(node.class_name, type_arguments);
          } else if constexpr (std::is_same_v<
                                   Node, janus::ast::MemberAccessExpression>) {
            const janus::Type &object_type =
                expression_type(*node.object, substitutions, locals);
            return *find_field(object_type.name(), node.member).second;
          } else if constexpr (std::is_same_v<
                                   Node, janus::ast::MethodCallExpression>) {
            const janus::Type &object_type =
                expression_type(*node.object, substitutions, locals);
            const ClassSpecialization &specialization =
                class_specializations_.at(std::string{object_type.name()});
            const auto &class_declaration = *specialization.declaration;
            for (const auto &method : class_declaration.methods) {
              if (method.name == node.method)
                return resolve(method.return_type,
                               specialization.substitutions);
            }
            return janus::Type::int_type();
          } else if constexpr (std::is_same_v<Node,
                                              janus::ast::UnaryExpression>) {
            if (node.operation == janus::ast::UnaryOperator::LogicalNot)
              return janus::Type::bool_type();
            return expression_type(*node.operand, substitutions, locals);
          } else {
            static_assert(std::is_same_v<Node, janus::ast::BinaryExpression>);
            switch (node.operation) {
            case janus::ast::BinaryOperator::Less:
            case janus::ast::BinaryOperator::LessEqual:
            case janus::ast::BinaryOperator::Greater:
            case janus::ast::BinaryOperator::GreaterEqual:
            case janus::ast::BinaryOperator::Equal:
            case janus::ast::BinaryOperator::NotEqual:
            case janus::ast::BinaryOperator::LogicalAnd:
            case janus::ast::BinaryOperator::LogicalOr:
              return janus::Type::bool_type();
            default:
              return expression_type(*node.left, substitutions, locals);
            }
          }
        },
        expression.value);
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
            std::vector<const janus::Type *> type_arguments;
            for (const janus::ast::TypeReference &argument :
                 node.type_arguments)
              type_arguments.push_back(&resolve(argument, substitutions));
            const janus::Type &object_type =
                ensure_class(node.class_name, type_arguments);
            const ClassSpecialization &specialization =
                class_specializations_.at(std::string{object_type.name()});
            ::llvm::StructType *class_type =
                llvm_class_types_.at(std::string{object_type.name()});
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
                  specialization.substitutions);
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
                    resolve(field_declaration.declared_type,
                            specialization.substitutions);
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
          } else if constexpr (std::is_same_v<
                                   Node, janus::ast::MethodCallExpression>) {
            const auto *identifier =
                std::get_if<janus::ast::IdentifierExpression>(
                    &node.object->value);
            const Local &object = locals.at(identifier->name);
            const ClassSpecialization &specialization =
                class_specializations_.at(std::string{object.type->name()});
            const auto &class_declaration = *specialization.declaration;
            const janus::ast::FunctionDeclaration *method = nullptr;
            for (const janus::ast::FunctionDeclaration &candidate :
                 class_declaration.methods) {
              if (candidate.name == node.method)
                method = &candidate;
            }
            ::llvm::Function *target = emit_function(
                *method, {}, &class_declaration, &specialization.substitutions,
                object.type->name());
            std::vector<::llvm::Value *> arguments;
            arguments.push_back(
                builder.CreateLoad(builder.getPtrTy(), object.storage,
                                   identifier->name + ".object"));
            for (std::size_t index = 0; index < node.arguments.size();
                 ++index) {
              const janus::Type &parameter_type = resolve(
                  method->parameters[index].type, specialization.substitutions);
              arguments.push_back(emit_expression(*node.arguments[index],
                                                  parameter_type, substitutions,
                                                  locals, builder));
            }
            return builder.CreateCall(target, arguments,
                                      node.method + ".result");
          } else if constexpr (std::is_same_v<Node,
                                              janus::ast::UnaryExpression>) {
            const janus::Type *operand_type =
                &expression_type(*node.operand, substitutions, locals);
            if (expected_type.kind() == janus::TypeKind::Byte &&
                std::holds_alternative<janus::ast::IntegerLiteralExpression>(
                    node.operand->value)) {
              operand_type = &expected_type;
            }
            ::llvm::Value *operand = emit_expression(
                *node.operand, *operand_type, substitutions, locals, builder);
            if (node.operation == janus::ast::UnaryOperator::LogicalNot)
              return builder.CreateNot(operand, "not");
            if (operand_type->kind() == janus::TypeKind::Double)
              return builder.CreateFNeg(operand, "neg");
            return builder.CreateNeg(operand, "neg");
          } else {
            static_assert(std::is_same_v<Node, janus::ast::BinaryExpression>);
            if (node.operation == janus::ast::BinaryOperator::LogicalAnd ||
                node.operation == janus::ast::BinaryOperator::LogicalOr) {
              ::llvm::Value *left =
                  emit_expression(*node.left, janus::Type::bool_type(),
                                  substitutions, locals, builder);
              ::llvm::BasicBlock *left_block = builder.GetInsertBlock();
              ::llvm::Function *function = left_block->getParent();
              auto *right_block =
                  ::llvm::BasicBlock::Create(context_, "logic.rhs", function);
              auto *merge_block =
                  ::llvm::BasicBlock::Create(context_, "logic.end", function);
              const bool is_and =
                  node.operation == janus::ast::BinaryOperator::LogicalAnd;
              if (is_and)
                builder.CreateCondBr(left, right_block, merge_block);
              else
                builder.CreateCondBr(left, merge_block, right_block);

              builder.SetInsertPoint(right_block);
              ::llvm::Value *right =
                  emit_expression(*node.right, janus::Type::bool_type(),
                                  substitutions, locals, builder);
              ::llvm::BasicBlock *right_end = builder.GetInsertBlock();
              builder.CreateBr(merge_block);

              builder.SetInsertPoint(merge_block);
              auto *result = builder.CreatePHI(builder.getInt1Ty(), 2, "logic");
              result->addIncoming(builder.getInt1(is_and ? false : true),
                                  left_block);
              result->addIncoming(right, right_end);
              return result;
            }

            const janus::Type &operand_type =
                expression_type(*node.left, substitutions, locals);
            ::llvm::Value *left = emit_expression(
                *node.left, operand_type, substitutions, locals, builder);
            ::llvm::Value *right = emit_expression(
                *node.right, operand_type, substitutions, locals, builder);
            const bool is_double =
                operand_type.kind() == janus::TypeKind::Double;

            switch (node.operation) {
            case janus::ast::BinaryOperator::Add:
              return is_double ? builder.CreateFAdd(left, right, "add")
                               : builder.CreateAdd(left, right, "add");
            case janus::ast::BinaryOperator::Subtract:
              return is_double ? builder.CreateFSub(left, right, "sub")
                               : builder.CreateSub(left, right, "sub");
            case janus::ast::BinaryOperator::Multiply:
              return is_double ? builder.CreateFMul(left, right, "mul")
                               : builder.CreateMul(left, right, "mul");
            case janus::ast::BinaryOperator::Divide:
              return is_double ? builder.CreateFDiv(left, right, "div")
                               : builder.CreateSDiv(left, right, "div");
            case janus::ast::BinaryOperator::Remainder:
              return builder.CreateSRem(left, right, "rem");
            case janus::ast::BinaryOperator::Less:
              if (is_double)
                return builder.CreateFCmpOLT(left, right, "cmp");
              return operand_type.kind() == janus::TypeKind::Char
                         ? builder.CreateICmpULT(left, right, "cmp")
                         : builder.CreateICmpSLT(left, right, "cmp");
            case janus::ast::BinaryOperator::LessEqual:
              if (is_double)
                return builder.CreateFCmpOLE(left, right, "cmp");
              return operand_type.kind() == janus::TypeKind::Char
                         ? builder.CreateICmpULE(left, right, "cmp")
                         : builder.CreateICmpSLE(left, right, "cmp");
            case janus::ast::BinaryOperator::Greater:
              if (is_double)
                return builder.CreateFCmpOGT(left, right, "cmp");
              return operand_type.kind() == janus::TypeKind::Char
                         ? builder.CreateICmpUGT(left, right, "cmp")
                         : builder.CreateICmpSGT(left, right, "cmp");
            case janus::ast::BinaryOperator::GreaterEqual:
              if (is_double)
                return builder.CreateFCmpOGE(left, right, "cmp");
              return operand_type.kind() == janus::TypeKind::Char
                         ? builder.CreateICmpUGE(left, right, "cmp")
                         : builder.CreateICmpSGE(left, right, "cmp");
            case janus::ast::BinaryOperator::Equal:
            case janus::ast::BinaryOperator::NotEqual: {
              const bool is_not_equal =
                  node.operation == janus::ast::BinaryOperator::NotEqual;
              if (is_double) {
                return is_not_equal
                           ? builder.CreateFCmpUNE(left, right, "equal")
                           : builder.CreateFCmpOEQ(left, right, "equal");
              }
              if (operand_type.kind() != janus::TypeKind::String) {
                return is_not_equal
                           ? builder.CreateICmpNE(left, right, "equal")
                           : builder.CreateICmpEQ(left, right, "equal");
              }

              ::llvm::Value *left_data =
                  builder.CreateExtractValue(left, 0, "string.left.data");
              ::llvm::Value *left_length =
                  builder.CreateExtractValue(left, 1, "string.left.length");
              ::llvm::Value *right_data =
                  builder.CreateExtractValue(right, 0, "string.right.data");
              ::llvm::Value *right_length =
                  builder.CreateExtractValue(right, 1, "string.right.length");
              ::llvm::Value *same_length = builder.CreateICmpEQ(
                  left_length, right_length, "same.length");
              ::llvm::BasicBlock *length_block = builder.GetInsertBlock();
              ::llvm::Function *function = length_block->getParent();
              auto *compare_block = ::llvm::BasicBlock::Create(
                  context_, "string.compare", function);
              auto *merge_block = ::llvm::BasicBlock::Create(
                  context_, "string.equal", function);
              builder.CreateCondBr(same_length, compare_block, merge_block);

              builder.SetInsertPoint(compare_block);
              ::llvm::FunctionCallee memcmp_function =
                  module_->getOrInsertFunction(
                      "memcmp", ::llvm::FunctionType::get(
                                    builder.getInt32Ty(),
                                    {builder.getPtrTy(), builder.getPtrTy(),
                                     builder.getInt64Ty()},
                                    false));
              ::llvm::Value *comparison = builder.CreateCall(
                  memcmp_function, {left_data, right_data, left_length},
                  "memcmp");
              ::llvm::Value *same_bytes = builder.CreateICmpEQ(
                  comparison, builder.getInt32(0), "same.bytes");
              builder.CreateBr(merge_block);

              builder.SetInsertPoint(merge_block);
              auto *equal =
                  builder.CreatePHI(builder.getInt1Ty(), 2, "string.equals");
              equal->addIncoming(builder.getFalse(), length_block);
              equal->addIncoming(same_bytes, compare_block);
              return is_not_equal ? builder.CreateNot(equal, "not.equal")
                                  : equal;
            }
            case janus::ast::BinaryOperator::LogicalAnd:
            case janus::ast::BinaryOperator::LogicalOr:
              return nullptr;
            }
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
  std::unordered_map<std::string, ClassSpecialization> class_specializations_;
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
