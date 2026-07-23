#include "janus/backend/llvm/ir_generator.hpp"

#include "janus/backend/llvm/type_lowering.hpp"
#include "janus/constant/evaluator.hpp"
#include "janus/diagnostics/compile_error.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <functional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <llvm/ADT/APInt.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>

namespace {

const janus::Type *builtin_type(std::string_view name) {
  if (name == "int")
    return &janus::Type::int_type();
  if (name == "uint")
    return &janus::Type::uint_type();
  if (name == "long")
    return &janus::Type::long_type();
  if (name == "ulong")
    return &janus::Type::ulong_type();
  if (name == "float")
    return &janus::Type::float_type();
  if (name == "double")
    return &janus::Type::double_type();
  if (name == "byte")
    return &janus::Type::byte_type();
  if (name == "ubyte")
    return &janus::Type::ubyte_type();
  if (name == "short")
    return &janus::Type::short_type();
  if (name == "ushort")
    return &janus::Type::ushort_type();
  if (name == "char")
    return &janus::Type::char_type();
  if (name == "bool")
    return &janus::Type::bool_type();
  if (name == "string")
    return &janus::Type::string_type();
  if (name == "Unit")
    return &janus::Type::unit_type();
  if (name == "isize")
    return &janus::Type::isize_type();
  if (name == "usize")
    return &janus::Type::usize_type();
  return nullptr;
}

class Generator {
public:
  Generator(::llvm::LLVMContext &context, const janus::ast::Program &program,
            std::string_view module_name)
      : context_{context}, module_{std::make_unique<::llvm::Module>(
                               std::string{module_name}, context)} {
#if LLVM_VERSION_MAJOR >= 21
    module_->setTargetTriple(
        ::llvm::Triple{::llvm::sys::getDefaultTargetTriple()});
#else
    module_->setTargetTriple(::llvm::sys::getDefaultTargetTriple());
#endif
    module_->setPICLevel(::llvm::PICLevel::BigPIC);
    module_->setPIELevel(::llvm::PIELevel::Large);
    for (const janus::ast::EnumDeclaration &enum_declaration : program.enums)
      enums_.emplace(enum_declaration.name, &enum_declaration);
    for (const janus::ast::ClassDeclaration &class_declaration :
         program.classes) {
      classes_.emplace(class_declaration.name, &class_declaration);
    }
    for (const janus::ast::FunctionDeclaration &function : program.functions)
      functions_.emplace(function.name, &function);
    for (const janus::ast::GlobalDeclaration &global : program.globals) {
      global_declarations_.push_back(&global);
      global_by_key_.emplace(
          source_global_key(global.module_name, global.declaration.name),
          &global);
      if (!global.declaration.is_private)
        public_global_keys_.emplace(
            global.declaration.name,
            source_global_key(global.module_name, global.declaration.name));
      if (global.module_name.has_value())
        global_modules_.insert(*global.module_name);
    }
    initialization_plan_ =
        janus::constant::plan_initialization(program.globals);
    for (const janus::ast::GlobalDeclaration *global :
         initialization_plan_.constants)
      constant_global_keys_.insert(
          source_global_key(global->module_name, global->declaration.name));
  }

  std::unique_ptr<::llvm::Module> generate() {
    for (const janus::ast::GlobalDeclaration *declaration :
         global_declarations_)
      emit_global(*declaration);
    emit_global_initializer_function();
    emit_global_finalizer_function();
    for (const auto &[name, class_declaration] : classes_) {
      if (class_declaration->type_parameters.empty())
        static_cast<void>(ensure_class(name, {}));
    }
    for (const auto &[name, function] : functions_) {
      static_cast<void>(name);
      if (function->type_parameters.empty())
        static_cast<void>(emit_function(*function, {}));
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

  struct EnumSpecialization {
    const janus::ast::EnumDeclaration *declaration;
    Substitutions substitutions;
  };

  struct FunctionSignature {
    std::vector<const janus::Type *> parameters;
    const janus::Type *return_type;
  };

  struct CleanupScope {
    const std::vector<const janus::ast::DeferStatement *> *actions;
    std::unordered_map<std::string, Local> *locals;
    const Substitutions *substitutions;
  };

  const janus::Type &resolve(const janus::ast::TypeReference &reference,
                             const Substitutions &substitutions) {
    if (const janus::Type *type = builtin_type(reference.name))
      return *type;
    if (reference.name == "Function") {
      std::vector<const janus::Type *> signature;
      signature.reserve(reference.type_arguments.size());
      for (const janus::ast::TypeReference &argument : reference.type_arguments)
        signature.push_back(&resolve(argument, substitutions));
      std::vector<const janus::Type *> parameters{signature.begin(),
                                                  signature.end() - 1};
      return ensure_function_type(parameters, *signature.back());
    }
    if (const auto iterator = substitutions.find(reference.name);
        iterator != substitutions.end())
      return *iterator->second;
    if (enums_.contains(reference.name)) {
      std::vector<const janus::Type *> arguments;
      for (const janus::ast::TypeReference &argument : reference.type_arguments)
        arguments.push_back(&resolve(argument, substitutions));
      return ensure_enum(reference.name, arguments);
    }
    if (reference.name == "Ptr")
      return ensure_pointer(
          resolve(reference.type_arguments.front(), substitutions));
    std::vector<const janus::Type *> type_arguments;
    type_arguments.reserve(reference.type_arguments.size());
    for (const janus::ast::TypeReference &argument : reference.type_arguments)
      type_arguments.push_back(&resolve(argument, substitutions));
    return ensure_class(reference.name, type_arguments);
  }

  const Local *find_storage(
      std::string_view name,
      const std::unordered_map<std::string, Local> &locals) const {
    if (const auto local = locals.find(std::string{name});
        local != locals.end())
      return &local->second;
    const std::string local_key = source_global_key(active_module_, name);
    if (const auto local_global = global_storage_.find(local_key);
        local_global != global_storage_.end())
      return &local_global->second;
    const auto exported = public_global_keys_.find(std::string{name});
    if (exported == public_global_keys_.end())
      return nullptr;
    return &global_storage_.at(exported->second);
  }

  const Local &resolve_storage(
      std::string_view name,
      const std::unordered_map<std::string, Local> &locals) const {
    return *find_storage(name, locals);
  }

  const Local &resolve_qualified_global(std::string_view module,
                                        std::string_view name) const {
    return global_storage_.at(
        source_global_key(std::optional<std::string>{module}, name));
  }

  static std::string
  source_global_key(const std::optional<std::string> &module,
                    std::string_view name) {
    return module.has_value() ? *module + "." + std::string{name}
                              : std::string{name};
  }

  static std::optional<std::string>
  qualified_expression_name(const janus::ast::Expression &expression) {
    if (const auto *identifier =
            std::get_if<janus::ast::IdentifierExpression>(&expression.value))
      return identifier->name;
    if (const auto *member =
            std::get_if<janus::ast::MemberAccessExpression>(
                &expression.value)) {
      if (auto prefix = qualified_expression_name(*member->object))
        return *prefix + "." + member->member;
    }
    return std::nullopt;
  }

  static std::string
  global_symbol_name(const janus::ast::GlobalDeclaration &global) {
    std::string result{"__janus_global_"};
    const std::string module =
        global.module_name.has_value() ? *global.module_name : "entry";
    for (const unsigned char character : module)
      result += std::isalnum(character) || character == '_' ? character : '_';
    result += "__" + global.declaration.name;
    return result;
  }

  const janus::constant::Value &
  evaluate_global_constant(const janus::ast::GlobalDeclaration &global) {
    const std::string key =
        source_global_key(global.module_name, global.declaration.name);
    const int state = constant_states_[key];
    if (state == 1)
      throw janus::CompileError{
          global.declaration.location,
          "cyclic global constant dependency involving '" + key + "'"};
    if (state == 2)
      return constant_values_.at(key);

    constant_states_[key] = 1;
    const janus::constant::Resolver resolver =
        [&](const std::optional<std::string> &qualified_module,
            std::string_view name,
            janus::SourceLocation location)
        -> std::optional<janus::constant::Value> {
      std::string dependency_key;
      if (qualified_module.has_value()) {
        dependency_key = source_global_key(qualified_module, name);
      } else {
        const std::string local_key =
            source_global_key(global.module_name, name);
        if (global_by_key_.contains(local_key))
          dependency_key = local_key;
        else if (const auto exported =
                     public_global_keys_.find(std::string{name});
                 exported != public_global_keys_.end())
          dependency_key = exported->second;
        else
          return std::nullopt;
      }
      const auto dependency = global_by_key_.find(dependency_key);
      if (dependency == global_by_key_.end())
        return std::nullopt;
      const janus::ast::GlobalDeclaration &target = *dependency->second;
      if (target.declaration.is_private &&
          target.module_name != global.module_name)
        throw janus::CompileError{location,
                                  "global constant '" + dependency_key +
                                      "' is private"};
      if (target.declaration.is_mutable)
        throw janus::CompileError{
            location, "global constant initializer cannot depend on mutable "
                      "global '" +
                          dependency_key + "'"};
      return evaluate_global_constant(target);
    };
    const janus::Type &type = resolve(global.declaration.declared_type, {});
    janus::constant::Value value = janus::constant::evaluate(
        *global.declaration.initializer, &type, resolver);
    constant_states_[key] = 2;
    auto [iterator, inserted] =
        constant_values_.emplace(key, std::move(value));
    static_cast<void>(inserted);
    return iterator->second;
  }

  ::llvm::Constant *
  emit_static_initializer(const janus::constant::Value &value,
                          const janus::Type &type) {
    ::llvm::Type *llvm_type = lower_type(type, context_);
    if (type.is_integer())
      return ::llvm::ConstantInt::get(
          llvm_type, std::get<std::uint64_t>(value.data), type.is_signed());
    if (type.is_floating_point())
      return ::llvm::ConstantFP::get(llvm_type, std::get<double>(value.data));
    if (type.kind() == janus::TypeKind::Bool)
      return ::llvm::ConstantInt::get(
          llvm_type, std::get<bool>(value.data), false);
    if (type.kind() == janus::TypeKind::Char)
      return ::llvm::ConstantInt::get(
          llvm_type,
          static_cast<std::uint32_t>(std::get<char32_t>(value.data)), false);

    const std::string &literal = std::get<std::string>(value.data);
    ::llvm::Constant *data =
        ::llvm::ConstantDataArray::getString(context_, literal, true);
    auto *storage = new ::llvm::GlobalVariable(
        *module_, data->getType(), true, ::llvm::GlobalValue::PrivateLinkage,
        data, ".str." + std::to_string(string_literal_index_++));
    storage->setUnnamedAddr(::llvm::GlobalValue::UnnamedAddr::Global);
    ::llvm::Constant *zero =
        ::llvm::ConstantInt::get(::llvm::Type::getInt32Ty(context_), 0);
    const std::array<::llvm::Constant *, 2> indices{zero, zero};
    ::llvm::Constant *pointer =
        ::llvm::ConstantExpr::getInBoundsGetElementPtr(data->getType(), storage,
                                                       indices);
    ::llvm::Constant *length = ::llvm::ConstantInt::get(
        ::llvm::Type::getInt64Ty(context_), literal.size(), false);
    return ::llvm::ConstantStruct::get(
        ::llvm::cast<::llvm::StructType>(llvm_type), {pointer, length});
  }

  void emit_global(const janus::ast::GlobalDeclaration &global) {
    const janus::ast::ValueDeclaration &declaration = global.declaration;
    const janus::Type &type = resolve(declaration.declared_type, {});
    const bool is_constant = constant_global_keys_.contains(
        source_global_key(global.module_name, declaration.name));
    const auto linkage = declaration.is_private
                             ? ::llvm::GlobalValue::InternalLinkage
                             : ::llvm::GlobalValue::ExternalLinkage;
    ::llvm::Constant *initializer =
        is_constant
            ? emit_static_initializer(evaluate_global_constant(global), type)
            : ::llvm::Constant::getNullValue(lower_type(type, context_));
    auto *storage = new ::llvm::GlobalVariable(
        *module_, lower_type(type, context_),
        is_constant && !declaration.is_mutable, linkage, initializer,
        global_symbol_name(global));
    global_storage_.emplace(
        source_global_key(global.module_name, declaration.name),
        Local{storage, &type});
  }

  void emit_global_initializer_function() {
    if (initialization_plan_.dynamic.empty())
      return;

    auto *function_type =
        ::llvm::FunctionType::get(::llvm::Type::getVoidTy(context_), false);
    global_initializer_ = ::llvm::Function::Create(
        function_type, ::llvm::Function::InternalLinkage,
        "__janus_init_globals", *module_);
    auto *entry =
        ::llvm::BasicBlock::Create(context_, "entry", global_initializer_);
    ::llvm::IRBuilder<> builder{entry};
    const Substitutions substitutions;
    const std::unordered_map<std::string, Local> locals;
    const auto previous_module = active_module_;
    const janus::Type *previous_return_type = active_return_type_;
    active_return_type_ = &janus::Type::unit_type();
    for (const janus::ast::GlobalDeclaration *global :
         initialization_plan_.dynamic) {
      active_module_ = global->module_name;
      const janus::Type &type =
          resolve(global->declaration.declared_type, substitutions);
      ::llvm::Value *value =
          emit_expression(*global->declaration.initializer, type,
                          substitutions, locals, builder);
      const Local &storage = global_storage_.at(source_global_key(
          global->module_name, global->declaration.name));
      builder.CreateStore(value, storage.storage);
    }
    builder.CreateRetVoid();
    active_module_ = previous_module;
    active_return_type_ = previous_return_type;
  }

  void emit_global_finalizer_function() {
    const bool has_owned = std::any_of(
        global_declarations_.begin(), global_declarations_.end(),
        [&](const janus::ast::GlobalDeclaration *global) {
          const janus::Type &type =
              resolve(global->declaration.declared_type, {});
          return type.kind() == janus::TypeKind::Class ||
                 type.kind() == janus::TypeKind::Pointer ||
                 type.kind() == janus::TypeKind::Function;
        });
    if (!has_owned)
      return;

    auto *function_type =
        ::llvm::FunctionType::get(::llvm::Type::getVoidTy(context_), false);
    global_finalizer_ = ::llvm::Function::Create(
        function_type, ::llvm::Function::InternalLinkage,
        "__janus_fini_globals", *module_);
    auto *entry =
        ::llvm::BasicBlock::Create(context_, "entry", global_finalizer_);
    ::llvm::IRBuilder<> builder{entry};
    ::llvm::FunctionCallee free_function = module_->getOrInsertFunction(
        "janus_free", ::llvm::FunctionType::get(builder.getVoidTy(),
                                           {builder.getPtrTy()}, false));

    for (auto iterator = initialization_plan_.dynamic.rbegin();
         iterator != initialization_plan_.dynamic.rend(); ++iterator) {
      const janus::ast::GlobalDeclaration &global = **iterator;
      const janus::Type &type = resolve(global.declaration.declared_type, {});
      if (type.kind() != janus::TypeKind::Class &&
          type.kind() != janus::TypeKind::Pointer &&
          type.kind() != janus::TypeKind::Function)
        continue;
      const Local &storage = global_storage_.at(
          source_global_key(global.module_name, global.declaration.name));
      ::llvm::Value *value = builder.CreateLoad(
          lower_type(type, context_), storage.storage,
          global.declaration.name + ".global.cleanup");
      ::llvm::Value *pointer = value;
      if (type.kind() == janus::TypeKind::Class)
        builder.CreateCall(emit_destructor(std::string{type.name()}), {value});
      else if (type.kind() == janus::TypeKind::Function)
        pointer =
            builder.CreateExtractValue(value, 1, "global.lambda.environment");
      builder.CreateCall(free_function, {pointer});
    }
    builder.CreateRetVoid();
  }

  const janus::Type &ensure_pointer(const janus::Type &element_type) {
    const std::string key = "Ptr__" + std::string{element_type.name()};
    if (const auto iterator = pointer_types_.find(key);
        iterator != pointer_types_.end())
      return iterator->second;
    auto [iterator, inserted] =
        pointer_types_.emplace(key, janus::Type::pointer_type(key));
    static_cast<void>(inserted);
    pointer_elements_.emplace(key, &element_type);
    return iterator->second;
  }

  const janus::Type &pointer_element(const janus::Type &pointer_type) const {
    return *pointer_elements_.at(std::string{pointer_type.name()});
  }

  std::string function_key(const std::vector<const janus::Type *> &parameters,
                           const janus::Type &return_type) const {
    std::string key{"Function"};
    for (const janus::Type *parameter : parameters)
      key += "__" + std::string{parameter->name()};
    key += "__to__" + std::string{return_type.name()};
    return key;
  }

  const janus::Type &
  ensure_function_type(const std::vector<const janus::Type *> &parameters,
                       const janus::Type &return_type) {
    const std::string key = function_key(parameters, return_type);
    if (const auto iterator = function_types_.find(key);
        iterator != function_types_.end())
      return iterator->second;
    auto [iterator, inserted] =
        function_types_.emplace(key, janus::Type::function_type(key));
    static_cast<void>(inserted);
    function_signatures_.emplace(key,
                                 FunctionSignature{parameters, &return_type});
    return iterator->second;
  }

  const FunctionSignature &function_signature(const janus::Type &type) const {
    return function_signatures_.at(std::string{type.name()});
  }

  std::string
  enum_key(std::string_view name,
           const std::vector<const janus::Type *> &type_arguments) const {
    std::string key{name};
    for (const janus::Type *argument : type_arguments)
      key += "__" + std::string{argument->name()};
    return key;
  }

  const janus::Type &
  ensure_enum(std::string_view name,
              const std::vector<const janus::Type *> &type_arguments) {
    const std::string key = enum_key(name, type_arguments);
    if (const auto iterator = enum_types_.find(key);
        iterator != enum_types_.end())
      return iterator->second;
    auto [type_iterator, inserted] =
        enum_types_.emplace(key, janus::Type::enum_type(key));
    static_cast<void>(inserted);
    ::llvm::StructType *llvm_type =
        ::llvm::StructType::create(context_, "enum." + key);
    llvm_enum_types_.emplace(key, llvm_type);

    const janus::ast::EnumDeclaration &declaration =
        *enums_.at(std::string{name});
    Substitutions substitutions;
    for (std::size_t index = 0; index < type_arguments.size(); ++index)
      substitutions.emplace(declaration.type_parameters[index],
                            type_arguments[index]);
    enum_specializations_.emplace(
        key, EnumSpecialization{&declaration, substitutions});

    std::vector<::llvm::Type *> fields{::llvm::Type::getInt32Ty(context_)};
    for (const janus::ast::EnumDeclaration::Case &enum_case : declaration.cases)
      for (const janus::ast::TypeReference &payload : enum_case.payload_types)
        fields.push_back(lower_type(resolve(payload, substitutions), context_));
    llvm_type->setBody(fields);
    return type_iterator->second;
  }

  ::llvm::Type *lower_type(const janus::Type &type,
                           ::llvm::LLVMContext &context) {
    if (type.kind() == janus::TypeKind::Enum)
      return llvm_enum_types_.at(std::string{type.name()});
    if (type.kind() == janus::TypeKind::Struct)
      return llvm_class_types_.at(std::string{type.name()});
    return janus::backend::llvm::lower_type(type, context);
  }

  std::int32_t enum_case_value(std::string_view enum_name,
                               std::string_view case_name) const {
    const janus::ast::EnumDeclaration &declaration =
        *enum_specializations_.at(std::string{enum_name}).declaration;
    const auto iterator =
        std::find_if(declaration.cases.begin(), declaration.cases.end(),
                     [&](const janus::ast::EnumDeclaration::Case &item) {
                       return item.name == case_name;
                     });
    return iterator->value;
  }

  unsigned enum_case_payload_start(std::string_view enum_name,
                                   std::string_view case_name) const {
    const janus::ast::EnumDeclaration &declaration =
        *enum_specializations_.at(std::string{enum_name}).declaration;
    unsigned index = 1;
    for (const janus::ast::EnumDeclaration::Case &enum_case :
         declaration.cases) {
      if (enum_case.name == case_name)
        return index;
      index += static_cast<unsigned>(enum_case.payload_types.size());
    }
    return index;
  }

  bool is_explicit_cast(const janus::ast::CallExpression &call) const {
    const janus::Type *type = builtin_type(call.callee);
    if (type != nullptr)
      return type->kind() != janus::TypeKind::String &&
             type->kind() != janus::TypeKind::Unit;
    return call.callee == "Ptr" || classes_.contains(call.callee) ||
           enums_.contains(call.callee);
  }

  const janus::Type &cast_destination(const janus::ast::CallExpression &call,
                                      const Substitutions &substitutions) {
    return resolve(janus::ast::TypeReference{call.callee, call.location,
                                             call.type_arguments},
                   substitutions);
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

    auto [type_iterator, inserted] = class_types_.emplace(
        key, declaration.is_value_type ? janus::Type::struct_type(key)
                                       : janus::Type::class_type(key));
    static_cast<void>(inserted);
    ::llvm::StructType *llvm_class_type =
        ::llvm::StructType::create(
            context_,
            std::string{declaration.is_value_type ? "struct." : "class."} +
                key);
    llvm_class_types_.emplace(key, llvm_class_type);
    class_specializations_.emplace(
        key, ClassSpecialization{&declaration, substitutions});

    std::vector<::llvm::Type *> fields;
    for (const auto &field : declaration.constructor_fields)
      fields.push_back(
          lower_type(resolve(field.declared_type, substitutions), context_));
    for (const auto &field : declaration.fields)
      fields.push_back(
          lower_type(resolve(field.declared_type, substitutions), context_));
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

  void
  emit_cleanup_action(const janus::ast::DeferStatement &deferred,
                      const Substitutions &substitutions,
                      std::unordered_map<std::string, Local> &cleanup_locals,
                      ::llvm::IRBuilder<> &builder) {
    if (const auto *deletion =
            std::get_if<janus::ast::DeleteStatement>(&deferred.action)) {
      const janus::Type &deleted_type =
          expression_type(deletion->expression, substitutions, cleanup_locals);
      ::llvm::Value *deleted_value =
          emit_expression(deletion->expression, deleted_type, substitutions,
                          cleanup_locals, builder);
      ::llvm::Value *pointer = deleted_value;
      if (deleted_type.kind() == janus::TypeKind::Function) {
        pointer =
            builder.CreateExtractValue(deleted_value, 1, "lambda.environment");
      } else {
        builder.CreateCall(emit_destructor(std::string{deleted_type.name()}),
                           {pointer});
      }
      ::llvm::FunctionCallee free_function = module_->getOrInsertFunction(
          "janus_free", ::llvm::FunctionType::get(builder.getVoidTy(),
                                            {builder.getPtrTy()}, false));
      builder.CreateCall(free_function, {pointer});
      return;
    }
    const auto &action =
        std::get<janus::ast::ExpressionStatement>(deferred.action);
    const janus::Type &type =
        expression_type(action.expression, substitutions, cleanup_locals);
    static_cast<void>(emit_expression(action.expression, type, substitutions,
                                      cleanup_locals, builder));
  }

  void emit_cleanups_from_depth(::llvm::IRBuilder<> &builder,
                                std::size_t retained_depth) {
    for (std::size_t index = active_cleanup_scopes_.size();
         index > retained_depth; --index) {
      const CleanupScope &scope = active_cleanup_scopes_[index - 1];
      for (auto action = scope.actions->rbegin();
           action != scope.actions->rend(); ++action)
        emit_cleanup_action(**action, *scope.substitutions, *scope.locals,
                            builder);
    }
  }

  void emit_active_cleanups(::llvm::IRBuilder<> &builder) {
    emit_cleanups_from_depth(builder, 0);
  }

  ::llvm::Function *emit_function(
      const janus::ast::FunctionDeclaration &function,
      const std::vector<const janus::Type *> &type_arguments,
      const janus::ast::ClassDeclaration *owner = nullptr,
      const Substitutions *owner_substitutions = nullptr,
      std::string_view owner_key = {},
      const std::vector<janus::ast::Statement> *body_override = nullptr) {
    const std::string llvm_name =
        function.is_external && function.external_symbol.has_value()
            ? *function.external_symbol
            : (owner == nullptr ? std::string{}
                                : std::string{owner_key} + "__") +
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
      parameter_types.push_back(
          lower_type(resolve(parameter.type, substitutions), context_));

    auto *function_type = ::llvm::FunctionType::get(
        lower_type(return_type, context_), parameter_types,
        function.is_variadic);
    const ::llvm::GlobalValue::LinkageTypes linkage =
        owner != nullptr &&
                (function.is_private || function.name == "destructor")
            ? ::llvm::Function::InternalLinkage
            : ::llvm::Function::ExternalLinkage;
    auto *llvm_function =
        ::llvm::Function::Create(function_type, linkage, llvm_name, *module_);
    emitted_.emplace(llvm_name, llvm_function);
    if (function.is_external)
      return llvm_function;

    const auto previous_active_module = active_module_;
    active_module_ =
        owner == nullptr ? function.module_name : owner->module_name;
    auto previous_cleanup_scopes = std::move(active_cleanup_scopes_);
    active_cleanup_scopes_.clear();

    auto *entry = ::llvm::BasicBlock::Create(context_, "entry", llvm_function);
    ::llvm::IRBuilder<> builder{entry};
    std::unordered_map<std::string, Local> locals;
    if (owner == nullptr && function.name == "main" &&
        global_initializer_ != nullptr)
      builder.CreateCall(global_initializer_);

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
      ::llvm::Value *storage = builder.CreateAlloca(lower_type(type, context_),
                                                    nullptr, parameter.name);
      builder.CreateStore(&argument, storage);
      locals.emplace(parameter.name, Local{storage, &type});
    }

    std::function<bool(const std::vector<janus::ast::Statement> &,
                       std::unordered_map<std::string, Local> &)>
        emit_block;
    struct LoopTarget {
      ::llvm::BasicBlock *break_block;
      ::llvm::BasicBlock *continue_block;
      std::size_t cleanup_depth;
    };
    std::vector<LoopTarget> loop_targets;
    emit_block = [&](const std::vector<janus::ast::Statement> &statements,
                     std::unordered_map<std::string, Local> &block_locals) {
      std::vector<const janus::ast::DeferStatement *> deferred_actions;
      active_cleanup_scopes_.push_back(
          CleanupScope{&deferred_actions, &block_locals, &substitutions});
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
            active_cleanup_scopes_.pop_back();
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
          loop_targets.push_back(LoopTarget{
              exit_block, condition_block, active_cleanup_scopes_.size()});
          const bool body_returns = emit_block((*loop)->body, body_locals);
          loop_targets.pop_back();
          if (!body_returns)
            builder.CreateBr(condition_block);
          builder.SetInsertPoint(exit_block);
          continue;
        }

        if (const auto *loop =
                std::get_if<std::shared_ptr<janus::ast::ForStatement>>(
                    &statement)) {
          const janus::Type &source_type =
              expression_type((*loop)->iterator, substitutions, block_locals);
          ::llvm::Value *source =
              emit_expression((*loop)->iterator, source_type, substitutions,
                              block_locals, builder);
          const janus::Type *iterator_type = &source_type;
          ::llvm::Value *iterator = source;
          const ClassSpecialization &source_specialization =
              class_specializations_.at(std::string{source_type.name()});
          if (source_specialization.declaration->name != "Iterator") {
            const janus::ast::FunctionDeclaration *iterator_method = nullptr;
            for (const janus::ast::FunctionDeclaration &method :
                 source_specialization.declaration->methods)
              if (method.name == "iterator")
                iterator_method = &method;
            ::llvm::Function *iterator_function = emit_function(
                *iterator_method, {}, source_specialization.declaration,
                &source_specialization.substitutions, source_type.name());
            iterator_type = &resolve(iterator_method->return_type,
                                     source_specialization.substitutions);
            iterator =
                builder.CreateCall(iterator_function, {source}, "for.iterator");
          }
          const ClassSpecialization &iterator_specialization =
              class_specializations_.at(std::string{iterator_type->name()});
          const auto &iterator_declaration =
              *iterator_specialization.declaration;
          const janus::ast::FunctionDeclaration *next_method = nullptr;
          for (const janus::ast::FunctionDeclaration &method :
               iterator_declaration.methods)
            if (method.name == "next")
              next_method = &method;
          ::llvm::Function *next_function = emit_function(
              *next_method, {}, &iterator_declaration,
              &iterator_specialization.substitutions, iterator_type->name());
          const janus::Type &option_type = resolve(
              next_method->return_type, iterator_specialization.substitutions);
          const EnumSpecialization &option_specialization =
              enum_specializations_.at(std::string{option_type.name()});
          const auto some_case = std::find_if(
              option_specialization.declaration->cases.begin(),
              option_specialization.declaration->cases.end(),
              [](const janus::ast::EnumDeclaration::Case &candidate) {
                return candidate.name == "Some";
              });
          const janus::Type &element_type =
              resolve(some_case->payload_types.front(),
                      option_specialization.substitutions);

          ::llvm::Function *current_function =
              builder.GetInsertBlock()->getParent();
          auto *condition_block = ::llvm::BasicBlock::Create(
              context_, "for.next", current_function);
          auto *body_block = ::llvm::BasicBlock::Create(context_, "for.body",
                                                        current_function);
          auto *exit_block =
              ::llvm::BasicBlock::Create(context_, "for.end", current_function);
          builder.CreateBr(condition_block);
          builder.SetInsertPoint(condition_block);
          ::llvm::Value *next =
              builder.CreateCall(next_function, {iterator}, "for.option");
          ::llvm::Value *tag =
              builder.CreateExtractValue(next, 0, "for.option.tag");
          builder.CreateCondBr(
              builder.CreateICmpEQ(tag, builder.getInt32(enum_case_value(
                                            option_type.name(), "Some"))),
              body_block, exit_block);

          builder.SetInsertPoint(body_block);
          ::llvm::Value *item = builder.CreateExtractValue(
              next, enum_case_payload_start(option_type.name(), "Some"),
              (*loop)->binding + ".item");
          ::llvm::Value *storage = builder.CreateAlloca(
              lower_type(element_type, context_), nullptr, (*loop)->binding);
          builder.CreateStore(item, storage);
          auto body_locals = block_locals;
          body_locals.insert_or_assign((*loop)->binding,
                                       Local{storage, &element_type});
          loop_targets.push_back(LoopTarget{
              exit_block, condition_block, active_cleanup_scopes_.size()});
          const bool body_returns = emit_block((*loop)->body, body_locals);
          loop_targets.pop_back();
          if (!body_returns)
            builder.CreateBr(condition_block);

          builder.SetInsertPoint(exit_block);
          builder.CreateCall(
              emit_destructor(std::string{iterator_type->name()}), {iterator});
          ::llvm::FunctionCallee free_function = module_->getOrInsertFunction(
              "janus_free", ::llvm::FunctionType::get(builder.getVoidTy(),
                                                {builder.getPtrTy()}, false));
          builder.CreateCall(free_function, {iterator});
          continue;
        }

        if (const auto *declaration =
                std::get_if<janus::ast::ValueDeclaration>(&statement)) {
          const janus::Type &type =
              resolve(declaration->declared_type, substitutions);
          ::llvm::Value *storage = builder.CreateAlloca(
              lower_type(type, context_), nullptr, declaration->name);
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
            if (!block_locals.contains(assignment->object) &&
                global_modules_.contains(assignment->object)) {
              const Local &global = resolve_qualified_global(
                  assignment->object, assignment->name);
              builder.CreateStore(
                  emit_expression(assignment->expression, *global.type,
                                  substitutions, block_locals, builder),
                  global.storage);
              continue;
            }
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
          const Local &local =
              resolve_storage(assignment->name, block_locals);
          ::llvm::Value *value =
              emit_expression(assignment->expression, *local.type,
                              substitutions, block_locals, builder);
          builder.CreateStore(value, local.storage);
          continue;
        }

        if (const auto *deletion =
                std::get_if<janus::ast::DeleteStatement>(&statement)) {
          const janus::Type &deleted_type = expression_type(
              deletion->expression, substitutions, block_locals);
          ::llvm::Value *deleted_value =
              emit_expression(deletion->expression, deleted_type, substitutions,
                              block_locals, builder);
          ::llvm::Value *pointer = deleted_value;
          if (deleted_type.kind() == janus::TypeKind::Function) {
            pointer = builder.CreateExtractValue(deleted_value, 1,
                                                 "lambda.environment");
          } else {
            builder.CreateCall(
                emit_destructor(std::string{deleted_type.name()}), {pointer});
          }
          ::llvm::FunctionCallee free_function = module_->getOrInsertFunction(
              "janus_free", ::llvm::FunctionType::get(builder.getVoidTy(),
                                                {builder.getPtrTy()}, false));
          builder.CreateCall(free_function, {pointer});
          continue;
        }

        if (const auto *expression_statement =
                std::get_if<janus::ast::ExpressionStatement>(&statement)) {
          const janus::Type &type = expression_type(
              expression_statement->expression, substitutions, block_locals);
          static_cast<void>(emit_expression(expression_statement->expression,
                                            type, substitutions, block_locals,
                                            builder));
          continue;
        }

        if (const auto *deferred =
                std::get_if<janus::ast::DeferStatement>(&statement)) {
          deferred_actions.push_back(deferred);
          continue;
        }

        if (std::holds_alternative<janus::ast::BreakStatement>(statement)) {
          emit_cleanups_from_depth(builder,
                                   loop_targets.back().cleanup_depth);
          builder.CreateBr(loop_targets.back().break_block);
          active_cleanup_scopes_.pop_back();
          return true;
        }

        if (std::holds_alternative<janus::ast::ContinueStatement>(statement)) {
          emit_cleanups_from_depth(builder,
                                   loop_targets.back().cleanup_depth);
          builder.CreateBr(loop_targets.back().continue_block);
          active_cleanup_scopes_.pop_back();
          return true;
        }

        const auto &return_statement =
            std::get<janus::ast::ReturnStatement>(statement);
        ::llvm::Value *return_value = nullptr;
        if (return_statement.expression.has_value())
          return_value =
              emit_expression(*return_statement.expression, return_type,
                              substitutions, block_locals, builder);
        emit_active_cleanups(builder);
        if (owner == nullptr && function.name == "main" &&
            global_finalizer_ != nullptr)
          builder.CreateCall(global_finalizer_);
        if (return_value != nullptr)
          builder.CreateRet(return_value);
        else
          builder.CreateRetVoid();
        active_cleanup_scopes_.pop_back();
        return true;
      }
      for (auto iterator = deferred_actions.rbegin();
           iterator != deferred_actions.rend(); ++iterator)
        emit_cleanup_action(**iterator, substitutions, block_locals, builder);
      active_cleanup_scopes_.pop_back();
      return false;
    };
    const janus::Type *previous_return_type = active_return_type_;
    active_return_type_ = &return_type;
    const bool emitted_return = emit_block(
        body_override == nullptr ? function.body : *body_override, locals);
    active_return_type_ = previous_return_type;
    if (!emitted_return && return_type.kind() == janus::TypeKind::Unit)
      builder.CreateRetVoid();
    active_cleanup_scopes_ = std::move(previous_cleanup_scopes);
    active_module_ = previous_active_module;
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
    const ClassSpecialization &specialization =
        class_specializations_.at(class_name);
    const janus::SourceLocation location =
        specialization.declaration->destructor.has_value()
            ? specialization.declaration->destructor->location
            : specialization.declaration->location;
    janus::ast::FunctionDeclaration destructor_function{
        "destructor",
        {},
        {},
        janus::ast::TypeReference{"Unit", location, {}},
        {},
        location,
        false,
        false,
        {},
        false,
        std::nullopt,
        false,
        std::nullopt};
    const std::vector<janus::ast::Statement> empty_body;
    const auto &body = specialization.declaration->destructor.has_value()
                           ? specialization.declaration->destructor->body
                           : empty_body;
    return emit_function(destructor_function, {}, specialization.declaration,
                         &specialization.substitutions, class_name, &body);
  }

  std::vector<std::string> collect_captures(
      const janus::ast::LambdaExpression &lambda,
      const std::unordered_map<std::string, Local> &available) const {
    std::vector<std::string> captures;
    std::unordered_set<std::string> captured;
    std::unordered_set<std::string> bound;
    for (const auto &parameter : lambda.parameters)
      bound.insert(parameter.name);

    std::function<void(const janus::ast::Expression &,
                       const std::unordered_set<std::string> &)>
        visit;
    visit = [&](const janus::ast::Expression &expression,
                const std::unordered_set<std::string> &active_bound) {
      const auto capture = [&](std::string_view name) {
        const std::string key{name};
        if (!active_bound.contains(key) && available.contains(key) &&
            captured.insert(key).second)
          captures.push_back(key);
      };
      std::visit(
          [&](const auto &node) {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<Node,
                                         janus::ast::IdentifierExpression>) {
              capture(node.name);
            } else if constexpr (std::is_same_v<Node,
                                                janus::ast::LambdaExpression>) {
              auto nested_bound = active_bound;
              for (const auto &parameter : node.parameters)
                nested_bound.insert(parameter.name);
              visit(*node.body, nested_bound);
            } else if constexpr (std::is_same_v<Node,
                                                janus::ast::CallExpression>) {
              capture(node.callee);
              for (const auto &argument : node.arguments)
                visit(*argument, active_bound);
            } else if constexpr (std::is_same_v<Node,
                                                janus::ast::NewExpression>) {
              for (const auto &argument : node.arguments)
                visit(*argument, active_bound);
            } else if constexpr (std::is_same_v<
                                     Node,
                                     janus::ast::MemberAccessExpression>) {
              visit(*node.object, active_bound);
            } else if constexpr (std::is_same_v<
                                     Node, janus::ast::MethodCallExpression>) {
              visit(*node.object, active_bound);
              for (const auto &argument : node.arguments)
                visit(*argument, active_bound);
            } else if constexpr (std::is_same_v<Node,
                                                janus::ast::IfExpression>) {
              visit(*node.condition, active_bound);
              visit(*node.then_expression, active_bound);
              visit(*node.else_expression, active_bound);
            } else if constexpr (std::is_same_v<Node,
                                                janus::ast::MatchExpression>) {
              visit(*node.scrutinee, active_bound);
              for (const janus::ast::MatchExpression::Arm &arm : node.arms) {
                auto arm_bound = active_bound;
                arm_bound.insert(arm.bindings.begin(), arm.bindings.end());
                visit(*arm.expression, arm_bound);
              }
            } else if constexpr (std::is_same_v<Node,
                                                janus::ast::MoveExpression>) {
              visit(*node.operand, active_bound);
            } else if constexpr (std::is_same_v<Node,
                                                janus::ast::TryExpression>) {
              visit(*node.operand, active_bound);
            } else if constexpr (std::is_same_v<Node,
                                                janus::ast::UnaryExpression>) {
              visit(*node.operand, active_bound);
            } else if constexpr (std::is_same_v<Node,
                                                janus::ast::BinaryExpression>) {
              visit(*node.left, active_bound);
              visit(*node.right, active_bound);
            }
          },
          expression.value);
    };
    visit(*lambda.body, bound);
    return captures;
  }

  ::llvm::Value *
  emit_lambda(const janus::ast::LambdaExpression &lambda,
              const janus::Type &function_type,
              const Substitutions &substitutions,
              const std::unordered_map<std::string, Local> &locals,
              ::llvm::IRBuilder<> &builder) {
    const FunctionSignature &signature = function_signature(function_type);
    const std::vector<std::string> capture_names =
        collect_captures(lambda, locals);
    std::vector<::llvm::Type *> capture_types;
    capture_types.reserve(capture_names.size());
    for (const std::string &name : capture_names)
      capture_types.push_back(lower_type(*locals.at(name).type, context_));

    const std::size_t lambda_index = lambda_index_++;
    ::llvm::StructType *environment_type = ::llvm::StructType::create(
        context_, capture_types, "lambda.env." + std::to_string(lambda_index));
    ::llvm::Value *environment =
        ::llvm::ConstantPointerNull::get(builder.getPtrTy());
    if (!capture_names.empty()) {
      ::llvm::FunctionCallee malloc_function = module_->getOrInsertFunction(
          "janus_alloc", ::llvm::FunctionType::get(builder.getPtrTy(),
                                              {builder.getInt64Ty()}, false));
      environment = builder.CreateCall(
          malloc_function, {::llvm::ConstantExpr::getSizeOf(environment_type)},
          "lambda.environment");
    }
    for (std::size_t index = 0; index < capture_names.size(); ++index) {
      const Local &capture = locals.at(capture_names[index]);
      ::llvm::Value *value = builder.CreateLoad(
          lower_type(*capture.type, context_), capture.storage,
          capture_names[index] + ".capture");
      builder.CreateStore(
          value, builder.CreateStructGEP(environment_type, environment,
                                         static_cast<unsigned>(index)));
    }

    std::vector<::llvm::Type *> parameter_types{builder.getPtrTy()};
    for (const janus::Type *parameter : signature.parameters)
      parameter_types.push_back(lower_type(*parameter, context_));
    auto *llvm_function_type = ::llvm::FunctionType::get(
        lower_type(*signature.return_type, context_), parameter_types, false);
    ::llvm::Function *lambda_function = ::llvm::Function::Create(
        llvm_function_type, ::llvm::Function::InternalLinkage,
        "__janus_lambda_" + std::to_string(lambda_index), *module_);
    auto *entry =
        ::llvm::BasicBlock::Create(context_, "entry", lambda_function);
    ::llvm::IRBuilder<> lambda_builder{entry};
    std::unordered_map<std::string, Local> lambda_locals;

    auto argument = lambda_function->arg_begin();
    ::llvm::Argument &environment_argument = *argument++;
    environment_argument.setName("environment");
    for (std::size_t index = 0; index < capture_names.size(); ++index) {
      const Local &capture = locals.at(capture_names[index]);
      lambda_locals.emplace(capture_names[index],
                            Local{lambda_builder.CreateStructGEP(
                                      environment_type, &environment_argument,
                                      static_cast<unsigned>(index),
                                      capture_names[index] + ".capture.addr"),
                                  capture.type});
    }
    for (std::size_t index = 0; index < lambda.parameters.size(); ++index) {
      ::llvm::Argument &parameter = *argument++;
      parameter.setName(lambda.parameters[index].name);
      const janus::Type *parameter_type = signature.parameters[index];
      ::llvm::Value *storage =
          lambda_builder.CreateAlloca(lower_type(*parameter_type, context_),
                                      nullptr, lambda.parameters[index].name);
      lambda_builder.CreateStore(&parameter, storage);
      lambda_locals.emplace(lambda.parameters[index].name,
                            Local{storage, parameter_type});
    }

    const janus::Type *previous_return_type = active_return_type_;
    active_return_type_ = signature.return_type;
    ::llvm::Value *result =
        emit_expression(*lambda.body, *signature.return_type, substitutions,
                        lambda_locals, lambda_builder);
    active_return_type_ = previous_return_type;
    if (signature.return_type->kind() == janus::TypeKind::Unit)
      lambda_builder.CreateRetVoid();
    else
      lambda_builder.CreateRet(result);

    auto *closure_type =
        ::llvm::cast<::llvm::StructType>(lower_type(function_type, context_));
    ::llvm::Value *closure = ::llvm::UndefValue::get(closure_type);
    closure =
        builder.CreateInsertValue(closure, lambda_function, 0, "lambda.code");
    return builder.CreateInsertValue(closure, environment, 1, "lambda.value");
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
            return *resolve_storage(node.name, locals).type;
          } else if constexpr (std::is_same_v<Node,
                                              janus::ast::LambdaExpression>) {
            std::unordered_map<std::string, Local> lambda_locals = locals;
            std::vector<const janus::Type *> parameters;
            parameters.reserve(node.parameters.size());
            for (const auto &parameter : node.parameters) {
              const janus::Type &type = resolve(parameter.type, substitutions);
              parameters.push_back(&type);
              lambda_locals.insert_or_assign(parameter.name,
                                             Local{nullptr, &type});
            }
            const janus::Type &return_type =
                expression_type(*node.body, substitutions, lambda_locals);
            return ensure_function_type(parameters, return_type);
          } else if constexpr (std::is_same_v<Node,
                                              janus::ast::CallExpression>) {
            if (const Local *callable = find_storage(node.callee, locals);
                callable != nullptr &&
                callable->type->kind() == janus::TypeKind::Function)
              return *function_signature(*callable->type).return_type;
            if (node.callee == "panic" || node.callee == "print" ||
                node.callee == "println")
              return janus::Type::unit_type();
            if (node.callee == "cstr")
              return ensure_pointer(janus::Type::byte_type());
            if (node.callee == "alloc" || node.callee == "realloc" ||
                node.callee == "null") {
              const janus::Type &element =
                  resolve(node.type_arguments.front(), substitutions);
              return ensure_pointer(element);
            }
            if (node.callee == "sizeof" || node.callee == "alignof")
              return janus::Type::usize_type();
            if (node.callee == "free")
              return janus::Type::unit_type();
            if (is_explicit_cast(node))
              return cast_destination(node, substitutions);
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
            if (const auto *identifier =
                    std::get_if<janus::ast::IdentifierExpression>(
                        &node.object->value);
                identifier != nullptr && enums_.contains(identifier->name))
              return ensure_enum(identifier->name, {});
            if (const auto module = qualified_expression_name(*node.object);
                module.has_value() && global_modules_.contains(*module) &&
                !locals.contains(module->substr(0, module->find('.'))))
              return *resolve_qualified_global(*module, node.member).type;
            const janus::Type &object_type =
                expression_type(*node.object, substitutions, locals);
            return *find_field(object_type.name(), node.member).second;
          } else if constexpr (std::is_same_v<
                                   Node, janus::ast::MethodCallExpression>) {
            if (const auto *identifier =
                    std::get_if<janus::ast::IdentifierExpression>(
                        &node.object->value);
                identifier != nullptr && enums_.contains(identifier->name)) {
              std::vector<const janus::Type *> type_arguments;
              for (const janus::ast::TypeReference &argument :
                   node.type_arguments)
                type_arguments.push_back(&resolve(argument, substitutions));
              return ensure_enum(identifier->name, type_arguments);
            }
            const janus::Type &object_type =
                expression_type(*node.object, substitutions, locals);
            if (object_type.kind() == janus::TypeKind::Pointer)
              return node.method == "load" ? pointer_element(object_type)
                                           : janus::Type::unit_type();
            const ClassSpecialization &specialization =
                class_specializations_.at(std::string{object_type.name()});
            const auto &class_declaration = *specialization.declaration;
            for (const auto &method : class_declaration.methods) {
              if (method.name == node.method) {
                Substitutions method_substitutions =
                    specialization.substitutions;
                for (std::size_t index = 0; index < node.type_arguments.size();
                     ++index)
                  method_substitutions.emplace(
                      method.type_parameters[index],
                      &resolve(node.type_arguments[index], substitutions));
                return resolve(method.return_type, method_substitutions);
              }
            }
            return janus::Type::int_type();
          } else if constexpr (std::is_same_v<Node, janus::ast::IfExpression>) {
            return expression_type(*node.then_expression, substitutions,
                                   locals);
          } else if constexpr (std::is_same_v<Node,
                                              janus::ast::MatchExpression>) {
            const janus::Type &enum_type =
                expression_type(*node.scrutinee, substitutions, locals);
            const EnumSpecialization &specialization =
                enum_specializations_.at(std::string{enum_type.name()});
            const janus::ast::MatchExpression::Arm &arm = node.arms.front();
            const auto enum_case = std::find_if(
                specialization.declaration->cases.begin(),
                specialization.declaration->cases.end(),
                [&](const janus::ast::EnumDeclaration::Case &candidate) {
                  return candidate.name == arm.case_name;
                });
            std::unordered_map<std::string, Local> arm_locals = locals;
            for (std::size_t index = 0; index < arm.bindings.size(); ++index) {
              const janus::Type &payload_type =
                  resolve(enum_case->payload_types[index],
                          specialization.substitutions);
              arm_locals.insert_or_assign(arm.bindings[index],
                                          Local{nullptr, &payload_type});
            }
            return expression_type(*arm.expression, substitutions, arm_locals);
          } else if constexpr (std::is_same_v<Node,
                                              janus::ast::MoveExpression>) {
            return expression_type(*node.operand, substitutions, locals);
          } else if constexpr (std::is_same_v<Node,
                                              janus::ast::TryExpression>) {
            const janus::Type &enum_type =
                expression_type(*node.operand, substitutions, locals);
            const EnumSpecialization &specialization =
                enum_specializations_.at(std::string{enum_type.name()});
            const std::string_view success_case =
                specialization.declaration->name == "Option" ? "Some" : "Ok";
            const auto enum_case = std::find_if(
                specialization.declaration->cases.begin(),
                specialization.declaration->cases.end(),
                [&](const janus::ast::EnumDeclaration::Case &candidate) {
                  return candidate.name == success_case;
                });
            return resolve(enum_case->payload_types.front(),
                           specialization.substitutions);
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

  void emit_integer_panic(std::string_view message,
                          ::llvm::IRBuilder<> &builder) {
    ::llvm::Value *data =
        builder.CreateGlobalStringPtr(message, "integer.panic.message");
    ::llvm::FunctionCallee panic_function = module_->getOrInsertFunction(
        "janus_panic",
        ::llvm::FunctionType::get(builder.getVoidTy(),
                                  {builder.getPtrTy(), builder.getInt64Ty()},
                                  false));
    builder.CreateCall(panic_function,
                       {data, builder.getInt64(message.size())});
    builder.CreateUnreachable();
  }

  ::llvm::Value *
  emit_integer_division(::llvm::Value *left, ::llvm::Value *right,
                        const janus::Type &operand_type, bool is_remainder,
                        bool is_unsigned, ::llvm::IRBuilder<> &builder) {
    ::llvm::Value *zero = ::llvm::ConstantInt::get(right->getType(), 0, false);
    ::llvm::Value *divides_by_zero =
        builder.CreateICmpEQ(right, zero, "integer.division.zero");

    ::llvm::Function *function = builder.GetInsertBlock()->getParent();
    auto *zero_trap_block =
        ::llvm::BasicBlock::Create(context_, "integer.division.zero_trap",
                                   function);
    auto *valid_block =
        ::llvm::BasicBlock::Create(context_, "integer.division.valid");

    if (is_unsigned) {
      builder.CreateCondBr(divides_by_zero, zero_trap_block, valid_block);
    } else {
      auto *overflow_check_block = ::llvm::BasicBlock::Create(
          context_, "integer.division.overflow_check", function);
      builder.CreateCondBr(divides_by_zero, zero_trap_block,
                           overflow_check_block);

      builder.SetInsertPoint(overflow_check_block);
      const unsigned width = operand_type.bit_width();
      ::llvm::Value *minimum = ::llvm::ConstantInt::get(
          right->getType(), ::llvm::APInt::getSignedMinValue(width));
      ::llvm::Value *minus_one =
          ::llvm::ConstantInt::get(right->getType(), -1, true);
      ::llvm::Value *overflows = builder.CreateAnd(
          builder.CreateICmpEQ(left, minimum, "integer.division.min"),
          builder.CreateICmpEQ(right, minus_one, "integer.division.minus_one"),
          "integer.division.overflow");
      auto *overflow_trap_block = ::llvm::BasicBlock::Create(
          context_, "integer.division.overflow_trap", function);
      builder.CreateCondBr(overflows, overflow_trap_block, valid_block);

      builder.SetInsertPoint(overflow_trap_block);
      emit_integer_panic("integer division overflow\n", builder);
    }

    builder.SetInsertPoint(zero_trap_block);
    emit_integer_panic("integer division by zero\n", builder);

    function->insert(function->end(), valid_block);
    builder.SetInsertPoint(valid_block);
    if (is_remainder)
      return is_unsigned ? builder.CreateURem(left, right, "rem")
                         : builder.CreateSRem(left, right, "rem");
    return is_unsigned ? builder.CreateUDiv(left, right, "div")
                       : builder.CreateSDiv(left, right, "div");
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
          ::llvm::Type *llvm_type = lower_type(expected_type, context_);
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
            const Local &local = resolve_storage(node.name, locals);
            return builder.CreateLoad(lower_type(*local.type, context_),
                                      local.storage, node.name + ".value");
          } else if constexpr (std::is_same_v<Node,
                                              janus::ast::LambdaExpression>) {
            return emit_lambda(node, expected_type, substitutions, locals,
                               builder);
          } else if constexpr (std::is_same_v<Node,
                                              janus::ast::CallExpression>) {
            if (const Local *local = find_storage(node.callee, locals);
                local != nullptr &&
                local->type->kind() == janus::TypeKind::Function) {
              const FunctionSignature &signature =
                  function_signature(*local->type);
              ::llvm::Value *closure = builder.CreateLoad(
                  lower_type(*local->type, context_), local->storage,
                  node.callee + ".closure");
              ::llvm::Value *code =
                  builder.CreateExtractValue(closure, 0, node.callee + ".code");
              ::llvm::Value *environment = builder.CreateExtractValue(
                  closure, 1, node.callee + ".environment");
              std::vector<::llvm::Value *> arguments{environment};
              for (std::size_t index = 0; index < node.arguments.size();
                   ++index)
                arguments.push_back(emit_expression(
                    *node.arguments[index], *signature.parameters[index],
                    substitutions, locals, builder));
              std::vector<::llvm::Type *> parameter_types{builder.getPtrTy()};
              for (const janus::Type *parameter : signature.parameters)
                parameter_types.push_back(lower_type(*parameter, context_));
              auto *callee_type = ::llvm::FunctionType::get(
                  lower_type(*signature.return_type, context_), parameter_types,
                  false);
              if (signature.return_type->kind() == janus::TypeKind::Unit)
                return builder.CreateCall(callee_type, code, arguments);
              return builder.CreateCall(callee_type, code, arguments,
                                        node.callee + ".call");
            }
            if (node.callee == "print" || node.callee == "println") {
              const janus::Type &argument_type = expression_type(
                  *node.arguments.front(), substitutions, locals);
              ::llvm::Value *argument =
                  emit_expression(*node.arguments.front(), argument_type,
                                  substitutions, locals, builder);
              ::llvm::Value *result = nullptr;
              switch (argument_type.kind()) {
              case janus::TypeKind::String: {
                ::llvm::Value *data =
                    builder.CreateExtractValue(argument, 0, "print.data");
                ::llvm::Value *length =
                    builder.CreateExtractValue(argument, 1, "print.length");
                ::llvm::FunctionCallee function = module_->getOrInsertFunction(
                    "janus_write_stdout",
                    ::llvm::FunctionType::get(
                        builder.getVoidTy(),
                        {builder.getPtrTy(), builder.getInt64Ty()}, false));
                result = builder.CreateCall(function, {data, length});
                break;
              }
              case janus::TypeKind::Int: {
                ::llvm::FunctionCallee function = module_->getOrInsertFunction(
                    "janus_print_int",
                    ::llvm::FunctionType::get(
                        builder.getVoidTy(), {builder.getInt32Ty()}, false));
                result = builder.CreateCall(function, {argument});
                break;
              }
              case janus::TypeKind::UInt: {
                ::llvm::FunctionCallee function = module_->getOrInsertFunction(
                    "janus_print_uint",
                    ::llvm::FunctionType::get(
                        builder.getVoidTy(), {builder.getInt32Ty()}, false));
                result = builder.CreateCall(function, {argument});
                break;
              }
              case janus::TypeKind::Long: {
                ::llvm::FunctionCallee function = module_->getOrInsertFunction(
                    "janus_print_long",
                    ::llvm::FunctionType::get(
                        builder.getVoidTy(), {builder.getInt64Ty()}, false));
                result = builder.CreateCall(function, {argument});
                break;
              }
              case janus::TypeKind::ULong: {
                ::llvm::FunctionCallee function = module_->getOrInsertFunction(
                    "janus_print_ulong",
                    ::llvm::FunctionType::get(
                        builder.getVoidTy(), {builder.getInt64Ty()}, false));
                result = builder.CreateCall(function, {argument});
                break;
              }
              case janus::TypeKind::Byte: {
                ::llvm::FunctionCallee function = module_->getOrInsertFunction(
                    "janus_print_byte",
                    ::llvm::FunctionType::get(
                        builder.getVoidTy(), {builder.getInt32Ty()}, false));
                ::llvm::Value *signed_argument = builder.CreateSExt(
                    argument, builder.getInt32Ty(), "print.byte.signed");
                result = builder.CreateCall(function, {signed_argument});
                break;
              }
              case janus::TypeKind::UByte: {
                ::llvm::FunctionCallee function = module_->getOrInsertFunction(
                    "janus_print_ubyte",
                    ::llvm::FunctionType::get(
                        builder.getVoidTy(), {builder.getInt32Ty()}, false));
                ::llvm::Value *unsigned_argument = builder.CreateZExt(
                    argument, builder.getInt32Ty(), "print.ubyte.unsigned");
                result = builder.CreateCall(function, {unsigned_argument});
                break;
              }
              case janus::TypeKind::Short: {
                ::llvm::FunctionCallee function = module_->getOrInsertFunction(
                    "janus_print_short",
                    ::llvm::FunctionType::get(
                        builder.getVoidTy(), {builder.getInt32Ty()}, false));
                ::llvm::Value *signed_argument = builder.CreateSExt(
                    argument, builder.getInt32Ty(), "print.short.signed");
                result = builder.CreateCall(function, {signed_argument});
                break;
              }
              case janus::TypeKind::UShort: {
                ::llvm::FunctionCallee function = module_->getOrInsertFunction(
                    "janus_print_ushort",
                    ::llvm::FunctionType::get(
                        builder.getVoidTy(), {builder.getInt32Ty()}, false));
                ::llvm::Value *unsigned_argument = builder.CreateZExt(
                    argument, builder.getInt32Ty(), "print.ushort.unsigned");
                result = builder.CreateCall(function, {unsigned_argument});
                break;
              }
              case janus::TypeKind::USize: {
                ::llvm::FunctionCallee function = module_->getOrInsertFunction(
                    "janus_print_usize",
                    ::llvm::FunctionType::get(
                        builder.getVoidTy(), {builder.getInt64Ty()}, false));
                result = builder.CreateCall(function, {argument});
                break;
              }
              case janus::TypeKind::ISize: {
                ::llvm::FunctionCallee function = module_->getOrInsertFunction(
                    "janus_print_isize",
                    ::llvm::FunctionType::get(
                        builder.getVoidTy(), {builder.getInt64Ty()}, false));
                result = builder.CreateCall(function, {argument});
                break;
              }
              case janus::TypeKind::Double: {
                ::llvm::FunctionCallee function = module_->getOrInsertFunction(
                    "janus_print_double",
                    ::llvm::FunctionType::get(
                        builder.getVoidTy(), {builder.getDoubleTy()}, false));
                result = builder.CreateCall(function, {argument});
                break;
              }
              case janus::TypeKind::Float: {
                ::llvm::FunctionCallee function = module_->getOrInsertFunction(
                    "janus_print_float",
                    ::llvm::FunctionType::get(
                        builder.getVoidTy(), {builder.getFloatTy()}, false));
                result = builder.CreateCall(function, {argument});
                break;
              }
              case janus::TypeKind::Bool: {
                ::llvm::FunctionCallee function = module_->getOrInsertFunction(
                    "janus_print_bool",
                    ::llvm::FunctionType::get(
                        builder.getVoidTy(), {builder.getInt1Ty()}, false));
                result = builder.CreateCall(function, {argument});
                break;
              }
              case janus::TypeKind::Char: {
                ::llvm::FunctionCallee function = module_->getOrInsertFunction(
                    "janus_print_char",
                    ::llvm::FunctionType::get(
                        builder.getVoidTy(), {builder.getInt32Ty()}, false));
                result = builder.CreateCall(function, {argument});
                break;
              }
              default:
                break;
              }
              if (node.callee == "println") {
                ::llvm::Value *newline = builder.CreateGlobalString("\n");
                ::llvm::FunctionCallee function = module_->getOrInsertFunction(
                    "janus_write_stdout",
                    ::llvm::FunctionType::get(
                        builder.getVoidTy(),
                        {builder.getPtrTy(), builder.getInt64Ty()}, false));
                result = builder.CreateCall(
                    function, {newline, builder.getInt64(1)});
              }
              return result;
            }
            if (node.callee == "cstr") {
              ::llvm::Value *text = emit_expression(
                  *node.arguments.front(), janus::Type::string_type(),
                  substitutions, locals, builder);
              return builder.CreateExtractValue(text, 0, "cstr.data");
            }
            if (node.callee == "panic") {
              ::llvm::Value *message = emit_expression(
                  *node.arguments.front(), janus::Type::string_type(),
                  substitutions, locals, builder);
              ::llvm::Value *data =
                  builder.CreateExtractValue(message, 0, "panic.data");
              ::llvm::Value *length =
                  builder.CreateExtractValue(message, 1, "panic.length");
              ::llvm::FunctionCallee panic_function =
                  module_->getOrInsertFunction(
                      "janus_panic",
                      ::llvm::FunctionType::get(
                          builder.getVoidTy(),
                          {builder.getPtrTy(), builder.getInt64Ty()}, false));
              return builder.CreateCall(panic_function, {data, length});
            }
            if (node.callee == "alloc" || node.callee == "realloc" ||
                node.callee == "null" || node.callee == "sizeof" ||
                node.callee == "alignof") {
              const janus::Type &element_type =
                  resolve(node.type_arguments.front(), substitutions);
              ::llvm::Type *llvm_element_type =
                  lower_type(element_type, context_);
              if (node.callee == "null")
                return ::llvm::ConstantPointerNull::get(builder.getPtrTy());
              if (node.callee == "sizeof")
                return ::llvm::ConstantExpr::getSizeOf(llvm_element_type);
              if (node.callee == "alignof")
                return ::llvm::ConstantExpr::getAlignOf(llvm_element_type);

              const janus::Type &pointer_type = ensure_pointer(element_type);
              const std::size_t count_index = node.callee == "alloc" ? 0 : 1;
              ::llvm::Value *count = emit_expression(
                  *node.arguments[count_index], janus::Type::usize_type(),
                  substitutions, locals, builder);
              ::llvm::Value *bytes = builder.CreateMul(
                  count, ::llvm::ConstantExpr::getSizeOf(llvm_element_type),
                  "allocation.bytes");
              if (node.callee == "alloc") {
                ::llvm::FunctionCallee malloc_function =
                    module_->getOrInsertFunction(
                        "janus_alloc",
                        ::llvm::FunctionType::get(
                            builder.getPtrTy(), {builder.getInt64Ty()}, false));
                return builder.CreateCall(malloc_function, {bytes}, "alloc");
              }
              ::llvm::Value *pointer =
                  emit_expression(*node.arguments[0], pointer_type,
                                  substitutions, locals, builder);
              ::llvm::FunctionCallee realloc_function =
                  module_->getOrInsertFunction(
                      "janus_realloc",
                      ::llvm::FunctionType::get(
                          builder.getPtrTy(),
                          {builder.getPtrTy(), builder.getInt64Ty()}, false));
              return builder.CreateCall(realloc_function, {pointer, bytes},
                                        "realloc");
            }
            if (node.callee == "free") {
              const janus::Type &pointer_type = expression_type(
                  *node.arguments.front(), substitutions, locals);
              ::llvm::Value *pointer =
                  emit_expression(*node.arguments.front(), pointer_type,
                                  substitutions, locals, builder);
              ::llvm::FunctionCallee free_function =
                  module_->getOrInsertFunction(
                      "janus_free",
                      ::llvm::FunctionType::get(builder.getVoidTy(),
                                                {builder.getPtrTy()}, false));
              return builder.CreateCall(free_function, {pointer});
            }
            if (is_explicit_cast(node)) {
              const janus::Type &conversion_type =
                  cast_destination(node, substitutions);
              const janus::Type &source_type = expression_type(
                  *node.arguments.front(), substitutions, locals);
              ::llvm::Value *source =
                  emit_expression(*node.arguments.front(), source_type,
                                  substitutions, locals, builder);
              const bool source_is_reference =
                  source_type.kind() == janus::TypeKind::Pointer ||
                  source_type.kind() == janus::TypeKind::Class;
              const bool destination_is_reference =
                  conversion_type.kind() == janus::TypeKind::Pointer ||
                  conversion_type.kind() == janus::TypeKind::Class;
              ::llvm::Type *destination_type =
                  lower_type(conversion_type, context_);

              if (source_type.kind() == janus::TypeKind::Enum)
                source = builder.CreateExtractValue(source, 0, "enum.tag");
              if (conversion_type.kind() == janus::TypeKind::Enum) {
                ::llvm::Value *tag = source;
                if (source_type.is_floating_point())
                  tag = builder.CreateFPToSI(source, builder.getInt32Ty(),
                                             "floating.to.enum");
                else if (source_is_reference)
                  tag = builder.CreatePtrToInt(source, builder.getInt32Ty(),
                                               "pointer.to.enum");
                else if (source->getType() != builder.getInt32Ty())
                  tag = builder.CreateIntCast(source, builder.getInt32Ty(),
                                              source_type.is_signed(),
                                              "integer.to.enum");
                auto *enum_type =
                    ::llvm::cast<::llvm::StructType>(destination_type);
                return builder.CreateInsertValue(
                    ::llvm::UndefValue::get(enum_type), tag, 0, "enum.value");
              }
              if (source_type.kind() == conversion_type.kind() ||
                  (source_is_reference && destination_is_reference))
                return source;
              if (source_is_reference &&
                  conversion_type.kind() == janus::TypeKind::Bool)
                return builder.CreateICmpNE(
                    source,
                    ::llvm::ConstantPointerNull::get(builder.getPtrTy()),
                    "pointer.to.bool");
              if (source_is_reference)
                return builder.CreatePtrToInt(source, destination_type,
                                              "pointer.to.integer");
              if (destination_is_reference)
                return builder.CreateIntToPtr(source, destination_type,
                                              "usize.to.pointer");
              if (conversion_type.kind() == janus::TypeKind::Bool) {
                if (source_type.is_floating_point())
                  return builder.CreateFCmpUNE(
                      source, ::llvm::ConstantFP::get(source->getType(), 0.0),
                      "floating.to.bool");
                return builder.CreateICmpNE(
                    source, ::llvm::ConstantInt::get(source->getType(), 0),
                    "integer.to.bool");
              }
              if (conversion_type.is_floating_point()) {
                if (source_type.is_floating_point())
                  return builder.CreateFPCast(source, destination_type,
                                              node.callee + ".conversion");
                if (source_type.is_signed())
                  return builder.CreateSIToFP(source, destination_type,
                                              "signed.to.floating");
                return builder.CreateUIToFP(source, destination_type,
                                            "unsigned.to.floating");
              }
              if (source_type.is_floating_point()) {
                if (conversion_type.is_signed())
                  return builder.CreateFPToSI(source, destination_type,
                                              "floating.to.signed");
                return builder.CreateFPToUI(source, destination_type,
                                            "floating.to.unsigned");
              }
              return builder.CreateIntCast(source, destination_type,
                                           source_type.is_signed(),
                                           node.callee + ".conversion");
            }
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
              if (index >= callee.parameters.size()) {
                const janus::Type &argument_type = expression_type(
                    *node.arguments[index], substitutions, locals);
                ::llvm::Value *argument = emit_expression(
                    *node.arguments[index], argument_type, substitutions,
                    locals, builder);
                if (argument_type.bit_width() < 32 &&
                    argument_type.is_integer())
                  argument = builder.CreateIntCast(
                      argument, builder.getInt32Ty(), argument_type.is_signed(),
                      "vararg.integer");
                else if (argument_type.kind() == janus::TypeKind::Float)
                  argument = builder.CreateFPExt(argument, builder.getDoubleTy(),
                                                 "vararg.float");
                else if (argument_type.kind() == janus::TypeKind::Bool)
                  argument = builder.CreateZExt(argument, builder.getInt32Ty(),
                                                "vararg.bool");
                arguments.push_back(argument);
                continue;
              }
              const janus::Type &parameter_type =
                  resolve(callee.parameters[index].type, callee_substitutions);
              arguments.push_back(emit_expression(*node.arguments[index],
                                                  parameter_type, substitutions,
                                                  locals, builder));
            }
            return target->getReturnType()->isVoidTy()
                       ? builder.CreateCall(target, arguments)
                       : builder.CreateCall(target, arguments,
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
            if (class_declaration.is_value_type) {
              ::llvm::Value *value = ::llvm::UndefValue::get(class_type);
              for (std::size_t index = 0;
                   index < class_declaration.constructor_fields.size();
                   ++index) {
                const auto &field_declaration =
                    class_declaration.constructor_fields[index];
                const janus::Type &field_type =
                    resolve(field_declaration.declared_type,
                            specialization.substitutions);
                value = builder.CreateInsertValue(
                    value,
                    emit_expression(*node.arguments[index], field_type,
                                    substitutions, locals, builder),
                    static_cast<unsigned>(index),
                    field_declaration.name + ".value");
              }
              return value;
            }
            ::llvm::FunctionCallee malloc_function =
                module_->getOrInsertFunction(
                    "janus_alloc",
                    ::llvm::FunctionType::get(builder.getPtrTy(),
                                              {builder.getInt64Ty()}, false));
            ::llvm::Value *object = builder.CreateCall(
                malloc_function, {::llvm::ConstantExpr::getSizeOf(class_type)},
                node.class_name + ".new");
            auto initializer_locals = locals;
            const std::size_t parameter_count =
                class_declaration.constructor_parameters.size();
            for (std::size_t index = 0; index < parameter_count; ++index) {
              const auto &parameter =
                  class_declaration.constructor_parameters[index];
              const janus::Type &parameter_type =
                  resolve(parameter.type, specialization.substitutions);
              ::llvm::Value *storage = builder.CreateAlloca(
                  lower_type(parameter_type, context_), nullptr,
                  parameter.name + ".constructor");
              builder.CreateStore(emit_expression(*node.arguments[index],
                                                  parameter_type, substitutions,
                                                  locals, builder),
                                  storage);
              initializer_locals.insert_or_assign(
                  parameter.name, Local{storage, &parameter_type});
            }
            unsigned field_index = 0;
            for (std::size_t index = 0;
                 index < class_declaration.constructor_fields.size(); ++index) {
              const auto &field_declaration =
                  class_declaration.constructor_fields[index];
              const janus::Type &field_type =
                  resolve(field_declaration.declared_type,
                          specialization.substitutions);
              ::llvm::Value *field =
                  builder.CreateStructGEP(class_type, object, field_index++);
              builder.CreateStore(
                  emit_expression(*node.arguments[parameter_count + index],
                                  field_type, substitutions, locals, builder),
                  field);
              initializer_locals.insert_or_assign(field_declaration.name,
                                                  Local{field, &field_type});
            }
            for (const auto &field_declaration : class_declaration.fields) {
              ::llvm::Value *field =
                  builder.CreateStructGEP(class_type, object, field_index++);
              const janus::Type &field_type =
                  resolve(field_declaration.declared_type,
                          specialization.substitutions);
              if (field_declaration.initializer.has_value()) {
                builder.CreateStore(
                    emit_expression(*field_declaration.initializer, field_type,
                                    specialization.substitutions,
                                    initializer_locals, builder),
                    field);
              }
              initializer_locals.insert_or_assign(field_declaration.name,
                                                  Local{field, &field_type});
            }
            return object;
          } else if constexpr (std::is_same_v<
                                   Node, janus::ast::MemberAccessExpression>) {
            const auto *identifier =
                std::get_if<janus::ast::IdentifierExpression>(
                    &node.object->value);
            if (identifier != nullptr && enums_.contains(identifier->name)) {
              const janus::Type &enum_type = ensure_enum(identifier->name, {});
              auto *llvm_enum_type =
                  llvm_enum_types_.at(std::string{enum_type.name()});
              ::llvm::Value *value = ::llvm::UndefValue::get(llvm_enum_type);
              return builder.CreateInsertValue(
                  value,
                  builder.getInt32(
                      enum_case_value(enum_type.name(), node.member)),
                  0, "enum.value");
            }
            if (const auto module = qualified_expression_name(*node.object);
                module.has_value() && global_modules_.contains(*module) &&
                !locals.contains(module->substr(0, module->find('.')))) {
              const Local &global =
                  resolve_qualified_global(*module, node.member);
              return builder.CreateLoad(lower_type(*global.type, context_),
                                        global.storage,
                                        node.member + ".global");
            }
            const janus::Type &object_type =
                expression_type(*node.object, substitutions, locals);
            ::llvm::Value *object_pointer = nullptr;
            if (identifier != nullptr) {
              const Local &object = resolve_storage(identifier->name, locals);
              object_pointer =
                  object_type.kind() == janus::TypeKind::Struct
                      ? object.storage
                      : builder.CreateLoad(builder.getPtrTy(), object.storage,
                                           identifier->name + ".object");
            } else {
              ::llvm::Value *object_value =
                  emit_expression(*node.object, object_type, substitutions,
                                  locals, builder);
              if (object_type.kind() == janus::TypeKind::Struct) {
                object_pointer = builder.CreateAlloca(
                    lower_type(object_type, context_), nullptr,
                    node.member + ".temporary");
                builder.CreateStore(object_value, object_pointer);
              } else {
                object_pointer = object_value;
              }
            }
            const auto [field_index, field_type] =
                find_field(object_type.name(), node.member);
            ::llvm::Value *field_pointer = builder.CreateStructGEP(
                llvm_class_types_.at(std::string{object_type.name()}),
                object_pointer, field_index);
            return builder.CreateLoad(lower_type(*field_type, context_),
                                      field_pointer, node.member + ".value");
          } else if constexpr (std::is_same_v<
                                   Node, janus::ast::MethodCallExpression>) {
            const auto *identifier =
                std::get_if<janus::ast::IdentifierExpression>(
                    &node.object->value);
            if (identifier != nullptr && enums_.contains(identifier->name)) {
              std::vector<const janus::Type *> type_arguments;
              for (const janus::ast::TypeReference &argument :
                   node.type_arguments)
                type_arguments.push_back(&resolve(argument, substitutions));
              const janus::Type &enum_type =
                  ensure_enum(identifier->name, type_arguments);
              const EnumSpecialization &specialization =
                  enum_specializations_.at(std::string{enum_type.name()});
              const auto enum_case = std::find_if(
                  specialization.declaration->cases.begin(),
                  specialization.declaration->cases.end(),
                  [&](const janus::ast::EnumDeclaration::Case &item) {
                    return item.name == node.method;
                  });
              auto *llvm_enum_type =
                  llvm_enum_types_.at(std::string{enum_type.name()});
              ::llvm::Value *value = ::llvm::UndefValue::get(llvm_enum_type);
              value =
                  builder.CreateInsertValue(value,
                                            builder.getInt32(enum_case_value(
                                                enum_type.name(), node.method)),
                                            0, "enum.tag");
              unsigned field =
                  enum_case_payload_start(enum_type.name(), node.method);
              for (std::size_t index = 0; index < node.arguments.size();
                   ++index) {
                const janus::Type &payload_type =
                    resolve(enum_case->payload_types[index],
                            specialization.substitutions);
                value = builder.CreateInsertValue(
                    value,
                    emit_expression(*node.arguments[index], payload_type,
                                    substitutions, locals, builder),
                    field++, "enum.payload");
              }
              return value;
            }
            const janus::Type &object_type =
                expression_type(*node.object, substitutions, locals);
            ::llvm::Value *object_value = nullptr;
            if (identifier != nullptr) {
              const Local &object = resolve_storage(identifier->name, locals);
              if (object_type.kind() == janus::TypeKind::Struct) {
                object_value = object.storage;
              } else {
                object_value = builder.CreateLoad(
                    builder.getPtrTy(), object.storage,
                    identifier->name +
                        (object_type.kind() == janus::TypeKind::Pointer
                             ? ".pointer"
                             : ".object"));
              }
            } else {
              object_value = emit_expression(*node.object, object_type,
                                             substitutions, locals, builder);
              if (object_type.kind() == janus::TypeKind::Struct) {
                ::llvm::Value *storage = builder.CreateAlloca(
                    lower_type(object_type, context_), nullptr,
                    node.method + ".temporary");
                builder.CreateStore(object_value, storage);
                object_value = storage;
              }
            }
            if (object_type.kind() == janus::TypeKind::Pointer) {
              const janus::Type &element_type = pointer_element(object_type);
              ::llvm::Value *index =
                  emit_expression(*node.arguments[0], janus::Type::usize_type(),
                                  substitutions, locals, builder);
              ::llvm::Value *element = builder.CreateInBoundsGEP(
                  lower_type(element_type, context_), object_value, index,
                  "pointer.element");
              if (node.method == "load")
                return builder.CreateLoad(lower_type(element_type, context_),
                                          element, "pointer.value");
              ::llvm::Value *value =
                  emit_expression(*node.arguments[1], element_type,
                                  substitutions, locals, builder);
              return builder.CreateStore(value, element);
            }
            const ClassSpecialization &specialization =
                class_specializations_.at(std::string{object_type.name()});
            const auto &class_declaration = *specialization.declaration;
            const janus::ast::FunctionDeclaration *method = nullptr;
            for (const janus::ast::FunctionDeclaration &candidate :
                 class_declaration.methods) {
              if (candidate.name == node.method)
                method = &candidate;
            }
            std::vector<const janus::Type *> method_type_arguments;
            method_type_arguments.reserve(node.type_arguments.size());
            for (const janus::ast::TypeReference &argument :
                 node.type_arguments)
              method_type_arguments.push_back(
                  &resolve(argument, substitutions));
            ::llvm::Function *target = emit_function(
                *method, method_type_arguments, &class_declaration,
                &specialization.substitutions, object_type.name());
            Substitutions method_substitutions = specialization.substitutions;
            for (std::size_t index = 0; index < method_type_arguments.size();
                 ++index)
              method_substitutions.emplace(method->type_parameters[index],
                                           method_type_arguments[index]);
            std::vector<::llvm::Value *> arguments;
            arguments.push_back(object_value);
            for (std::size_t index = 0; index < node.arguments.size();
                 ++index) {
              const janus::Type &parameter_type =
                  resolve(method->parameters[index].type, method_substitutions);
              arguments.push_back(emit_expression(*node.arguments[index],
                                                  parameter_type, substitutions,
                                                  locals, builder));
            }
            return target->getReturnType()->isVoidTy()
                       ? builder.CreateCall(target, arguments)
                       : builder.CreateCall(target, arguments,
                                            node.method + ".result");
          } else if constexpr (std::is_same_v<Node, janus::ast::IfExpression>) {
            ::llvm::Value *condition =
                emit_expression(*node.condition, janus::Type::bool_type(),
                                substitutions, locals, builder);
            ::llvm::Function *function = builder.GetInsertBlock()->getParent();
            auto *then_block =
                ::llvm::BasicBlock::Create(context_, "if.value.then", function);
            auto *else_block =
                ::llvm::BasicBlock::Create(context_, "if.value.else", function);
            auto *merge_block =
                ::llvm::BasicBlock::Create(context_, "if.value.end", function);
            builder.CreateCondBr(condition, then_block, else_block);

            builder.SetInsertPoint(then_block);
            ::llvm::Value *then_value =
                emit_expression(*node.then_expression, expected_type,
                                substitutions, locals, builder);
            ::llvm::BasicBlock *then_end = builder.GetInsertBlock();
            builder.CreateBr(merge_block);

            builder.SetInsertPoint(else_block);
            ::llvm::Value *else_value =
                emit_expression(*node.else_expression, expected_type,
                                substitutions, locals, builder);
            ::llvm::BasicBlock *else_end = builder.GetInsertBlock();
            builder.CreateBr(merge_block);

            builder.SetInsertPoint(merge_block);
            auto *result = builder.CreatePHI(llvm_type, 2, "if.value");
            result->addIncoming(then_value, then_end);
            result->addIncoming(else_value, else_end);
            return result;
          } else if constexpr (std::is_same_v<Node,
                                              janus::ast::MatchExpression>) {
            const janus::Type &enum_type =
                expression_type(*node.scrutinee, substitutions, locals);
            const EnumSpecialization &specialization =
                enum_specializations_.at(std::string{enum_type.name()});
            ::llvm::Value *scrutinee = emit_expression(
                *node.scrutinee, enum_type, substitutions, locals, builder);
            ::llvm::Value *tag =
                builder.CreateExtractValue(scrutinee, 0, "match.tag");
            ::llvm::Function *function = builder.GetInsertBlock()->getParent();
            auto *default_block =
                ::llvm::BasicBlock::Create(context_, "match.unhandled");
            auto *merge_block =
                ::llvm::BasicBlock::Create(context_, "match.end");
            auto *switch_value = builder.CreateSwitch(
                tag, default_block, static_cast<unsigned>(node.arms.size()));

            std::vector<std::pair<::llvm::Value *, ::llvm::BasicBlock *>>
                incoming;
            incoming.reserve(node.arms.size());
            for (const janus::ast::MatchExpression::Arm &arm : node.arms) {
              auto *arm_block = ::llvm::BasicBlock::Create(
                  context_, "match." + arm.case_name, function);
              switch_value->addCase(builder.getInt32(enum_case_value(
                                        enum_type.name(), arm.case_name)),
                                    arm_block);
              builder.SetInsertPoint(arm_block);

              const auto enum_case = std::find_if(
                  specialization.declaration->cases.begin(),
                  specialization.declaration->cases.end(),
                  [&](const janus::ast::EnumDeclaration::Case &candidate) {
                    return candidate.name == arm.case_name;
                  });
              std::unordered_map<std::string, Local> arm_locals = locals;
              unsigned field =
                  enum_case_payload_start(enum_type.name(), arm.case_name);
              for (std::size_t index = 0; index < arm.bindings.size();
                   ++index) {
                const janus::Type &payload_type =
                    resolve(enum_case->payload_types[index],
                            specialization.substitutions);
                ::llvm::Value *payload = builder.CreateExtractValue(
                    scrutinee, field++, arm.bindings[index] + ".payload");
                ::llvm::Value *storage =
                    builder.CreateAlloca(lower_type(payload_type, context_),
                                         nullptr, arm.bindings[index]);
                builder.CreateStore(payload, storage);
                arm_locals.insert_or_assign(arm.bindings[index],
                                            Local{storage, &payload_type});
              }
              ::llvm::Value *arm_value =
                  emit_expression(*arm.expression, expected_type, substitutions,
                                  arm_locals, builder);
              ::llvm::BasicBlock *arm_end = builder.GetInsertBlock();
              builder.CreateBr(merge_block);
              incoming.emplace_back(arm_value, arm_end);
            }

            function->insert(function->end(), default_block);
            builder.SetInsertPoint(default_block);
            builder.CreateUnreachable();
            function->insert(function->end(), merge_block);
            builder.SetInsertPoint(merge_block);
            auto *result = builder.CreatePHI(
                llvm_type, static_cast<unsigned>(incoming.size()),
                "match.value");
            for (const auto &[value, block] : incoming)
              result->addIncoming(value, block);
            return result;
          } else if constexpr (std::is_same_v<Node,
                                              janus::ast::MoveExpression>) {
            return emit_expression(*node.operand, expected_type, substitutions,
                                   locals, builder);
          } else if constexpr (std::is_same_v<Node,
                                              janus::ast::TryExpression>) {
            const janus::Type &operand_type =
                expression_type(*node.operand, substitutions, locals);
            const EnumSpecialization &operand_specialization =
                enum_specializations_.at(std::string{operand_type.name()});
            const bool is_option =
                operand_specialization.declaration->name == "Option";
            const std::string_view success_case = is_option ? "Some" : "Ok";
            const std::string_view failure_case = is_option ? "None" : "Error";
            ::llvm::Value *operand = emit_expression(
                *node.operand, operand_type, substitutions, locals, builder);
            ::llvm::Value *tag =
                builder.CreateExtractValue(operand, 0, "try.tag");
            ::llvm::Function *function = builder.GetInsertBlock()->getParent();
            auto *success_block =
                ::llvm::BasicBlock::Create(context_, "try.success", function);
            auto *failure_block =
                ::llvm::BasicBlock::Create(context_, "try.failure", function);
            builder.CreateCondBr(
                builder.CreateICmpEQ(
                    tag, builder.getInt32(enum_case_value(operand_type.name(),
                                                          success_case))),
                success_block, failure_block);

            builder.SetInsertPoint(failure_block);
            const janus::Type &return_type = *active_return_type_;
            auto *llvm_return_type = ::llvm::cast<::llvm::StructType>(
                lower_type(return_type, context_));
            ::llvm::Value *failure = ::llvm::UndefValue::get(llvm_return_type);
            failure = builder.CreateInsertValue(
                failure,
                builder.getInt32(
                    enum_case_value(return_type.name(), failure_case)),
                0, "try.failure.tag");
            if (!is_option) {
              ::llvm::Value *error = builder.CreateExtractValue(
                  operand,
                  enum_case_payload_start(operand_type.name(), failure_case),
                  "try.error");
              failure = builder.CreateInsertValue(
                  failure, error,
                  enum_case_payload_start(return_type.name(), failure_case),
                  "try.failure.value");
            }
            emit_active_cleanups(builder);
            builder.CreateRet(failure);

            builder.SetInsertPoint(success_block);
            return builder.CreateExtractValue(
                operand,
                enum_case_payload_start(operand_type.name(), success_case),
                "try.value");
          } else if constexpr (std::is_same_v<Node,
                                              janus::ast::UnaryExpression>) {
            const janus::Type *operand_type =
                &expression_type(*node.operand, substitutions, locals);
            if (expected_type.is_integer() &&
                std::holds_alternative<janus::ast::IntegerLiteralExpression>(
                    node.operand->value)) {
              operand_type = &expected_type;
            }
            ::llvm::Value *operand = emit_expression(
                *node.operand, *operand_type, substitutions, locals, builder);
            if (node.operation == janus::ast::UnaryOperator::LogicalNot)
              return builder.CreateNot(operand, "not");
            if (operand_type->is_floating_point())
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
            if (operand_type.kind() == janus::TypeKind::Enum) {
              left = builder.CreateExtractValue(left, 0, "enum.left.tag");
              right = builder.CreateExtractValue(right, 0, "enum.right.tag");
            }
            const bool is_floating = operand_type.is_floating_point();
            const bool is_unsigned_integer =
                operand_type.kind() == janus::TypeKind::Char ||
                (operand_type.is_integer() && !operand_type.is_signed());

            switch (node.operation) {
            case janus::ast::BinaryOperator::Add:
              return is_floating ? builder.CreateFAdd(left, right, "add")
                                 : builder.CreateAdd(left, right, "add");
            case janus::ast::BinaryOperator::Subtract:
              return is_floating ? builder.CreateFSub(left, right, "sub")
                                 : builder.CreateSub(left, right, "sub");
            case janus::ast::BinaryOperator::Multiply:
              return is_floating ? builder.CreateFMul(left, right, "mul")
                                 : builder.CreateMul(left, right, "mul");
            case janus::ast::BinaryOperator::Divide:
              return is_floating
                         ? builder.CreateFDiv(left, right, "div")
                         : emit_integer_division(left, right, operand_type,
                                                 false, is_unsigned_integer,
                                                 builder);
            case janus::ast::BinaryOperator::Remainder:
              return emit_integer_division(left, right, operand_type, true,
                                           is_unsigned_integer, builder);
            case janus::ast::BinaryOperator::Less:
              if (is_floating)
                return builder.CreateFCmpOLT(left, right, "cmp");
              return is_unsigned_integer
                         ? builder.CreateICmpULT(left, right, "cmp")
                         : builder.CreateICmpSLT(left, right, "cmp");
            case janus::ast::BinaryOperator::LessEqual:
              if (is_floating)
                return builder.CreateFCmpOLE(left, right, "cmp");
              return is_unsigned_integer
                         ? builder.CreateICmpULE(left, right, "cmp")
                         : builder.CreateICmpSLE(left, right, "cmp");
            case janus::ast::BinaryOperator::Greater:
              if (is_floating)
                return builder.CreateFCmpOGT(left, right, "cmp");
              return is_unsigned_integer
                         ? builder.CreateICmpUGT(left, right, "cmp")
                         : builder.CreateICmpSGT(left, right, "cmp");
            case janus::ast::BinaryOperator::GreaterEqual:
              if (is_floating)
                return builder.CreateFCmpOGE(left, right, "cmp");
              return is_unsigned_integer
                         ? builder.CreateICmpUGE(left, right, "cmp")
                         : builder.CreateICmpSGE(left, right, "cmp");
            case janus::ast::BinaryOperator::Equal:
            case janus::ast::BinaryOperator::NotEqual: {
              const bool is_not_equal =
                  node.operation == janus::ast::BinaryOperator::NotEqual;
              if (is_floating) {
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
                      "janus_memcmp", ::llvm::FunctionType::get(
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
            return nullptr;
          }
        },
        expression.value);
  }

  ::llvm::LLVMContext &context_;
  std::unique_ptr<::llvm::Module> module_;
  std::unordered_map<std::string, const janus::ast::FunctionDeclaration *>
      functions_;
  std::vector<const janus::ast::GlobalDeclaration *> global_declarations_;
  std::unordered_map<std::string, const janus::ast::GlobalDeclaration *>
      global_by_key_;
  std::unordered_map<std::string, Local> global_storage_;
  std::unordered_map<std::string, std::string> public_global_keys_;
  std::unordered_set<std::string> global_modules_;
  janus::constant::InitializationPlan initialization_plan_;
  std::unordered_set<std::string> constant_global_keys_;
  std::unordered_map<std::string, int> constant_states_;
  std::unordered_map<std::string, janus::constant::Value> constant_values_;
  ::llvm::Function *global_initializer_{};
  ::llvm::Function *global_finalizer_{};
  std::unordered_map<std::string, const janus::ast::ClassDeclaration *>
      classes_;
  std::unordered_map<std::string, const janus::ast::EnumDeclaration *> enums_;
  std::unordered_map<std::string, janus::Type> enum_types_;
  std::unordered_map<std::string, ::llvm::StructType *> llvm_enum_types_;
  std::unordered_map<std::string, EnumSpecialization> enum_specializations_;
  std::unordered_map<std::string, janus::Type> class_types_;
  std::unordered_map<std::string, ::llvm::StructType *> llvm_class_types_;
  std::unordered_map<std::string, ClassSpecialization> class_specializations_;
  std::unordered_map<std::string, janus::Type> pointer_types_;
  std::unordered_map<std::string, const janus::Type *> pointer_elements_;
  std::unordered_map<std::string, janus::Type> function_types_;
  std::unordered_map<std::string, FunctionSignature> function_signatures_;
  std::unordered_map<std::string, ::llvm::Function *> emitted_;
  std::vector<CleanupScope> active_cleanup_scopes_;
  const janus::Type *active_return_type_{};
  std::optional<std::string> active_module_;
  std::size_t string_literal_index_{};
  std::size_t lambda_index_{};
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
