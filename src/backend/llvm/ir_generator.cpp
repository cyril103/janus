#include "janus/backend/llvm/ir_generator.hpp"

#include "janus/semantic/analyzer.hpp"

#include "janus/backend/llvm/type_lowering.hpp"
#include "janus/constant/evaluator.hpp"
#include "janus/diagnostics/compile_error.hpp"
#include "janus/ownership/classifier.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <llvm/ADT/APInt.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
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

bool has_derivation(const std::vector<janus::ast::Derivation> &derivations,
                    janus::ast::DerivationKind kind) {
  return std::any_of(
      derivations.begin(), derivations.end(),
      [kind](const janus::ast::Derivation &item) { return item.kind == kind; });
}

class Generator {
public:
  Generator(::llvm::LLVMContext &context, const janus::ast::Program &program,
            const janus::semantic::AnalysisResult &analysis,
            std::string_view module_name,
            janus::backend::llvm::PanicTraceMode panic_trace,
            bool dependencies_only, const janus::Target &target)
      : context_{context}, module_{std::make_unique<::llvm::Module>(
                               std::string{module_name}, context)},
        source_name_{module_name}, panic_trace_{panic_trace},
        analysis_{analysis}, entry_module_{program.module_name},
        dependencies_only_{dependencies_only}, target_{target} {
#if LLVM_VERSION_MAJOR >= 21
    module_->setTargetTriple(::llvm::Triple{target.triple});
#else
    module_->setTargetTriple(target.triple);
#endif
    module_->setPICLevel(::llvm::PICLevel::BigPIC);
    module_->setPIELevel(::llvm::PIELevel::Large);
    const auto imported_names = [&](const std::optional<std::string> &module,
                                    std::string_view name) {
      std::vector<std::string> names;
      if (!module.has_value())
        return names;
      for (const janus::ast::ImportDeclaration &import : program.imports) {
        if (import.module_name != *module)
          continue;
        if (import.module_alias.has_value())
          names.push_back(source_global_key(import.importing_module,
                                            *import.module_alias + "." +
                                                std::string{name}));
        for (const janus::ast::ImportDeclaration::Symbol &symbol :
             import.symbols)
          if (symbol.name == name)
            names.push_back(source_global_key(
                import.importing_module, symbol.alias.value_or(symbol.name)));
      }
      return names;
    };
    std::unordered_map<std::string, std::size_t> type_name_counts;
    for (const janus::ast::EnumDeclaration &declaration : program.enums) {
      enums_.emplace(
          source_global_key(declaration.module_name, declaration.name),
          &declaration);
      if (!declaration.is_private)
        for (const std::string &alias :
             imported_names(declaration.module_name, declaration.name)) {
          scoped_type_names_.insert(alias);
          enums_.emplace(alias, &declaration);
        }
      ++type_name_counts[declaration.name];
    }
    for (const janus::ast::ClassDeclaration &class_declaration :
         program.classes) {
      classes_.emplace(source_global_key(class_declaration.module_name,
                                         class_declaration.name),
                       &class_declaration);
      if (!class_declaration.is_private)
        for (const std::string &alias : imported_names(
                 class_declaration.module_name, class_declaration.name)) {
          scoped_type_names_.insert(alias);
          classes_.emplace(alias, &class_declaration);
        }
      ++type_name_counts[class_declaration.name];
    }
    for (const janus::ast::EnumDeclaration &declaration : program.enums)
      if (type_name_counts.at(declaration.name) == 1)
        enums_.emplace(declaration.name, &declaration);
    for (const janus::ast::ClassDeclaration &declaration : program.classes)
      if (type_name_counts.at(declaration.name) == 1)
        classes_.emplace(declaration.name, &declaration);

    std::unordered_map<std::string, std::size_t> function_name_counts;
    for (const janus::ast::FunctionDeclaration &function : program.functions) {
      functions_.emplace(source_global_key(function.module_name, function.name),
                         &function);
      if (!function.is_private)
        for (const std::string &alias :
             imported_names(function.module_name, function.name))
          functions_.emplace(alias, &function);
      ++function_name_counts[function.name];
    }
    for (const janus::ast::FunctionDeclaration &function : program.functions)
      if (function_name_counts.at(function.name) == 1)
        functions_.emplace(function.name, &function);
      else
        ambiguous_function_names_.insert(function.name);
    for (const janus::ast::GlobalDeclaration &global : program.globals) {
      if (dependencies_only_ && !is_dependency(global.module_name))
        continue;
      global_declarations_.push_back(&global);
      global_by_key_.emplace(
          source_global_key(global.module_name, global.declaration.name),
          &global);
      if (!global.declaration.is_private)
        public_global_keys_.emplace(
            global.declaration.name,
            source_global_key(global.module_name, global.declaration.name));
      if (!global.declaration.is_private)
        for (const std::string &alias :
             imported_names(global.module_name, global.declaration.name))
          public_global_keys_.emplace(
              alias,
              source_global_key(global.module_name, global.declaration.name));
    }
    initialization_plan_ = janus::constant::plan_initialization(program);
    if (dependencies_only_) {
      const auto imported = [&](const janus::ast::GlobalDeclaration *global) {
        return is_dependency(global->module_name);
      };
      std::erase_if(initialization_plan_.constants,
                    [&](const auto *global) { return !imported(global); });
      std::erase_if(initialization_plan_.dynamic,
                    [&](const auto *global) { return !imported(global); });
    }
    for (const janus::ast::GlobalDeclaration *global :
         initialization_plan_.constants)
      constant_global_keys_.insert(
          source_global_key(global->module_name, global->declaration.name));
  }

  std::unique_ptr<::llvm::Module> generate() {
    for (const janus::ast::GlobalDeclaration *declaration :
         global_declarations_)
      emit_global(*declaration);
    const auto all_dynamic = initialization_plan_.dynamic;
    const auto emit_lifecycle = [&](bool dependencies) {
      initialization_plan_.dynamic = all_dynamic;
      std::erase_if(initialization_plan_.dynamic, [&](const auto *global) {
        return is_dependency(global->module_name) != dependencies;
      });
      global_lifecycle_suffix_ = dependencies ? "_dependencies" : "_consumer";
      emitting_dependency_lifecycle_ = dependencies;
      force_global_lifecycle_ = dependencies;
      global_initializer_ = nullptr;
      global_finalizer_ = nullptr;
      global_initialization_started_ = nullptr;
      global_initialized_count_ = nullptr;
      global_finalization_finished_ = nullptr;
      emit_global_initializer_function();
      emit_global_finalizer_function();
      if (global_initializer_ != nullptr)
        global_initializers_.push_back(global_initializer_);
      if (global_finalizer_ != nullptr)
        global_finalizers_.push_back(global_finalizer_);
    };
    if (dependencies_only_)
      emit_lifecycle(true);
    else {
      emit_lifecycle(true);
      emit_lifecycle(false);
      emit_panic_finalizer_function();
    }
    initialization_plan_.dynamic = all_dynamic;
    for (const auto &[name, class_declaration] : classes_) {
      if (!class_declaration->type_parameters.empty() ||
          (dependencies_only_ &&
           !is_dependency(class_declaration->module_name)))
        continue;
      static_cast<void>(ensure_class(name, {}));
      if (dependencies_only_) {
        const ClassSpecialization &specialization =
            class_specializations_.at(name);
        for (const janus::ast::FunctionDeclaration &method :
             class_declaration->methods)
          if (method.type_parameters.empty())
            static_cast<void>(emit_function(method, {}, class_declaration,
                                            &specialization.substitutions,
                                            name));
        static_cast<void>(emit_destructor(name));
      }
    }
    for (const auto &[name, function] : functions_) {
      static_cast<void>(name);
      if (function->type_parameters.empty() &&
          (!dependencies_only_ || is_dependency(function->module_name)))
        static_cast<void>(emit_function(*function, {}));
    }
    return std::move(module_);
  }

private:
  using Substitutions = std::unordered_map<std::string, const janus::Type *>;

  struct Local {
    ::llvm::Value *storage;
    const janus::Type *type;
    bool is_constant{};
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
    std::vector<janus::ast::ParameterOwnership> parameter_ownership;
    janus::ast::ReturnOwnership return_ownership{
        janus::ast::ReturnOwnership::Unspecified};
  };

  struct CleanupScope {
    const std::vector<const janus::ast::DeferStatement *> *actions;
    std::vector<std::pair<::llvm::Value *, const janus::Type *>> *owned_values;
    std::unordered_map<std::string, Local> *locals;
    const Substitutions *substitutions;
  };

  struct TransientPanicCleanup {
    std::vector<::llvm::Value *> frames;
  };

  [[nodiscard]] bool
  is_dependency(const std::optional<std::string> &module) const {
    return module.has_value() &&
           (!entry_module_.has_value() || *module != *entry_module_);
  }

  static ::llvm::AllocaInst *create_entry_alloca(::llvm::IRBuilder<> &builder,
                                                 ::llvm::Type *type,
                                                 const ::llvm::Twine &name) {
    ::llvm::Function *function = builder.GetInsertBlock()->getParent();
    ::llvm::IRBuilder<> entry_builder{&function->getEntryBlock(),
                                      function->getEntryBlock().begin()};
    return entry_builder.CreateAlloca(type, nullptr, name);
  }

  static bool mark_tail_call_if_eligible(::llvm::Value *return_value,
                                         ::llvm::Function &caller,
                                         ::llvm::IRBuilder<> &builder) {
    auto *call = ::llvm::dyn_cast_or_null<::llvm::CallInst>(return_value);
    if (call == nullptr || caller.isVarArg() ||
        call->getType()->isAggregateType() ||
        call->getFunctionType() != caller.getFunctionType() ||
        call->getCallingConv() != caller.getCallingConv() ||
        call->getParent() != builder.GetInsertBlock() ||
        &builder.GetInsertBlock()->back() != call)
      return false;

    // `musttail` is a backend guarantee, including unoptimized builds: the
    // current frame is reused instead of growing the native stack.  Requiring
    // the call to still be the block's final instruction also deliberately
    // excludes returns with pending defer/ownership cleanup work.
    call->setTailCallKind(::llvm::CallInst::TCK_MustTail);
    return true;
  }

  static std::string source_global_key(const std::optional<std::string> &module,
                                       std::string_view name) {
    return module.has_value() ? *module + "." + std::string{name}
                              : std::string{name};
  }

  template <typename Map>
  auto find_in_active_module(const Map &symbols, std::string_view name) const {
    auto iterator = symbols.find(source_global_key(active_module_, name));
    if (iterator == symbols.end())
      iterator = symbols.find(std::string{name});
    return iterator;
  }

  template <typename Map>
  auto find_type_in_active_module(const Map &symbols,
                                  std::string_view name) const {
    const std::string scoped = source_global_key(active_module_, name);
    if (scoped_type_names_.contains(scoped))
      return symbols.find(scoped);
    auto iterator = symbols.find(std::string{name});
    if (iterator == symbols.end())
      iterator = symbols.find(scoped);
    return iterator;
  }

  const janus::Type &resolve(const janus::ast::TypeReference &reference,
                             const Substitutions &substitutions) {
    if (reference.name == "isize")
      return janus::Type::isize_type(target_.pointer_width);
    if (reference.name == "usize")
      return janus::Type::usize_type(target_.pointer_width);
    if (const janus::Type *type = builtin_type(reference.name))
      return *type;
    if (reference.name == "Function") {
      std::vector<const janus::Type *> signature;
      signature.reserve(reference.type_arguments.size());
      for (const janus::ast::TypeReference &argument : reference.type_arguments)
        signature.push_back(&resolve(argument, substitutions));
      std::vector<const janus::Type *> parameters{signature.begin(),
                                                  signature.end() - 1};
      return ensure_function_type(parameters, *signature.back(),
                                  reference.function_parameter_ownership,
                                  reference.function_return_ownership);
    }
    if (const auto iterator = substitutions.find(reference.name);
        iterator != substitutions.end())
      return *iterator->second;
    if (const auto declaration =
            find_type_in_active_module(enums_, reference.name);
        declaration != enums_.end()) {
      std::vector<const janus::Type *> arguments;
      for (const janus::ast::TypeReference &argument : reference.type_arguments)
        arguments.push_back(&resolve(argument, substitutions));
      return ensure_enum(declaration->first, arguments);
    }
    if (reference.name == "Ptr")
      return ensure_pointer(
          resolve(reference.type_arguments.front(), substitutions));
    std::vector<const janus::Type *> type_arguments;
    type_arguments.reserve(reference.type_arguments.size());
    for (const janus::ast::TypeReference &argument : reference.type_arguments)
      type_arguments.push_back(&resolve(argument, substitutions));
    const auto declaration =
        find_type_in_active_module(classes_, reference.name);
    return ensure_class(declaration->first, type_arguments);
  }

  const janus::Type &
  resolve(const std::optional<janus::ast::TypeReference> &reference,
          const Substitutions &substitutions) {
    if (!reference.has_value())
      throw std::logic_error{"an explicit type is required in this context"};
    return resolve(*reference, substitutions);
  }

  const janus::Type &
  resolve(const janus::semantic::SemanticType &type,
          const Substitutions &substitutions = {}) {
    if (type.is_concrete())
      return *type.concrete;
    if (!type.is_class() && !type.is_enum() && !type.is_pointer() &&
        !type.is_function()) {
      if (const auto substitution = substitutions.find(type.parameter);
          substitution != substitutions.end())
        return *substitution->second;
      throw janus::CompileError{
          janus::DiagnosticCode::BackendLegacy, {},
          "backend cannot lower unresolved analyzed type '" + type.parameter +
              "'"};
    }
    std::vector<const janus::Type *> arguments;
    arguments.reserve(type.type_arguments.size());
    for (const auto &argument : type.type_arguments)
      arguments.push_back(&resolve(argument, substitutions));
    if (type.is_function()) {
      if (arguments.empty())
        throw std::logic_error{
            "analyzed function type has no return type for codegen"};
      std::vector<const janus::Type *> parameters{arguments.begin(),
                                                  arguments.end() - 1};
      return ensure_function_type(parameters, *arguments.back(),
                                  type.function_parameter_ownership,
                                  type.function_return_ownership);
    }
    if (type.is_pointer())
      return ensure_pointer(*arguments.front());
    if (type.is_enum()) {
      const auto declaration =
          find_type_in_active_module(enums_, type.parameter);
      return ensure_enum(declaration->first, arguments);
    }
    const auto declaration =
        find_type_in_active_module(classes_, type.parameter);
    if (declaration == classes_.end())
      throw std::logic_error{"analyzed local type is not available to codegen"};
    return ensure_class(declaration->first, arguments);
  }

  template <typename Declaration>
  void add_associated_type_substitutions(
      Substitutions &substitutions, std::string_view parameter,
      const Declaration &declaration,
      const Substitutions &specialization_substitutions) {
    std::unordered_map<std::string,
                       const janus::ast::AssociatedTypeDeclaration *>
        associated_declarations;
    for (const auto &associated : declaration.associated_types)
      associated_declarations.emplace(associated.name, &associated);

    Substitutions normalized = specialization_substitutions;
    std::unordered_set<std::string> resolving;
    std::function<const janus::Type &(const std::string &)> normalize;
    normalize = [&](const std::string &name) -> const janus::Type & {
      if (const auto ready = normalized.find(name); ready != normalized.end())
        return *ready->second;
      const auto found = associated_declarations.find(name);
      if (found == associated_declarations.end() ||
          !found->second->definition.has_value())
        throw janus::CompileError{
            janus::DiagnosticCode::BackendLegacy, declaration.location,
            "backend cannot normalize associated type '" +
                std::string{parameter} + "." + name + "'"};
      if (!resolving.insert(name).second)
        throw janus::CompileError{
            janus::DiagnosticCode::BackendLegacy, found->second->location,
            "backend encountered a cyclic associated type projection '" +
                std::string{parameter} + "." + name + "'"};

      const auto prepare_dependencies = [&](const auto &self,
                                            const janus::ast::TypeReference &type)
          -> void {
        if (associated_declarations.contains(type.name))
          normalized.insert_or_assign(type.name, &normalize(type.name));
        for (const auto &argument : type.type_arguments)
          self(self, argument);
      };
      prepare_dependencies(prepare_dependencies, *found->second->definition);
      const janus::Type &type =
          resolve(*found->second->definition, normalized);
      resolving.erase(name);
      normalized.insert_or_assign(name, &type);
      return type;
    };

    for (const auto &[name, _] : associated_declarations)
      substitutions.insert_or_assign(std::string{parameter} + "." + name,
                                     &normalize(name));
  }

  void add_type_parameter_substitution(Substitutions &substitutions,
                                       std::string_view parameter,
                                       const janus::Type &argument) {
    substitutions.insert_or_assign(std::string{parameter}, &argument);
    if (argument.kind() == janus::TypeKind::Class ||
        argument.kind() == janus::TypeKind::Struct) {
      const auto specialization =
          class_specializations_.find(std::string{argument.name()});
      if (specialization != class_specializations_.end())
        add_associated_type_substitutions(
            substitutions, parameter, *specialization->second.declaration,
            specialization->second.substitutions);
    } else if (argument.kind() == janus::TypeKind::Enum) {
      const auto specialization =
          enum_specializations_.find(std::string{argument.name()});
      if (specialization != enum_specializations_.end())
        add_associated_type_substitutions(
            substitutions, parameter, *specialization->second.declaration,
            specialization->second.substitutions);
    }
  }

  std::vector<const janus::Type *> effective_type_arguments(
      const std::vector<std::string> &parameters,
      const std::vector<janus::ast::TypeReference> &explicit_arguments,
      const janus::ast::Expression *expression,
      const Substitutions &substitutions, janus::SourceLocation location,
      std::string_view display_name) {
    std::vector<const janus::Type *> arguments;
    if (!explicit_arguments.empty()) {
      arguments.reserve(explicit_arguments.size());
      for (const janus::ast::TypeReference &argument : explicit_arguments)
        arguments.push_back(&resolve(argument, substitutions));
    } else if (!parameters.empty()) {
      const auto inferred =
          analysis_.inferred_generic_arguments.find(expression);
      if (inferred == analysis_.inferred_generic_arguments.end())
        throw janus::CompileError{
            janus::DiagnosticCode::BackendLegacy, location,
            "backend is missing inferred type arguments for '" +
                std::string{display_name} + "'"};
      arguments.reserve(inferred->second.size());
      for (const janus::semantic::SemanticType &argument : inferred->second)
        arguments.push_back(&resolve(argument, substitutions));
    }
    if (arguments.size() != parameters.size())
      throw janus::CompileError{
          janus::DiagnosticCode::BackendLegacy, location,
          "backend received " + std::to_string(arguments.size()) +
              " type argument(s) for '" + std::string{display_name} +
              "', but semantic analysis requires " +
              std::to_string(parameters.size())};
    return arguments;
  }

  const Local *
  find_storage(std::string_view name,
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

  const Local &
  resolve_storage(std::string_view name,
                  const std::unordered_map<std::string, Local> &locals) const {
    return *find_storage(name, locals);
  }

  const Local &resolve_qualified_global(
      const janus::ast::MemberAccessExpression &access) const {
    const auto resolved = analysis_.qualified_global_reads.find(&access);
    if (resolved == analysis_.qualified_global_reads.end())
      throw janus::CompileError{
          access.location,
          "backend has no semantically validated global read for '" +
              qualified_expression_name(*access.object).value_or("<unknown>") +
              "." + access.member + "'"};
    return global_storage_.at(resolved->second);
  }

  const Local &resolve_qualified_global(
      const janus::ast::AssignmentStatement &assignment) const {
    const auto resolved = analysis_.qualified_global_writes.find(&assignment);
    if (resolved == analysis_.qualified_global_writes.end())
      throw janus::CompileError{
          assignment.location,
          "backend has no semantically validated global write for '" +
              assignment.object + "." + assignment.name + "'"};
    return global_storage_.at(resolved->second);
  }

  static std::optional<std::string>
  qualified_expression_name(const janus::ast::Expression &expression) {
    if (const auto *identifier =
            std::get_if<janus::ast::IdentifierExpression>(&expression.value))
      return identifier->name;
    if (const auto *member = std::get_if<janus::ast::MemberAccessExpression>(
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
    if (const auto analyzed = analysis_.global_constant_values.find(key);
        analyzed != analysis_.global_constant_values.end())
      return analyzed->second;
    const int state = constant_states_[key];
    if (state == 1)
      throw janus::CompileError{
          janus::DiagnosticCode::BackendCyclicGlobalConstant,
          global.declaration.location,
          "cyclic global constant dependency involving '" + key + "'"};
    if (state == 2)
      return constant_values_.at(key);

    constant_states_[key] = 1;
    const janus::constant::Resolver resolver =
        [&](const std::optional<std::string> &qualified_module,
            std::string_view name, janus::SourceLocation location)
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
        throw janus::CompileError{
            location, "global constant '" + dependency_key + "' is private"};
      if (target.declaration.is_mutable)
        throw janus::CompileError{
            location, "global constant initializer cannot depend on mutable "
                      "global '" +
                          dependency_key + "'"};
      return evaluate_global_constant(target);
    };
    const janus::constant::ConstructorResolver constructor_resolver =
        [&](std::string_view name, const std::optional<std::string> &enum_case,
            const std::vector<janus::ast::TypeReference> &type_references,
            janus::SourceLocation)
        -> std::optional<janus::constant::ConstructorShape> {
      std::vector<const janus::Type *> type_arguments;
      for (const janus::ast::TypeReference &argument : type_references)
        type_arguments.push_back(&resolve(argument, {}));
      if (!enum_case.has_value()) {
        const auto declaration = classes_.find(std::string{name});
        if (declaration == classes_.end() ||
            !declaration->second->is_value_type)
          return std::nullopt;
        const janus::Type &aggregate_type = ensure_class(name, type_arguments);
        const ClassSpecialization &specialization =
            class_specializations_.at(std::string{aggregate_type.name()});
        janus::constant::ConstructorShape shape{
            &aggregate_type, std::nullopt, {}};
        for (std::size_t index = 0;
             index < specialization.declaration->constructor_fields.size();
             ++index)
          shape.fields.emplace_back(
              index,
              &resolve(specialization.declaration->constructor_fields[index]
                           .declared_type,
                       specialization.substitutions));
        return shape;
      }
      if (!enums_.contains(std::string{name}))
        return std::nullopt;
      const janus::Type &aggregate_type = ensure_enum(name, type_arguments);
      const EnumSpecialization &specialization =
          enum_specializations_.at(std::string{aggregate_type.name()});
      const auto matched =
          std::find_if(specialization.declaration->cases.begin(),
                       specialization.declaration->cases.end(),
                       [&](const janus::ast::EnumDeclaration::Case &candidate) {
                         return candidate.name == *enum_case;
                       });
      if (matched == specialization.declaration->cases.end())
        return std::nullopt;
      janus::constant::ConstructorShape shape{
          &aggregate_type, matched->value, {}};
      const unsigned start =
          enum_case_payload_start(aggregate_type.name(), *enum_case);
      unsigned field = start;
      for (const janus::ast::TypeReference &payload : matched->payload_types) {
        const janus::Type &payload_type =
            resolve(payload, specialization.substitutions);
        if (payload_type.kind() != janus::TypeKind::Unit)
          shape.fields.emplace_back(field++, &payload_type);
      }
      return shape;
    };
    const janus::Type &type = resolve(global.declaration.declared_type, {});
    std::function<std::optional<janus::constant::Value>(
        std::string_view, const std::vector<janus::constant::Value> &,
        janus::SourceLocation)>
        call_constant_function;
    call_constant_function =
        [&](std::string_view name,
            const std::vector<janus::constant::Value> &arguments,
            janus::SourceLocation location)
        -> std::optional<janus::constant::Value> {
      const auto found = functions_.find(std::string{name});
      if (found == functions_.end() || !found->second->is_constant)
        return std::nullopt;
      const janus::ast::FunctionDeclaration &function = *found->second;
      if (arguments.size() != function.parameters.size())
        throw janus::CompileError{
            location, "const def received an invalid argument count"};
      std::unordered_map<std::string, janus::constant::Value> locals;
      for (std::size_t index = 0; index < arguments.size(); ++index)
        locals.emplace(function.parameters[index].name, arguments[index]);
      const janus::constant::Resolver function_resolver =
          [&](const std::optional<std::string> &module, std::string_view value,
              janus::SourceLocation reference_location)
          -> std::optional<janus::constant::Value> {
        if (!module.has_value())
          if (const auto local = locals.find(std::string{value});
              local != locals.end())
            return local->second;
        const std::string key = source_global_key(module, value);
        if (const auto dependency = global_by_key_.find(key);
            dependency != global_by_key_.end())
          return evaluate_global_constant(*dependency->second);
        throw janus::CompileError{reference_location,
                                  "const def references non-constant value"};
      };
      const janus::Type &return_type = resolve(function.return_type, {});
      return janus::constant::evaluate_statements(
          function.body, &return_type, std::move(locals), function_resolver,
          constructor_resolver, call_constant_function);
    };
    janus::constant::Value value = janus::constant::evaluate(
        *global.declaration.initializer, &type, resolver, constructor_resolver,
        call_constant_function);
    constant_states_[key] = 2;
    auto [iterator, inserted] = constant_values_.emplace(key, std::move(value));
    static_cast<void>(inserted);
    return iterator->second;
  }

  ::llvm::Constant *emit_static_initializer(const janus::constant::Value &value,
                                            const janus::Type &type) {
    ::llvm::Type *llvm_type = lower_type(type, context_);
    if (type.is_integer())
      return ::llvm::ConstantInt::get(
          llvm_type, std::get<std::uint64_t>(value.data), type.is_signed());
    if (type.is_floating_point())
      return ::llvm::ConstantFP::get(llvm_type, std::get<double>(value.data));
    if (type.kind() == janus::TypeKind::Bool)
      return ::llvm::ConstantInt::get(llvm_type, std::get<bool>(value.data),
                                      false);
    if (type.kind() == janus::TypeKind::Char)
      return ::llvm::ConstantInt::get(
          llvm_type, static_cast<std::uint32_t>(std::get<char32_t>(value.data)),
          false);
    if (type.kind() == janus::TypeKind::Struct ||
        type.kind() == janus::TypeKind::Enum) {
      const auto &aggregate =
          *std::get<std::shared_ptr<janus::constant::AggregateValue>>(
              value.data);
      auto *struct_type = ::llvm::cast<::llvm::StructType>(llvm_type);
      std::vector<::llvm::Constant *> fields;
      fields.reserve(struct_type->getNumElements());
      for (unsigned index = 0; index < struct_type->getNumElements(); ++index)
        fields.push_back(
            ::llvm::Constant::getNullValue(struct_type->getElementType(index)));
      if (aggregate.tag.has_value())
        fields[0] = ::llvm::ConstantInt::get(::llvm::Type::getInt32Ty(context_),
                                             *aggregate.tag, true);
      for (const auto &[index, field] : aggregate.fields)
        fields[index] = emit_static_initializer(field, *field.type);
      return ::llvm::ConstantStruct::get(struct_type, fields);
    }

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
    ::llvm::Constant *pointer = ::llvm::ConstantExpr::getInBoundsGetElementPtr(
        data->getType(), storage, indices);
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
    if (declaration.is_constant) {
      static_cast<void>(evaluate_global_constant(global));
      return;
    }
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
    if (is_dependency(global.module_name))
      storage->setMetadata(
          "janus.module",
          ::llvm::MDNode::get(
              context_, ::llvm::MDString::get(context_, *global.module_name)));
    global_storage_.emplace(
        source_global_key(global.module_name, declaration.name),
        Local{storage, &type});
  }

  void emit_global_initializer_function() {
    if (initialization_plan_.dynamic.empty() && !force_global_lifecycle_)
      return;

    auto *boolean_type = ::llvm::Type::getInt1Ty(context_);
    auto *count_type = ::llvm::Type::getInt64Ty(context_);
    global_initialization_started_ = new ::llvm::GlobalVariable(
        *module_, boolean_type, false, ::llvm::GlobalValue::ExternalLinkage,
        ::llvm::ConstantInt::getFalse(context_),
        "__janus_globals_initialization_started" + global_lifecycle_suffix_);
    global_initialized_count_ = new ::llvm::GlobalVariable(
        *module_, count_type, false, ::llvm::GlobalValue::ExternalLinkage,
        ::llvm::ConstantInt::get(count_type, 0),
        "__janus_globals_initialized_count" + global_lifecycle_suffix_);
    global_initialization_started_->setVisibility(
        ::llvm::GlobalValue::HiddenVisibility);
    global_initialized_count_->setVisibility(
        ::llvm::GlobalValue::HiddenVisibility);
    if (emitting_dependency_lifecycle_) {
      const auto metadata = ::llvm::MDNode::get(
          context_, ::llvm::MDString::get(context_, "dependencies"));
      global_initialization_started_->setMetadata("janus.module", metadata);
      global_initialized_count_->setMetadata("janus.module", metadata);
    }
    auto *function_type =
        ::llvm::FunctionType::get(::llvm::Type::getVoidTy(context_), false);
    global_initializer_ = ::llvm::Function::Create(
        function_type, ::llvm::Function::ExternalLinkage,
        "__janus_init_globals" + global_lifecycle_suffix_, *module_);
    global_initializer_->setVisibility(::llvm::GlobalValue::HiddenVisibility);
    if (emitting_dependency_lifecycle_)
      global_initializer_->addFnAttr("janus.module", "dependencies");
    auto *entry =
        ::llvm::BasicBlock::Create(context_, "entry", global_initializer_);
    ::llvm::IRBuilder<> builder{entry};
    auto *initialize =
        ::llvm::BasicBlock::Create(context_, "initialize", global_initializer_);
    auto *done =
        ::llvm::BasicBlock::Create(context_, "done", global_initializer_);
    ::llvm::Value *started = builder.CreateLoad(
        boolean_type, global_initialization_started_, "initialization.started");
    builder.CreateCondBr(started, done, initialize);
    builder.SetInsertPoint(initialize);
    builder.CreateStore(::llvm::ConstantInt::getTrue(context_),
                        global_initialization_started_);
    const Substitutions substitutions;
    const std::unordered_map<std::string, Local> locals;
    const auto previous_module = active_module_;
    const janus::Type *previous_return_type = active_return_type_;
    active_return_type_ = &janus::Type::unit_type();
    for (std::size_t index = 0; index < initialization_plan_.dynamic.size();
         ++index) {
      const janus::ast::GlobalDeclaration *global =
          initialization_plan_.dynamic[index];
      active_module_ = global->module_name;
      const janus::Type &type =
          resolve(global->declaration.declared_type, substitutions);
      ::llvm::Value *value =
          emit_expression(*global->declaration.initializer, type, substitutions,
                          locals, builder);
      const Local &storage = global_storage_.at(
          source_global_key(global->module_name, global->declaration.name));
      builder.CreateStore(value, storage.storage);
      builder.CreateStore(::llvm::ConstantInt::get(count_type, index + 1),
                          global_initialized_count_);
    }
    builder.CreateBr(done);
    builder.SetInsertPoint(done);
    builder.CreateRetVoid();
    active_module_ = previous_module;
    active_return_type_ = previous_return_type;
  }

  void emit_global_finalizer_function() {
    const bool has_owned =
        std::any_of(initialization_plan_.dynamic.begin(),
                    initialization_plan_.dynamic.end(),
                    [&](const janus::ast::GlobalDeclaration *global) {
                      const janus::Type &type =
                          resolve(global->declaration.declared_type, {});
                      return owns_value(type);
                    });
    if (!has_owned && !force_global_lifecycle_)
      return;

    auto *function_type =
        ::llvm::FunctionType::get(::llvm::Type::getVoidTy(context_), false);
    global_finalizer_ = ::llvm::Function::Create(
        function_type, ::llvm::Function::ExternalLinkage,
        "__janus_fini_globals" + global_lifecycle_suffix_, *module_);
    global_finalizer_->setVisibility(::llvm::GlobalValue::HiddenVisibility);
    if (emitting_dependency_lifecycle_)
      global_finalizer_->addFnAttr("janus.module", "dependencies");
    auto *entry =
        ::llvm::BasicBlock::Create(context_, "entry", global_finalizer_);
    ::llvm::IRBuilder<> builder{entry};
    auto *boolean_type = builder.getInt1Ty();
    global_finalization_finished_ = new ::llvm::GlobalVariable(
        *module_, boolean_type, false, ::llvm::GlobalValue::ExternalLinkage,
        ::llvm::ConstantInt::getFalse(context_),
        "__janus_globals_finalization_finished" + global_lifecycle_suffix_);
    global_finalization_finished_->setVisibility(
        ::llvm::GlobalValue::HiddenVisibility);
    if (emitting_dependency_lifecycle_)
      global_finalization_finished_->setMetadata(
          "janus.module",
          ::llvm::MDNode::get(context_,
                              ::llvm::MDString::get(context_, "dependencies")));
    auto *cleanup =
        ::llvm::BasicBlock::Create(context_, "cleanup", global_finalizer_);
    auto *done =
        ::llvm::BasicBlock::Create(context_, "done", global_finalizer_);
    ::llvm::Value *finished = builder.CreateLoad(
        boolean_type, global_finalization_finished_, "finalization.finished");
    builder.CreateCondBr(finished, done, cleanup);
    builder.SetInsertPoint(cleanup);
    builder.CreateStore(::llvm::ConstantInt::getTrue(context_),
                        global_finalization_finished_);
    for (std::size_t index = initialization_plan_.dynamic.size();
         index-- > 0;) {
      const janus::ast::GlobalDeclaration &global =
          *initialization_plan_.dynamic[index];
      const janus::Type &type = resolve(global.declaration.declared_type, {});
      if (!owns_value(type))
        continue;
      auto *release = ::llvm::BasicBlock::Create(
          context_, "release." + global.declaration.name, global_finalizer_);
      auto *next = ::llvm::BasicBlock::Create(
          context_, "next." + global.declaration.name, global_finalizer_);
      ::llvm::Value *initialized_count = builder.CreateLoad(
          builder.getInt64Ty(), global_initialized_count_, "initialized.count");
      builder.CreateCondBr(
          builder.CreateICmpUGE(
              initialized_count,
              ::llvm::ConstantInt::get(builder.getInt64Ty(), index + 1)),
          release, next);
      builder.SetInsertPoint(release);
      const Local &storage = global_storage_.at(
          source_global_key(global.module_name, global.declaration.name));
      ::llvm::Value *value =
          builder.CreateLoad(lower_type(type, context_), storage.storage,
                             global.declaration.name + ".global.cleanup");
      emit_owned_value_cleanup(value, type, builder);
      builder.CreateBr(next);
      builder.SetInsertPoint(next);
    }
    builder.CreateBr(done);
    builder.SetInsertPoint(done);
    builder.CreateRetVoid();
  }

  void emit_panic_finalizer_function() {
    if (global_finalizers_.empty())
      return;
    auto *function_type =
        ::llvm::FunctionType::get(::llvm::Type::getVoidTy(context_), false);
    panic_finalizer_ = ::llvm::Function::Create(
        function_type, ::llvm::Function::InternalLinkage,
        "__janus_fini_globals_panic", *module_);
    auto *entry =
        ::llvm::BasicBlock::Create(context_, "entry", panic_finalizer_);
    ::llvm::IRBuilder<> builder{entry};
    for (auto finalizer = global_finalizers_.rbegin();
         finalizer != global_finalizers_.rend(); ++finalizer)
      builder.CreateCall(*finalizer);
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

  std::string function_key(
      const std::vector<const janus::Type *> &parameters,
      const janus::Type &return_type,
      const std::vector<janus::ast::ParameterOwnership> &ownerships,
      janus::ast::ReturnOwnership return_ownership) const {
    std::string key{"Function"};
    for (std::size_t index = 0; index < parameters.size(); ++index) {
      const auto ownership = index < ownerships.size()
                                 ? ownerships[index]
                                 : janus::ast::ParameterOwnership::Unspecified;
      key += ownership == janus::ast::ParameterOwnership::Borrow
                 ? "__borrow_"
                 : (ownership == janus::ast::ParameterOwnership::BorrowMutable
                        ? "__borrow_mut_"
                        : "__");
      key += std::string{parameters[index]->name()};
    }
    if (return_ownership == janus::ast::ReturnOwnership::Borrow)
      key += "__borrow_return";
    else if (return_ownership ==
             janus::ast::ReturnOwnership::BorrowMutable)
      key += "__borrow_mut_return";
    key += "__to__" + std::string{return_type.name()};
    return key;
  }

  const janus::Type &
  ensure_function_type(const std::vector<const janus::Type *> &parameters,
                       const janus::Type &return_type,
                       std::vector<janus::ast::ParameterOwnership> ownerships =
                           {},
                       janus::ast::ReturnOwnership return_ownership =
                           janus::ast::ReturnOwnership::Unspecified) {
    if (ownerships.empty())
      ownerships.resize(parameters.size(),
                        janus::ast::ParameterOwnership::Unspecified);
    const std::string key =
        function_key(parameters, return_type, ownerships, return_ownership);
    if (const auto iterator = function_types_.find(key);
        iterator != function_types_.end())
      return iterator->second;
    auto [iterator, inserted] =
        function_types_.emplace(key, janus::Type::function_type(key));
    static_cast<void>(inserted);
    function_signatures_.emplace(
        key, FunctionSignature{parameters, &return_type, std::move(ownerships),
                               return_ownership});
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
      for (const janus::ast::TypeReference &payload : enum_case.payload_types) {
        const janus::Type &payload_type = resolve(payload, substitutions);
        if (payload_type.kind() != janus::TypeKind::Unit)
          fields.push_back(lower_type(payload_type, context_));
      }
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
                                   std::string_view case_name) {
    const EnumSpecialization &specialization =
        enum_specializations_.at(std::string{enum_name});
    const janus::ast::EnumDeclaration &declaration =
        *specialization.declaration;
    unsigned index = 1;
    for (const janus::ast::EnumDeclaration::Case &enum_case :
         declaration.cases) {
      if (enum_case.name == case_name)
        return index;
      for (const janus::ast::TypeReference &payload : enum_case.payload_types)
        if (resolve(payload, specialization.substitutions).kind() !=
            janus::TypeKind::Unit)
          ++index;
    }
    return index;
  }

  bool is_explicit_cast(const janus::ast::CallExpression &call) const {
    if (call.callee == "numericCast" || call.callee == "saturatingCast" ||
        call.callee == "truncatingCast")
      return true;
    const janus::Type *type = builtin_type(call.callee);
    if (type != nullptr)
      return type->kind() != janus::TypeKind::String &&
             type->kind() != janus::TypeKind::Unit;
    return call.callee == "Ptr" ||
           find_type_in_active_module(classes_, call.callee) !=
               classes_.end() ||
           find_type_in_active_module(enums_, call.callee) != enums_.end();
  }

  const janus::Type &cast_destination(const janus::ast::CallExpression &call,
                                      const Substitutions &substitutions) {
    if (call.callee == "numericCast" || call.callee == "saturatingCast" ||
        call.callee == "truncatingCast")
      return resolve(call.type_arguments.front(), substitutions);
    return resolve(janus::ast::TypeReference{call.callee, call.location,
                                             call.type_arguments},
                   substitutions);
  }

  ::llvm::Value *emit_clamped_numeric_cast(::llvm::Value *source,
                                           const janus::Type &source_type,
                                           const janus::Type &destination,
                                           ::llvm::IRBuilder<> &builder) {
    ::llvm::Type *destination_type = lower_type(destination, context_);
    if (destination.is_floating_point()) {
      ::llvm::Value *converted = nullptr;
      if (source_type.is_floating_point())
        converted = builder.CreateFPCast(source, destination_type,
                                         "saturating.floating");
      else if (source_type.is_signed())
        converted =
            builder.CreateSIToFP(source, destination_type, "saturating.signed");
      else
        converted = builder.CreateUIToFP(source, destination_type,
                                         "saturating.unsigned");
      if (source_type.is_floating_point()) {
        ::llvm::Value *is_nan = builder.CreateFCmpUNO(source, source);
        ::llvm::Value *is_positive = builder.CreateFCmpOGT(
            source, ::llvm::ConstantFP::get(source->getType(), 0.0));
        const double maximum = destination.kind() == janus::TypeKind::Float
                                   ? std::numeric_limits<float>::max()
                                   : std::numeric_limits<double>::max();
        ::llvm::Value *outside_range = builder.CreateFCmpOGT(
            builder.CreateUnaryIntrinsic(::llvm::Intrinsic::fabs, source),
            ::llvm::ConstantFP::get(source->getType(), maximum));
        ::llvm::Value *limit = builder.CreateSelect(
            is_positive, ::llvm::ConstantFP::get(destination_type, maximum),
            ::llvm::ConstantFP::get(destination_type, -maximum));
        converted = builder.CreateSelect(outside_range, limit, converted);
        converted = builder.CreateSelect(
            is_nan, ::llvm::ConstantFP::get(destination_type, 0.0), converted);
      }
      return converted;
    }

    if (source_type.is_floating_point()) {
      const unsigned bits = destination.bit_width();
      const double lower = destination.is_signed()
                               ? -std::ldexp(1.0, static_cast<int>(bits - 1))
                               : 0.0;
      const double upper = std::ldexp(
          1.0, static_cast<int>(bits - (destination.is_signed() ? 1 : 0)));
      ::llvm::Value *is_nan = builder.CreateFCmpUNO(source, source);
      ::llvm::Value *below = builder.CreateFCmpOLT(
          source, ::llvm::ConstantFP::get(source->getType(), lower));
      ::llvm::Value *above = builder.CreateFCmpOGE(
          source, ::llvm::ConstantFP::get(source->getType(), upper));
      ::llvm::Value *safe = builder.CreateSelect(
          builder.CreateOr(is_nan, builder.CreateOr(below, above)),
          ::llvm::ConstantFP::get(source->getType(), 0.0), source);
      ::llvm::Value *converted =
          destination.is_signed()
              ? builder.CreateFPToSI(safe, destination_type,
                                     "clamped.floating.to.signed")
              : builder.CreateFPToUI(safe, destination_type,
                                     "clamped.floating.to.unsigned");
      const std::uint64_t maximum =
          destination.is_signed()
              ? (std::uint64_t{1} << (bits - 1)) - 1
              : (bits == 64 ? std::numeric_limits<std::uint64_t>::max()
                            : (std::uint64_t{1} << bits) - 1);
      const std::uint64_t minimum =
          destination.is_signed() ? std::uint64_t{1} << (bits - 1) : 0;
      converted = builder.CreateSelect(
          below, ::llvm::ConstantInt::get(destination_type, minimum),
          converted);
      converted = builder.CreateSelect(
          above, ::llvm::ConstantInt::get(destination_type, maximum),
          converted);
      return converted;
    }

    auto *wide_type = builder.getInt128Ty();
    ::llvm::Value *wide = source_type.is_signed()
                              ? builder.CreateSExt(source, wide_type)
                              : builder.CreateZExt(source, wide_type);
    const unsigned bits = destination.bit_width();
    const ::llvm::APInt minimum =
        destination.is_signed()
            ? ::llvm::APInt::getSignedMinValue(bits).sext(128)
            : ::llvm::APInt(128, 0);
    const ::llvm::APInt maximum =
        destination.is_signed()
            ? ::llvm::APInt::getSignedMaxValue(bits).sext(128)
            : ::llvm::APInt::getMaxValue(bits).zext(128);
    ::llvm::Value *below = builder.CreateICmpSLT(
        wide, ::llvm::ConstantInt::get(context_, minimum));
    ::llvm::Value *above = builder.CreateICmpSGT(
        wide, ::llvm::ConstantInt::get(context_, maximum));
    wide = builder.CreateSelect(
        below, ::llvm::ConstantInt::get(context_, minimum), wide);
    wide = builder.CreateSelect(
        above, ::llvm::ConstantInt::get(context_, maximum), wide);
    return builder.CreateTruncOrBitCast(wide, destination_type,
                                        "saturating.integer");
  }

  ::llvm::Value *emit_truncating_numeric_cast(::llvm::Value *source,
                                              const janus::Type &source_type,
                                              const janus::Type &destination,
                                              ::llvm::IRBuilder<> &builder) {
    if (source_type.is_floating_point() || destination.is_floating_point())
      return emit_clamped_numeric_cast(source, source_type, destination,
                                       builder);
    return builder.CreateIntCast(source, lower_type(destination, context_),
                                 source_type.is_signed(), "truncating.integer");
  }

  ::llvm::Value *emit_checked_numeric_cast(::llvm::Value *source,
                                           const janus::Type &source_type,
                                           const janus::Type &destination,
                                           ::llvm::IRBuilder<> &builder) {
    const auto error_declaration =
        find_type_in_active_module(enums_, "NumericCastError");
    const janus::Type &error_type = ensure_enum(error_declaration->first, {});
    const auto result_declaration =
        find_type_in_active_module(enums_, "Result");
    const janus::Type &result_type =
        ensure_enum(result_declaration->first, {&destination, &error_type});
    const auto error_case_value = [&](std::string_view case_name) {
      return static_cast<std::uint32_t>(
          enum_case_value(error_type.name(), case_name));
    };

    ::llvm::Value *success = builder.getTrue();
    ::llvm::Value *error_code = builder.getInt32(error_case_value("Overflow"));
    const auto fail = [&](::llvm::Value *condition, std::uint32_t code) {
      ::llvm::Value *was_success = success;
      error_code = builder.CreateSelect(builder.CreateAnd(success, condition),
                                        builder.getInt32(code), error_code,
                                        "checked.error");
      success = builder.CreateAnd(was_success, builder.CreateNot(condition),
                                  "checked.success");
    };

    ::llvm::Value *converted =
        emit_clamped_numeric_cast(source, source_type, destination, builder);
    if (source_type.is_floating_point()) {
      ::llvm::Value *non_finite = builder.CreateOr(
          builder.CreateFCmpUNO(source, source),
          builder.CreateFCmpOEQ(
              builder.CreateUnaryIntrinsic(::llvm::Intrinsic::fabs, source),
              ::llvm::ConstantFP::getInfinity(source->getType())));
      fail(non_finite, error_case_value("NonFinite"));
      if (destination.is_integer()) {
        const unsigned bits = destination.bit_width();
        const double lower = destination.is_signed()
                                 ? -std::ldexp(1.0, static_cast<int>(bits - 1))
                                 : 0.0;
        const double safety_exclusive_maximum = std::ldexp(
            1.0, static_cast<int>(bits - (destination.is_signed() ? 1 : 0)));
        const double diagnostic_maximum =
            destination.is_signed()
                ? static_cast<double>((std::uint64_t{1} << (bits - 1)) - 1)
                : (bits == 64
                       ? static_cast<double>(
                             std::numeric_limits<std::uint64_t>::max())
                       : static_cast<double>((std::uint64_t{1} << bits) - 1));
        if (!destination.is_signed())
          fail(builder.CreateFCmpOLT(
                   source, ::llvm::ConstantFP::get(source->getType(), 0.0)),
               error_case_value("IncompatibleSign"));
        else
          fail(builder.CreateFCmpOLT(
                   source, ::llvm::ConstantFP::get(source->getType(), lower)),
               error_case_value("Underflow"));
        ::llvm::Value *above_maximum = builder.CreateOr(
            builder.CreateFCmpOGE(
                source, ::llvm::ConstantFP::get(source->getType(),
                                                safety_exclusive_maximum)),
            builder.CreateFCmpOGT(source,
                                  ::llvm::ConstantFP::get(source->getType(),
                                                          diagnostic_maximum)));
        fail(above_maximum, error_case_value("Overflow"));
        ::llvm::Value *truncated =
            builder.CreateUnaryIntrinsic(::llvm::Intrinsic::trunc, source);
        fail(builder.CreateFCmpONE(source, truncated),
             error_case_value("FractionalLoss"));
      } else if (destination.bit_width() < source_type.bit_width()) {
        ::llvm::Value *overflow = builder.CreateFCmpOEQ(
            builder.CreateUnaryIntrinsic(::llvm::Intrinsic::fabs, converted),
            ::llvm::ConstantFP::getInfinity(converted->getType()));
        fail(overflow, error_case_value("Overflow"));
        ::llvm::Value *roundtrip =
            builder.CreateFPExt(converted, source->getType());
        fail(builder.CreateFCmpONE(source, roundtrip),
             error_case_value("PrecisionLoss"));
      }
    } else if (destination.is_integer()) {
      auto *wide_type = builder.getInt128Ty();
      ::llvm::Value *wide = source_type.is_signed()
                                ? builder.CreateSExt(source, wide_type)
                                : builder.CreateZExt(source, wide_type);
      const unsigned bits = destination.bit_width();
      const ::llvm::APInt minimum =
          destination.is_signed()
              ? ::llvm::APInt::getSignedMinValue(bits).sext(128)
              : ::llvm::APInt(128, 0);
      const ::llvm::APInt maximum =
          destination.is_signed()
              ? ::llvm::APInt::getSignedMaxValue(bits).sext(128)
              : ::llvm::APInt::getMaxValue(bits).zext(128);
      if (!destination.is_signed() && source_type.is_signed())
        fail(
            builder.CreateICmpSLT(wide, ::llvm::ConstantInt::get(wide_type, 0)),
            error_case_value("IncompatibleSign"));
      else
        fail(builder.CreateICmpSLT(wide,
                                   ::llvm::ConstantInt::get(context_, minimum)),
             error_case_value("Underflow"));
      fail(builder.CreateICmpSGT(wide,
                                 ::llvm::ConstantInt::get(context_, maximum)),
           error_case_value("Overflow"));
    } else {
      const unsigned bits = source_type.bit_width();
      const double lower = source_type.is_signed()
                               ? -std::ldexp(1.0, static_cast<int>(bits - 1))
                               : 0.0;
      const double upper = std::ldexp(
          1.0, static_cast<int>(bits - (source_type.is_signed() ? 1 : 0)));
      ::llvm::Value *outside_range = builder.CreateOr(
          builder.CreateFCmpOLT(
              converted, ::llvm::ConstantFP::get(converted->getType(), lower)),
          builder.CreateFCmpOGE(
              converted, ::llvm::ConstantFP::get(converted->getType(), upper)));
      ::llvm::Value *safe = builder.CreateSelect(
          outside_range, ::llvm::ConstantFP::get(converted->getType(), 0.0),
          converted, "checked.safe.floating");
      ::llvm::Value *roundtrip =
          source_type.is_signed()
              ? builder.CreateFPToSI(safe, source->getType())
              : builder.CreateFPToUI(safe, source->getType());
      fail(builder.CreateICmpNE(source, roundtrip),
           error_case_value("PrecisionLoss"));
    }

    auto *llvm_error_type =
        ::llvm::cast<::llvm::StructType>(lower_type(error_type, context_));
    ::llvm::Value *error =
        builder.CreateInsertValue(::llvm::UndefValue::get(llvm_error_type),
                                  error_code, 0, "numeric.cast.error");
    auto *llvm_result_type =
        ::llvm::cast<::llvm::StructType>(lower_type(result_type, context_));
    ::llvm::Value *result = ::llvm::UndefValue::get(llvm_result_type);
    result = builder.CreateInsertValue(
        result,
        builder.CreateSelect(
            success,
            builder.getInt32(enum_case_value(result_type.name(), "Ok")),
            builder.getInt32(enum_case_value(result_type.name(), "Error"))),
        0, "checked.result.tag");
    result = builder.CreateInsertValue(
        result, converted, enum_case_payload_start(result_type.name(), "Ok"),
        "checked.result.value");
    return builder.CreateInsertValue(
        result, error, enum_case_payload_start(result_type.name(), "Error"),
        "checked.result.error");
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
    ::llvm::StructType *llvm_class_type = ::llvm::StructType::create(
        context_,
        std::string{declaration.is_value_type ? "struct." : "class."} + key);
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

  bool owns_value(const janus::Type &type) {
    return janus::ownership::recursively_owns_value(
        type,
        [](const janus::Type &candidate) {
          return candidate.kind() == janus::TypeKind::Class ||
                 candidate.kind() == janus::TypeKind::Pointer ||
                 candidate.kind() == janus::TypeKind::Function;
        },
        [&](const janus::Type &candidate, const auto &visit) {
          if (candidate.kind() == janus::TypeKind::Struct) {
            const ClassSpecialization &specialization =
                class_specializations_.at(std::string{candidate.name()});
            for (const janus::ast::ValueDeclaration &field :
                 specialization.declaration->constructor_fields)
              visit(resolve(field.declared_type, specialization.substitutions));
            return;
          }
          if (candidate.kind() != janus::TypeKind::Enum)
            return;
          const EnumSpecialization &specialization =
              enum_specializations_.at(std::string{candidate.name()});
          for (const janus::ast::EnumDeclaration::Case &enum_case :
               specialization.declaration->cases)
            for (const janus::ast::TypeReference &payload :
                 enum_case.payload_types)
              visit(resolve(payload, specialization.substitutions));
        });
  }

  void emit_owned_value_cleanup(::llvm::Value *value, const janus::Type &type,
                                ::llvm::IRBuilder<> &builder) {
    if (!owns_value(type))
      return;
    ::llvm::FunctionCallee free_function = module_->getOrInsertFunction(
        "janus_free", ::llvm::FunctionType::get(builder.getVoidTy(),
                                                {builder.getPtrTy()}, false));
    if (type.kind() == janus::TypeKind::Class) {
      const TransientPanicCleanup caller_cleanup =
          emitting_panic_cleanup_ || emitting_inline_cleanup_
              ? TransientPanicCleanup{}
              : push_transient_panic_cleanup(builder);
      auto *frame_type = ::llvm::StructType::get(
          context_,
          {builder.getPtrTy(), builder.getPtrTy(), builder.getPtrTy()});
      ::llvm::Value *release_frame =
          create_entry_alloca(builder, frame_type, "destructor.release.frame");
      ::llvm::FunctionCallee push = module_->getOrInsertFunction(
          "janus_push_panic_cleanup",
          ::llvm::FunctionType::get(
              builder.getVoidTy(),
              {builder.getPtrTy(), builder.getPtrTy(), builder.getPtrTy()},
              false));
      ::llvm::FunctionCallee pop = module_->getOrInsertFunction(
          "janus_pop_panic_cleanup",
          ::llvm::FunctionType::get(builder.getVoidTy(), {builder.getPtrTy()},
                                    false));
      builder.CreateCall(push,
                         {release_frame, free_function.getCallee(), value});
      ::llvm::Function *destructor =
          emit_destructor(std::string{type.name()});
      builder.CreateCall(destructor, {value});
      builder.CreateCall(pop, {release_frame});
      pop_transient_panic_cleanup(caller_cleanup, builder);
      builder.CreateCall(free_function, {value});
      return;
    }
    if (type.kind() == janus::TypeKind::Pointer) {
      builder.CreateCall(free_function, {value});
      return;
    }
    if (type.kind() == janus::TypeKind::Function) {
      ::llvm::Value *environment =
          builder.CreateExtractValue(value, 1, "aggregate.lambda.environment");
      ::llvm::Value *owns_environment = builder.CreateExtractValue(
          value, 2, "aggregate.lambda.owns.environment");
      ::llvm::Value *owned_environment = builder.CreateSelect(
          owns_environment, environment,
          ::llvm::ConstantPointerNull::get(builder.getPtrTy()),
          "aggregate.lambda.owned.environment");
      builder.CreateCall(free_function, {owned_environment});
      return;
    }
    if (type.kind() == janus::TypeKind::Struct) {
      const ClassSpecialization &specialization =
          class_specializations_.at(std::string{type.name()});
      for (std::size_t index =
               specialization.declaration->constructor_fields.size();
           index-- > 0;) {
        const janus::Type &field_type = resolve(
            specialization.declaration->constructor_fields[index].declared_type,
            specialization.substitutions);
        if (owns_value(field_type))
          emit_owned_value_cleanup(builder.CreateExtractValue(
                                       value, index, "aggregate.struct.field"),
                                   field_type, builder);
      }
      return;
    }

    const EnumSpecialization &specialization =
        enum_specializations_.at(std::string{type.name()});
    ::llvm::Function *function = builder.GetInsertBlock()->getParent();
    auto *done = ::llvm::BasicBlock::Create(
        context_, "aggregate.enum.cleanup.done", function);
    ::llvm::Value *tag =
        builder.CreateExtractValue(value, 0, "aggregate.enum.tag");
    unsigned payload_index = 1;
    for (const janus::ast::EnumDeclaration::Case &enum_case :
         specialization.declaration->cases) {
      bool case_owns = false;
      for (const janus::ast::TypeReference &payload : enum_case.payload_types)
        case_owns = case_owns ||
                    owns_value(resolve(payload, specialization.substitutions));
      if (!case_owns) {
        for (const janus::ast::TypeReference &payload : enum_case.payload_types)
          if (resolve(payload, specialization.substitutions).kind() !=
              janus::TypeKind::Unit)
            ++payload_index;
        continue;
      }
      auto *release = ::llvm::BasicBlock::Create(
          context_, "aggregate.enum.release." + enum_case.name, function);
      auto *next = ::llvm::BasicBlock::Create(
          context_, "aggregate.enum.next." + enum_case.name, function);
      builder.CreateCondBr(
          builder.CreateICmpEQ(tag, builder.getInt32(enum_case.value),
                               "aggregate.enum.is." + enum_case.name),
          release, next);
      builder.SetInsertPoint(release);
      unsigned stored_payloads = 0;
      for (const janus::ast::TypeReference &payload : enum_case.payload_types)
        if (resolve(payload, specialization.substitutions).kind() !=
            janus::TypeKind::Unit)
          ++stored_payloads;
      unsigned stored_index = stored_payloads;
      for (std::size_t index = enum_case.payload_types.size(); index-- > 0;) {
        const janus::Type &payload_type = resolve(
            enum_case.payload_types[index], specialization.substitutions);
        if (payload_type.kind() != janus::TypeKind::Unit)
          --stored_index;
        if (owns_value(payload_type))
          emit_owned_value_cleanup(
              builder.CreateExtractValue(value, payload_index + stored_index,
                                         "aggregate.enum.payload"),
              payload_type, builder);
      }
      builder.CreateBr(done);
      builder.SetInsertPoint(next);
      payload_index += stored_payloads;
    }
    builder.CreateBr(done);
    builder.SetInsertPoint(done);
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
      emit_owned_value_cleanup(deleted_value, deleted_type, builder);
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
    std::size_t completed = 0;
    for (std::size_t index = active_cleanup_scopes_.size();
         index > retained_depth; --index) {
      const CleanupScope &scope = active_cleanup_scopes_[index - 1];
      const auto *actions = scope.actions;
      const auto *owned_values = scope.owned_values;
      auto *locals = scope.locals;
      const auto *substitutions = scope.substitutions;
      for (auto action = actions->rbegin(); action != actions->rend();
           ++action) {
        const TransientPanicCleanup remaining = push_transient_panic_cleanup(
            builder, retained_depth, completed + 1);
        const bool previous_emitting_inline_cleanup = emitting_inline_cleanup_;
        emitting_inline_cleanup_ = true;
        emit_cleanup_action(**action, *substitutions, *locals, builder);
        emitting_inline_cleanup_ = previous_emitting_inline_cleanup;
        pop_transient_panic_cleanup(remaining, builder);
        ++completed;
      }
      for (auto value = owned_values->rbegin(); value != owned_values->rend();
           ++value) {
        const TransientPanicCleanup remaining = push_transient_panic_cleanup(
            builder, retained_depth, completed + 1);
        const bool previous_emitting_inline_cleanup = emitting_inline_cleanup_;
        emitting_inline_cleanup_ = true;
        emit_owned_value_cleanup(value->first, *value->second, builder);
        emitting_inline_cleanup_ = previous_emitting_inline_cleanup;
        pop_transient_panic_cleanup(remaining, builder);
        ++completed;
      }
    }
  }

  void emit_active_cleanups(::llvm::IRBuilder<> &builder) {
    emit_cleanups_from_depth(builder, 0);
  }

  TransientPanicCleanup
  push_transient_panic_cleanup(::llvm::IRBuilder<> &builder,
                               std::size_t retained_depth = 0,
                               std::size_t skipped_units = 0) {
    if (emitting_panic_cleanup_ || emitting_inline_cleanup_ ||
        active_cleanup_scopes_.size() <= retained_depth)
      return {};
    bool has_cleanup = false;
    for (std::size_t index = retained_depth;
         index < active_cleanup_scopes_.size(); ++index) {
      const CleanupScope &scope = active_cleanup_scopes_[index];
      has_cleanup = has_cleanup || !scope.actions->empty() ||
                    !scope.owned_values->empty();
    }
    if (!has_cleanup)
      return {};

    struct CleanupUnit {
      std::size_t scope;
      bool action;
      std::size_t index;
    };
    std::vector<CleanupUnit> units;
    for (std::size_t scope_index = active_cleanup_scopes_.size();
         scope_index-- > retained_depth;) {
      const CleanupScope &scope = active_cleanup_scopes_[scope_index];
      for (std::size_t action_index = scope.actions->size();
           action_index-- > 0;)
        units.push_back({scope_index, true, action_index});
      for (std::size_t value_index = scope.owned_values->size();
           value_index-- > 0;)
        units.push_back({scope_index, false, value_index});
    }
    if (skipped_units >= units.size())
      return {};
    units.erase(units.begin(), units.begin() + skipped_units);

    std::vector<::llvm::Type *> context_fields;
    for (const CleanupScope &scope : active_cleanup_scopes_) {
      for (const auto &[name, local] : *scope.locals) {
        static_cast<void>(name);
        if (local.storage != nullptr)
          context_fields.push_back(builder.getPtrTy());
      }
      for (const auto &[value, type] : *scope.owned_values) {
        static_cast<void>(value);
        context_fields.push_back(lower_type(*type, context_));
      }
    }
    const std::size_t context_index = panic_cleanup_index_++;
    auto *context_type = ::llvm::StructType::create(context_, context_fields,
                                                    "panic.cleanup.context." +
                                                        std::to_string(
                                                            context_index));
    ::llvm::Value *context_storage =
        create_entry_alloca(builder, context_type, "panic.cleanup.context");
    unsigned field_index = 0;
    for (const CleanupScope &scope : active_cleanup_scopes_) {
      for (const auto &[name, local] : *scope.locals) {
        static_cast<void>(name);
        if (local.storage == nullptr)
          continue;
        builder.CreateStore(local.storage,
                            builder.CreateStructGEP(
                                context_type, context_storage, field_index++));
      }
      for (const auto &[value, type] : *scope.owned_values) {
        builder.CreateStore(value, builder.CreateStructGEP(context_type,
                                                           context_storage,
                                                           field_index++));
      }
    }

    auto *frame_type = ::llvm::StructType::get(
        context_, {builder.getPtrTy(), builder.getPtrTy(), builder.getPtrTy()});
    ::llvm::FunctionCallee push = module_->getOrInsertFunction(
        "janus_push_panic_cleanup",
        ::llvm::FunctionType::get(
            builder.getVoidTy(),
            {builder.getPtrTy(), builder.getPtrTy(), builder.getPtrTy()},
            false));
    auto *cleanup_type = ::llvm::FunctionType::get(builder.getVoidTy(),
                                                   {builder.getPtrTy()}, false);
    TransientPanicCleanup result;
    result.frames.reserve(units.size());
    for (auto unit = units.rbegin(); unit != units.rend(); ++unit) {
      const std::size_t cleanup_index = panic_cleanup_index_++;
      auto *cleanup = ::llvm::Function::Create(
          cleanup_type, ::llvm::Function::InternalLinkage,
          "__janus_panic_cleanup_" + std::to_string(cleanup_index), *module_);
      ::llvm::IRBuilder<> cleanup_builder{
          ::llvm::BasicBlock::Create(context_, "entry", cleanup)};
      ::llvm::Argument *context = cleanup->getArg(0);
      context->setName("context");

      std::vector<std::unordered_map<std::string, Local>> cleanup_locals;
      std::vector<std::vector<std::pair<::llvm::Value *, const janus::Type *>>>
          cleanup_owned_values;
      std::vector<CleanupScope> cleanup_scopes;
      cleanup_locals.reserve(active_cleanup_scopes_.size());
      cleanup_owned_values.reserve(active_cleanup_scopes_.size());
      cleanup_scopes.reserve(active_cleanup_scopes_.size());
      unsigned cleanup_field_index = 0;
      for (const CleanupScope &scope : active_cleanup_scopes_) {
        cleanup_locals.emplace_back();
        for (const auto &[name, local] : *scope.locals) {
          if (local.storage == nullptr) {
            cleanup_locals.back().emplace(name, local);
            continue;
          }
          ::llvm::Value *field = cleanup_builder.CreateStructGEP(
              context_type, context, cleanup_field_index++);
          ::llvm::Value *storage = cleanup_builder.CreateLoad(
              cleanup_builder.getPtrTy(), field, name + ".cleanup.storage");
          cleanup_locals.back().emplace(
              name, Local{storage, local.type, local.is_constant});
        }
        cleanup_owned_values.emplace_back();
        for (const auto &[value, type] : *scope.owned_values) {
          static_cast<void>(value);
          ::llvm::Value *field = cleanup_builder.CreateStructGEP(
              context_type, context, cleanup_field_index++);
          cleanup_owned_values.back().push_back(
              {cleanup_builder.CreateLoad(lower_type(*type, context_), field,
                                          "owned.cleanup.value"),
               type});
        }
        cleanup_scopes.push_back(
            CleanupScope{scope.actions, &cleanup_owned_values.back(),
                         &cleanup_locals.back(), scope.substitutions});
      }

      auto saved_scopes = std::move(active_cleanup_scopes_);
      active_cleanup_scopes_ = cleanup_scopes;
      emitting_panic_cleanup_ = true;
      const CleanupScope &scope = active_cleanup_scopes_[unit->scope];
      if (unit->action)
        emit_cleanup_action(*scope.actions->at(unit->index),
                            *scope.substitutions, *scope.locals,
                            cleanup_builder);
      else {
        const auto &[value, type] = scope.owned_values->at(unit->index);
        emit_owned_value_cleanup(value, *type, cleanup_builder);
      }
      emitting_panic_cleanup_ = false;
      active_cleanup_scopes_ = std::move(saved_scopes);
      cleanup_builder.CreateRetVoid();

      ::llvm::Value *frame =
          create_entry_alloca(builder, frame_type, "panic.cleanup.frame");
      builder.CreateCall(push, {frame, cleanup, context_storage});
      result.frames.push_back(frame);
    }
    return result;
  }

  void pop_transient_panic_cleanup(const TransientPanicCleanup &cleanup,
                                   ::llvm::IRBuilder<> &builder) {
    ::llvm::FunctionCallee pop = module_->getOrInsertFunction(
        "janus_pop_panic_cleanup",
        ::llvm::FunctionType::get(builder.getVoidTy(), {builder.getPtrTy()},
                                  false));
    for (auto frame = cleanup.frames.rbegin(); frame != cleanup.frames.rend();
         ++frame)
      builder.CreateCall(pop, {*frame});
  }

  ::llvm::CallInst *
  emit_protected_call(::llvm::FunctionCallee target,
                      ::llvm::ArrayRef<::llvm::Value *> arguments,
                      ::llvm::IRBuilder<> &builder,
                      const ::llvm::Twine &name = "") {
    const TransientPanicCleanup panic_cleanup =
        push_transient_panic_cleanup(builder);
    ::llvm::CallInst *call = builder.CreateCall(target, arguments, name);
    pop_transient_panic_cleanup(panic_cleanup, builder);
    return call;
  }

  ::llvm::CallInst *
  emit_protected_indirect_call(::llvm::FunctionType *type,
                               ::llvm::Value *target,
                               ::llvm::ArrayRef<::llvm::Value *> arguments,
                               ::llvm::IRBuilder<> &builder,
                               const ::llvm::Twine &name = "") {
    const TransientPanicCleanup panic_cleanup =
        push_transient_panic_cleanup(builder);
    ::llvm::CallInst *call = builder.CreateCall(type, target, arguments, name);
    pop_transient_panic_cleanup(panic_cleanup, builder);
    return call;
  }

  ::llvm::Function *emit_function(
      const janus::ast::FunctionDeclaration &function,
      const std::vector<const janus::Type *> &type_arguments,
      const janus::ast::ClassDeclaration *owner = nullptr,
      const Substitutions *owner_substitutions = nullptr,
      std::string_view owner_key = {},
      const std::vector<janus::ast::Statement> *body_override = nullptr,
      const janus::ast::ExtensionDeclaration *extension = nullptr,
      janus::ast::ParameterOwnership extension_receiver_ownership =
          janus::ast::ParameterOwnership::Unspecified) {
    std::string llvm_name;
    if (function.is_external) {
      llvm_name = function.external_symbol.value_or(function.name);
    } else {
      std::string extension_prefix;
      if (extension != nullptr) {
        extension_prefix = extension->module_name.value_or("entry");
        std::replace(extension_prefix.begin(), extension_prefix.end(), '.',
                     '_');
        extension_prefix +=
            "__extension__" + extension->target_type.name + "__";
      }
      llvm_name = (owner == nullptr ? extension_prefix
                                    : std::string{owner_key} + "__") +
                  mangle(function, type_arguments);
      if (owner == nullptr && extension == nullptr &&
          ambiguous_function_names_.contains(function.name) &&
          function.module_name.has_value()) {
        std::string module = *function.module_name;
        std::replace(module.begin(), module.end(), '.', '_');
        llvm_name = module + "__" + llvm_name;
      }
    }
    if (const auto iterator = emitted_.find(llvm_name);
        iterator != emitted_.end())
      return iterator->second;

    const auto previous_active_module = active_module_;
    const std::string previous_active_function = active_function_;
    active_module_ = owner != nullptr
                         ? owner->module_name
                         : (extension != nullptr ? extension->module_name
                                                 : function.module_name);

    Substitutions substitutions;
    if (owner_substitutions != nullptr)
      substitutions = *owner_substitutions;
    std::size_t type_argument_index = 0;
    if (extension != nullptr)
      for (const std::string &parameter : extension->type_parameters)
        add_type_parameter_substitution(
            substitutions, parameter,
            *type_arguments[type_argument_index++]);
    for (const std::string &parameter : function.type_parameters) {
      const janus::Type *argument = type_arguments[type_argument_index++];
      add_type_parameter_substitution(substitutions, parameter, *argument);
    }

    const janus::Type &return_type =
        resolve(function.return_type, substitutions);
    std::vector<::llvm::Type *> parameter_types;
    parameter_types.reserve(function.parameters.size() +
                            (owner == nullptr && extension == nullptr ? 0 : 1));
    const bool native_entry = owner == nullptr && extension == nullptr &&
                              function.name == "main" && !function.is_external;
    if (native_entry) {
      parameter_types.push_back(::llvm::Type::getInt32Ty(context_));
      parameter_types.push_back(::llvm::PointerType::getUnqual(context_));
    } else if (owner != nullptr) {
      parameter_types.push_back(::llvm::PointerType::getUnqual(context_));
    } else if (extension != nullptr) {
      const janus::Type &receiver_type =
          resolve(extension->target_type, substitutions);
      const bool indirect_borrow =
          extension_receiver_ownership ==
              janus::ast::ParameterOwnership::BorrowMutable ||
          (extension_receiver_ownership ==
               janus::ast::ParameterOwnership::Borrow &&
           (receiver_type.kind() == janus::TypeKind::Struct ||
            receiver_type.kind() == janus::TypeKind::Enum));
      parameter_types.push_back(indirect_borrow
                                    ? ::llvm::PointerType::getUnqual(context_)
                                    : lower_type(receiver_type, context_));
    }
    for (const auto &parameter : function.parameters) {
      const janus::Type &parameter_type =
          resolve(parameter.type, substitutions);
      const bool indirect_borrow =
          parameter.ownership ==
              janus::ast::ParameterOwnership::BorrowMutable ||
          (parameter.ownership == janus::ast::ParameterOwnership::Borrow &&
           (parameter_type.kind() == janus::TypeKind::Struct ||
            parameter_type.kind() == janus::TypeKind::Enum));
      parameter_types.push_back(indirect_borrow
                                    ? ::llvm::PointerType::getUnqual(context_)
                                    : lower_type(parameter_type, context_));
    }

    ::llvm::Type *llvm_return_type =
        function.return_ownership ==
                janus::ast::ReturnOwnership::BorrowMutable
            ? ::llvm::PointerType::getUnqual(context_)
            : lower_type(return_type, context_);
    auto *function_type = ::llvm::FunctionType::get(
        llvm_return_type, parameter_types, function.is_variadic);
    const ::llvm::GlobalValue::LinkageTypes linkage =
        !function.is_external &&
                (function.is_private || function.is_internal ||
                 (owner != nullptr && function.name == "destructor") ||
                 (extension != nullptr && extension->is_private))
            ? ::llvm::Function::InternalLinkage
            : ::llvm::Function::ExternalLinkage;
    auto *llvm_function =
        ::llvm::Function::Create(function_type, linkage, llvm_name, *module_);
    const std::optional<std::string> definition_module =
        owner != nullptr ? owner->module_name
                         : (extension != nullptr ? extension->module_name
                                                 : function.module_name);
    if (is_dependency(definition_module))
      llvm_function->addFnAttr("janus.module", *definition_module);
    if (!function.type_parameters.empty() ||
        (extension != nullptr && !extension->type_parameters.empty()) ||
        (owner != nullptr && !owner->type_parameters.empty()))
      llvm_function->addFnAttr("janus.consumer-owned");
    emitted_.emplace(llvm_name, llvm_function);
    if (function.is_external) {
      active_function_ = previous_active_function;
      active_module_ = previous_active_module;
      return llvm_function;
    }

    active_function_ =
        owner != nullptr
            ? owner->name + "." + function.name
            : (extension != nullptr
                   ? extension->target_type.name + "." + function.name
                   : function.name);
    const bool previous_emitting_panic_cleanup = emitting_panic_cleanup_;
    emitting_panic_cleanup_ = false;
    auto previous_cleanup_scopes = std::move(active_cleanup_scopes_);
    active_cleanup_scopes_.clear();

    auto *entry = ::llvm::BasicBlock::Create(context_, "entry", llvm_function);
    ::llvm::IRBuilder<> builder{entry};
    std::unordered_map<std::string, Local> locals;
    if (native_entry && panic_finalizer_ != nullptr) {
      ::llvm::FunctionCallee register_cleanup = module_->getOrInsertFunction(
          "janus_set_panic_cleanup",
          ::llvm::FunctionType::get(builder.getVoidTy(), {builder.getPtrTy()},
                                    false));
      builder.CreateCall(register_cleanup, {panic_finalizer_});
    }
    auto argument_iterator = llvm_function->arg_begin();
    if (native_entry) {
      ::llvm::Argument &argc = *argument_iterator++;
      ::llvm::Argument &argv = *argument_iterator++;
      argc.setName("argc");
      argv.setName("argv");
      ::llvm::FunctionCallee initialize = module_->getOrInsertFunction(
          "janus_process_initialize",
          ::llvm::FunctionType::get(builder.getVoidTy(),
                                    {builder.getInt32Ty(), builder.getPtrTy()},
                                    false));
      builder.CreateCall(initialize, {&argc, &argv});
    }
    if (native_entry)
      for (::llvm::Function *initializer : global_initializers_)
        builder.CreateCall(initializer);

    if (owner != nullptr) {
      ::llvm::Argument &this_argument = *argument_iterator++;
      this_argument.setName("this");
      const janus::Type &owner_type = class_types_.at(std::string{owner_key});
      ::llvm::Value *this_storage =
          create_entry_alloca(builder, builder.getPtrTy(), "this.addr");
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
    if (extension != nullptr) {
      ::llvm::Argument &this_argument = *argument_iterator++;
      this_argument.setName("this");
      const janus::Type &receiver_type =
          resolve(extension->target_type, substitutions);
      const bool indirect_borrow =
          extension_receiver_ownership ==
              janus::ast::ParameterOwnership::BorrowMutable ||
          (extension_receiver_ownership ==
               janus::ast::ParameterOwnership::Borrow &&
           (receiver_type.kind() == janus::TypeKind::Struct ||
            receiver_type.kind() == janus::TypeKind::Enum));
      if (indirect_borrow) {
        locals.emplace("this", Local{&this_argument, &receiver_type});
      } else {
        ::llvm::Value *this_storage = create_entry_alloca(
            builder, lower_type(receiver_type, context_), "this.addr");
        builder.CreateStore(&this_argument, this_storage);
        locals.emplace("this", Local{this_storage, &receiver_type});
      }
    }

    std::size_t parameter_index = 0;
    for (; argument_iterator != llvm_function->arg_end(); ++argument_iterator) {
      ::llvm::Argument &argument = *argument_iterator;
      const auto &parameter = function.parameters[parameter_index++];
      const janus::Type &type = resolve(parameter.type, substitutions);
      argument.setName(parameter.name);
      if (parameter.ownership ==
              janus::ast::ParameterOwnership::BorrowMutable ||
          (parameter.ownership == janus::ast::ParameterOwnership::Borrow &&
           (type.kind() == janus::TypeKind::Struct ||
            type.kind() == janus::TypeKind::Enum))) {
        locals.emplace(parameter.name, Local{&argument, &type});
        continue;
      }
      ::llvm::Value *storage = create_entry_alloca(
          builder, lower_type(type, context_), parameter.name);
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
      std::vector<std::pair<::llvm::Value *, const janus::Type *>> owned_values;
      active_cleanup_scopes_.push_back(CleanupScope{
          &deferred_actions, &owned_values, &block_locals, &substitutions});
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
          loop_targets.push_back(LoopTarget{exit_block, condition_block,
                                            active_cleanup_scopes_.size()});
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
            const bool consumes_source =
                std::holds_alternative<janus::ast::MoveExpression>(
                    (*loop)->iterator.value);
            for (const janus::ast::FunctionDeclaration &method :
               source_specialization.declaration->methods)
              if (method.name ==
                  (consumes_source ? "intoIterator" : "iterator"))
                iterator_method = &method;
            ::llvm::Function *iterator_function = emit_function(
                *iterator_method, {}, source_specialization.declaration,
                &source_specialization.substitutions, source_type.name());
            iterator_type = &resolve(iterator_method->return_type,
                                     source_specialization.substitutions);
            iterator = emit_protected_call(iterator_function, {source}, builder,
                                           "for.iterator");
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
          std::vector<const janus::ast::DeferStatement *>
              iterator_deferred_actions;
          std::vector<std::pair<::llvm::Value *, const janus::Type *>>
              iterator_owned_values{{iterator, iterator_type}};
          active_cleanup_scopes_.push_back(
              CleanupScope{&iterator_deferred_actions, &iterator_owned_values,
                           &block_locals, &substitutions});

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
          ::llvm::Value *next = emit_protected_call(
              next_function, {iterator}, builder, "for.option");
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
          ::llvm::Value *storage = create_entry_alloca(
              builder, lower_type(element_type, context_), (*loop)->binding);
          builder.CreateStore(item, storage);
          auto body_locals = block_locals;
          body_locals.insert_or_assign((*loop)->binding,
                                       Local{storage, &element_type});
          loop_targets.push_back(LoopTarget{exit_block, condition_block,
                                            active_cleanup_scopes_.size()});
          const bool body_returns = emit_block((*loop)->body, body_locals);
          loop_targets.pop_back();
          if (!body_returns)
            builder.CreateBr(condition_block);

          active_cleanup_scopes_.pop_back();
          builder.SetInsertPoint(exit_block);
          emit_owned_value_cleanup(iterator, *iterator_type, builder);
          continue;
        }

        if (const auto *declaration =
                std::get_if<janus::ast::ValueDeclaration>(&statement)) {
          const janus::Type &type =
              declaration->declared_type
                  ? resolve(*declaration->declared_type, substitutions)
                  : resolve(analysis_.local_types.at(declaration));
          if (declaration->is_constant) {
            const janus::constant::Value &value =
                analysis_.local_constant_values.at(declaration);
            block_locals.emplace(
                declaration->name,
                Local{emit_static_initializer(value, type), &type, true});
            continue;
          }
          const auto *borrowed_source =
              declaration->initializer.has_value()
                  ? std::get_if<janus::ast::IdentifierExpression>(
                        &declaration->initializer->value)
                  : nullptr;
          if (declaration->is_borrowed && borrowed_source != nullptr) {
            const Local &source =
                resolve_storage(borrowed_source->name, block_locals);
            block_locals.emplace(declaration->name,
                                 Local{source.storage, &type});
            continue;
          }
          const auto resolved_initializer =
              declaration->initializer.has_value()
                  ? analysis_.call_return_ownership.find(
                        &*declaration->initializer)
                  : analysis_.call_return_ownership.end();
          const bool mutable_borrow_call =
              declaration->is_borrowed &&
              resolved_initializer != analysis_.call_return_ownership.end() &&
              resolved_initializer->second ==
                  janus::ast::ReturnOwnership::BorrowMutable;
          if (mutable_borrow_call) {
            ::llvm::Value *storage = emit_expression(
                *declaration->initializer, type, substitutions, block_locals,
                builder);
            block_locals.emplace(declaration->name, Local{storage, &type});
            continue;
          }
          if (type.kind() == janus::TypeKind::Unit) {
            if (declaration->initializer.has_value())
              static_cast<void>(emit_expression(
                  *declaration->initializer, type, substitutions,
                  block_locals, builder));
            block_locals.emplace(declaration->name,
                                 Local{nullptr, &type});
            continue;
          }
          ::llvm::Value *storage = create_entry_alloca(
              builder, lower_type(type, context_), declaration->name);
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
          if (assignment->index_target != nullptr) {
            const janus::ast::IndexExpression &place =
                *assignment->index_target;
            const janus::Type &object_type = expression_type(
                *place.container, substitutions, block_locals);
            ::llvm::Value *object_value = emit_expression(
                *place.container, object_type, substitutions, block_locals,
                builder);
            if (const auto ownership =
                    analysis_.call_return_ownership.find(place.container.get());
                ownership != analysis_.call_return_ownership.end() &&
                ownership->second ==
                    janus::ast::ReturnOwnership::BorrowMutable)
              object_value = builder.CreateLoad(builder.getPtrTy(), object_value,
                                                "index.mutable.object");
            ::llvm::Value *index_value = emit_expression(
                *place.index, janus::Type::usize_type(), substitutions,
                block_locals, builder);
            const auto &capabilities =
                analysis_.indexed_capabilities.at(&place);
            const janus::Type &element_type =
                resolve(capabilities.element_type);
            const ClassSpecialization &specialization =
                class_specializations_.at(std::string{object_type.name()});
            ::llvm::Value *replacement = nullptr;
            if (assignment->operation ==
                janus::ast::AssignmentOperator::Assign) {
              replacement = emit_expression(
                  assignment->expression, element_type, substitutions,
                  block_locals, builder);
            } else {
              ::llvm::Function *get_target = emit_function(
                  *capabilities.read, {}, specialization.declaration,
                  &specialization.substitutions, object_type.name());
              ::llvm::Value *left = emit_protected_call(
                  get_target, {object_value, index_value}, builder,
                  "index.old");
              const janus::ast::BinaryOperator operation =
                  *janus::ast::assignment_binary_operator(
                      assignment->operation);
              const bool is_shift =
                  operation == janus::ast::BinaryOperator::ShiftLeft ||
                  operation == janus::ast::BinaryOperator::ShiftRight;
              ::llvm::Value *right = emit_expression(
                  assignment->expression,
                  is_shift ? janus::Type::usize_type() : element_type,
                  substitutions, block_locals, builder);
              replacement = emit_controlled_binary_operation(
                  operation, left, right, element_type, assignment->location,
                  builder);
            }
            ::llvm::Function *set_target = emit_function(
                *capabilities.replace, {}, specialization.declaration,
                &specialization.substitutions, object_type.name());
            emit_protected_call(
                set_target, {object_value, index_value, replacement}, builder);
            continue;
          }
          const auto emit_assignment = [&](::llvm::Value *storage,
                                           const janus::Type &type) {
            if (assignment->operation ==
                janus::ast::AssignmentOperator::Assign) {
              builder.CreateStore(
                  emit_expression(assignment->expression, type, substitutions,
                                  block_locals, builder),
                  storage);
              return;
            }
            ::llvm::Value *left = builder.CreateLoad(
                lower_type(type, context_), storage, assignment->name + ".value");
            const janus::ast::BinaryOperator binary_operation =
                *janus::ast::assignment_binary_operator(
                    assignment->operation);
            const bool is_shift =
                binary_operation == janus::ast::BinaryOperator::ShiftLeft ||
                binary_operation == janus::ast::BinaryOperator::ShiftRight;
            ::llvm::Value *right = emit_expression(
                assignment->expression,
                is_shift ? janus::Type::usize_type() : type, substitutions,
                block_locals, builder);
            ::llvm::Value *value = emit_controlled_binary_operation(
                binary_operation, left, right, type, assignment->location,
                builder);
            builder.CreateStore(value, storage);
          };
          if (!assignment->object.empty()) {
            if (analysis_.qualified_global_writes.contains(assignment)) {
              const Local &global = resolve_qualified_global(*assignment);
              emit_assignment(global.storage, *global.type);
              continue;
            }
            const Local &object = block_locals.at(assignment->object);
            ::llvm::Value *object_pointer =
                object.type->kind() == janus::TypeKind::Struct
                    ? object.storage
                    : builder.CreateLoad(
                          ::llvm::PointerType::getUnqual(context_),
                          object.storage, assignment->object + ".object");
            const auto [field_index, field_type] =
                find_field(object.type->name(), assignment->name);
            ::llvm::Value *field_pointer = builder.CreateStructGEP(
                llvm_class_types_.at(std::string{object.type->name()}),
                object_pointer, field_index);
            emit_assignment(field_pointer, *field_type);
            continue;
          }
          const Local &local = resolve_storage(assignment->name, block_locals);
          emit_assignment(local.storage, *local.type);
          continue;
        }

        if (const auto *deletion =
                std::get_if<janus::ast::DeleteStatement>(&statement)) {
          const janus::Type &deleted_type = expression_type(
              deletion->expression, substitutions, block_locals);
          ::llvm::Value *deleted_value =
              emit_expression(deletion->expression, deleted_type, substitutions,
                              block_locals, builder);
          emit_owned_value_cleanup(deleted_value, deleted_type, builder);
          continue;
        }

        if (const auto *expression_statement =
                std::get_if<janus::ast::ExpressionStatement>(&statement)) {
          const janus::Type &type = expression_type(
              expression_statement->expression, substitutions, block_locals);
          static_cast<void>(emit_expression(expression_statement->expression,
                                            type, substitutions, block_locals,
                                            builder));
          if (const auto *call = std::get_if<janus::ast::CallExpression>(
                  &expression_statement->expression.value);
              call != nullptr && call->callee == "panic") {
            builder.CreateUnreachable();
            active_cleanup_scopes_.pop_back();
            return true;
          }
          continue;
        }

        if (const auto *deferred =
                std::get_if<janus::ast::DeferStatement>(&statement)) {
          deferred_actions.push_back(deferred);
          continue;
        }

        if (std::holds_alternative<janus::ast::BreakStatement>(statement)) {
          emit_cleanups_from_depth(builder, loop_targets.back().cleanup_depth);
          builder.CreateBr(loop_targets.back().break_block);
          active_cleanup_scopes_.pop_back();
          return true;
        }

        if (std::holds_alternative<janus::ast::ContinueStatement>(statement)) {
          emit_cleanups_from_depth(builder, loop_targets.back().cleanup_depth);
          builder.CreateBr(loop_targets.back().continue_block);
          active_cleanup_scopes_.pop_back();
          return true;
        }

        const auto &return_statement =
            std::get<janus::ast::ReturnStatement>(statement);
        ::llvm::Value *return_value = nullptr;
        if (return_statement.expression.has_value()) {
          if (function.return_ownership ==
              janus::ast::ReturnOwnership::BorrowMutable)
            return_value = emit_borrow_storage(
                *return_statement.expression, return_type, substitutions,
                block_locals, builder);
          if (return_value == nullptr)
            return_value =
                emit_expression(*return_statement.expression, return_type,
                                substitutions, block_locals, builder);
        }
        emit_active_cleanups(builder);
        if (owner == nullptr && function.name == "main")
          for (auto finalizer = global_finalizers_.rbegin();
               finalizer != global_finalizers_.rend(); ++finalizer)
            builder.CreateCall(*finalizer);
        const bool emitted_musttail = mark_tail_call_if_eligible(
            return_value, *llvm_function, builder);
        if (return_statement.expression.has_value() &&
            analysis_.tailrec_edges.contains(&*return_statement.expression) &&
            !emitted_musttail)
          throw janus::CompileError{
              janus::DiagnosticCode::AnalyzerIncompatibleTailrec,
              return_statement.location,
              "tailrec backend invariant failed: recursive edge could not "
              "be emitted as musttail"};
        if (return_type.kind() != janus::TypeKind::Unit &&
            return_value != nullptr)
          builder.CreateRet(return_value);
        else
          builder.CreateRetVoid();
        active_cleanup_scopes_.pop_back();
        return true;
      }
      emit_cleanups_from_depth(builder, active_cleanup_scopes_.size() - 1);
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
    emitting_panic_cleanup_ = previous_emitting_panic_cleanup;
    active_function_ = previous_active_function;
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
        std::nullopt,
        false,
        {},
        janus::ast::ReturnOwnership::Unspecified,
        false,
        false,
        false,
        {},
        {},
        0};
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
    std::function<void(const std::vector<janus::ast::Statement> &,
                       std::unordered_set<std::string>)>
        visit_block;
    visit_block = [&](const std::vector<janus::ast::Statement> &statements,
                      std::unordered_set<std::string> active_bound) {
      for (const auto &statement : statements) {
        if (const auto *declaration =
                std::get_if<janus::ast::ValueDeclaration>(&statement)) {
          if (declaration->initializer)
            visit(*declaration->initializer, active_bound);
          active_bound.insert(declaration->name);
        } else if (const auto *assignment =
                       std::get_if<janus::ast::AssignmentStatement>(&statement)) {
          if (assignment->index_target != nullptr) {
            visit(*assignment->index_target->container, active_bound);
            visit(*assignment->index_target->index, active_bound);
          }
          if (!assignment->object.empty() &&
              !active_bound.contains(assignment->object) &&
              available.contains(assignment->object) &&
              captured.insert(assignment->object).second)
            captures.push_back(assignment->object);
          if (assignment->object.empty() &&
              !active_bound.contains(assignment->name) &&
              available.contains(assignment->name) &&
              captured.insert(assignment->name).second)
            captures.push_back(assignment->name);
          visit(assignment->expression, active_bound);
        } else if (const auto *deletion =
                       std::get_if<janus::ast::DeleteStatement>(&statement)) {
          visit(deletion->expression, active_bound);
        } else if (const auto *returned =
                       std::get_if<janus::ast::ReturnStatement>(&statement)) {
          if (returned->expression)
            visit(*returned->expression, active_bound);
        } else if (const auto *expression =
                       std::get_if<janus::ast::ExpressionStatement>(&statement)) {
          visit(expression->expression, active_bound);
        } else if (const auto *deferred =
                       std::get_if<janus::ast::DeferStatement>(&statement)) {
          std::visit([&](const auto &action) { visit(action.expression, active_bound); },
                     deferred->action);
        } else if (const auto *conditional = std::get_if<
                       std::shared_ptr<janus::ast::IfStatement>>(&statement)) {
          visit((*conditional)->condition, active_bound);
          visit_block((*conditional)->then_body, active_bound);
          visit_block((*conditional)->else_body, active_bound);
        } else if (const auto *loop = std::get_if<
                       std::shared_ptr<janus::ast::WhileStatement>>(&statement)) {
          visit((*loop)->condition, active_bound);
          visit_block((*loop)->body, active_bound);
        } else if (const auto *loop = std::get_if<
                       std::shared_ptr<janus::ast::ForStatement>>(&statement)) {
          visit((*loop)->iterator, active_bound);
          auto loop_bound = active_bound;
          loop_bound.insert((*loop)->binding);
          visit_block((*loop)->body, std::move(loop_bound));
        }
      }
    };
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
            } else if constexpr (std::is_same_v<
                                     Node,
                                     janus::ast::ArrayLiteralExpression>) {
              for (const auto &element : node.elements)
                visit(*element, active_bound);
            } else if constexpr (std::is_same_v<Node,
                                                janus::ast::LambdaExpression>) {
              auto nested_bound = active_bound;
              for (const auto &parameter : node.parameters)
                nested_bound.insert(parameter.name);
              if (const auto *body = std::get_if<
                      std::unique_ptr<janus::ast::Expression>>(&node.body))
                visit(**body, nested_bound);
              else
                visit_block(std::get<std::shared_ptr<janus::ast::LambdaBlock>>(
                                node.body)->statements,
                            std::move(nested_bound));
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
            } else if constexpr (std::is_same_v<
                                     Node, janus::ast::IndexExpression>) {
              visit(*node.container, active_bound);
              visit(*node.index, active_bound);
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
                const auto bindings =
                    janus::ast::match_pattern_binding_names(arm);
                arm_bound.insert(bindings.begin(), bindings.end());
                if (!janus::ast::is_enum_binding_pattern(arm))
                  for (const auto &pattern : arm.patterns)
                    janus::ast::visit_match_pattern(
                        pattern, [&](const auto &part) {
                          if (part.literal)
                            visit(*part.literal, active_bound);
                        });
                if (arm.guard)
                  visit(*arm.guard, arm_bound);
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
    if (const auto *body =
            std::get_if<std::unique_ptr<janus::ast::Expression>>(&lambda.body))
      visit(**body, bound);
    else
      visit_block(std::get<std::shared_ptr<janus::ast::LambdaBlock>>(lambda.body)
                      ->statements,
                  bound);
    return captures;
  }

  ::llvm::Value *
  emit_lambda(const janus::ast::LambdaExpression &lambda,
              const janus::Type &function_type,
              const Substitutions &substitutions,
              const std::unordered_map<std::string, Local> &locals,
              ::llvm::IRBuilder<> &builder,
              bool stack_allocate_environment = false) {
    const std::optional<std::string> lexical_module = active_module_;
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
      if (stack_allocate_environment) {
        environment = create_entry_alloca(builder, environment_type,
                                          "lambda.environment.scoped");
      } else {
        ::llvm::FunctionCallee malloc_function = module_->getOrInsertFunction(
            "janus_alloc",
            ::llvm::FunctionType::get(builder.getPtrTy(),
                                      {builder.getInt64Ty()}, false));
        environment = builder.CreateCall(
            malloc_function,
            {::llvm::ConstantExpr::getSizeOf(environment_type)},
            "lambda.environment");
      }
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
    for (std::size_t index = 0; index < signature.parameters.size(); ++index) {
      const janus::Type *parameter = signature.parameters[index];
      const janus::ast::ParameterOwnership ownership =
          signature.parameter_ownership[index];
      const bool indirect_borrow =
          ownership == janus::ast::ParameterOwnership::BorrowMutable ||
          (ownership == janus::ast::ParameterOwnership::Borrow &&
           (parameter->kind() == janus::TypeKind::Struct ||
            parameter->kind() == janus::TypeKind::Enum));
      parameter_types.push_back(indirect_borrow
                                    ? builder.getPtrTy()
                                    : lower_type(*parameter, context_));
    }
    ::llvm::Type *lambda_return_type =
        signature.return_ownership ==
                janus::ast::ReturnOwnership::BorrowMutable
            ? builder.getPtrTy()
            : lower_type(*signature.return_type, context_);
    auto *llvm_function_type = ::llvm::FunctionType::get(
        lambda_return_type, parameter_types, false);
    ::llvm::Function *lambda_function = ::llvm::Function::Create(
        llvm_function_type, ::llvm::Function::InternalLinkage,
        "__janus_lambda_" + std::to_string(lambda_index), *module_);
    if (is_dependency(lexical_module))
      lambda_function->addFnAttr("janus.module", *lexical_module);
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
      const janus::ast::ParameterOwnership ownership =
          signature.parameter_ownership[index];
      const bool indirect_borrow =
          ownership == janus::ast::ParameterOwnership::BorrowMutable ||
          (ownership == janus::ast::ParameterOwnership::Borrow &&
           (parameter_type->kind() == janus::TypeKind::Struct ||
            parameter_type->kind() == janus::TypeKind::Enum));
      if (indirect_borrow) {
        lambda_locals.emplace(lambda.parameters[index].name,
                              Local{&parameter, parameter_type});
        continue;
      }
      ::llvm::Value *storage = create_entry_alloca(
          lambda_builder, lower_type(*parameter_type, context_),
          lambda.parameters[index].name);
      lambda_builder.CreateStore(&parameter, storage);
      lambda_locals.emplace(lambda.parameters[index].name,
                            Local{storage, parameter_type});
    }

    if (const auto *expression_body = std::get_if<
            std::unique_ptr<janus::ast::Expression>>(&lambda.body)) {
      const janus::Type *previous_return_type = active_return_type_;
      auto previous_cleanup_scopes = std::move(active_cleanup_scopes_);
      active_cleanup_scopes_.clear();
      active_return_type_ = signature.return_type;
      ::llvm::Value *result = nullptr;
      if (signature.return_ownership ==
          janus::ast::ReturnOwnership::BorrowMutable)
        result = emit_borrow_storage(
            **expression_body, *signature.return_type, substitutions,
            lambda_locals, lambda_builder);
      if (result == nullptr)
        result = emit_expression(**expression_body, *signature.return_type,
                                 substitutions, lambda_locals,
                                 lambda_builder);
      active_return_type_ = previous_return_type;
      active_cleanup_scopes_ = std::move(previous_cleanup_scopes);
      if (signature.return_type->kind() == janus::TypeKind::Unit)
        lambda_builder.CreateRetVoid();
      else
        lambda_builder.CreateRet(result);
    } else {
      const auto &block =
          *std::get<std::shared_ptr<janus::ast::LambdaBlock>>(lambda.body);
      janus::ast::FunctionDeclaration body_function{
          "__janus_lambda_body_" + std::to_string(lambda_index),
          {}, {}, janus::ast::TypeReference{"Unit", lambda.location, {}}, {},
          lambda.location, true, false, {}, false, std::nullopt, false,
          std::nullopt, false, {}, janus::ast::ReturnOwnership::Unspecified,
          false, false, false, {}, {}, 0};
      body_function.module_name = lexical_module;
      std::vector<const janus::Type *> body_types;
      auto add_parameter = [&](std::string name, const janus::Type *type,
                               janus::ast::ParameterOwnership ownership) {
        const std::string type_name =
            "__LambdaType" + std::to_string(body_types.size());
        body_function.type_parameters.push_back(type_name);
        body_function.parameters.push_back(
            janus::ast::FunctionDeclaration::Parameter{
                std::move(name),
                janus::ast::TypeReference{type_name, lambda.location, {}},
                lambda.location, ownership});
        body_types.push_back(type);
      };
      for (const std::string &name : capture_names)
        add_parameter(name, locals.at(name).type,
                      janus::ast::ParameterOwnership::BorrowMutable);
      for (std::size_t index = 0; index < lambda.parameters.size(); ++index)
        add_parameter(lambda.parameters[index].name, signature.parameters[index],
                      signature.parameter_ownership[index]);
      const std::string return_name =
          "__LambdaType" + std::to_string(body_types.size());
      body_function.type_parameters.push_back(return_name);
      body_function.return_type =
          janus::ast::TypeReference{return_name, lambda.location, {}};
      body_function.return_ownership = signature.return_ownership;
      body_types.push_back(signature.return_type);
      ::llvm::Function *lowered_body =
          emit_function(body_function, body_types, nullptr, nullptr, {},
                        &block.statements);
      std::vector<::llvm::Value *> arguments;
      arguments.reserve(capture_names.size() + lambda.parameters.size());
      for (const std::string &name : capture_names)
        arguments.push_back(lambda_locals.at(name).storage);
      for (std::size_t index = 0; index < lambda.parameters.size(); ++index) {
        const auto &parameter = lambda.parameters[index];
        const janus::Type &parameter_type = *signature.parameters[index];
        const auto ownership = signature.parameter_ownership[index];
        const bool indirect_borrow =
            ownership == janus::ast::ParameterOwnership::BorrowMutable ||
            (ownership == janus::ast::ParameterOwnership::Borrow &&
             (parameter_type.kind() == janus::TypeKind::Struct ||
              parameter_type.kind() == janus::TypeKind::Enum));
        if (indirect_borrow) {
          arguments.push_back(lambda_locals.at(parameter.name).storage);
        } else {
          arguments.push_back(lambda_builder.CreateLoad(
              lower_type(parameter_type, context_),
              lambda_locals.at(parameter.name).storage,
              parameter.name + ".body.arg"));
        }
      }
      if (signature.return_type->kind() == janus::TypeKind::Unit) {
        lambda_builder.CreateCall(lowered_body, arguments);
        lambda_builder.CreateRetVoid();
      } else {
        lambda_builder.CreateRet(lambda_builder.CreateCall(lowered_body, arguments));
      }
    }

    auto *closure_type =
        ::llvm::cast<::llvm::StructType>(lower_type(function_type, context_));
    ::llvm::Value *closure = ::llvm::UndefValue::get(closure_type);
    closure =
        builder.CreateInsertValue(closure, lambda_function, 0, "lambda.code");
    closure = builder.CreateInsertValue(closure, environment, 1,
                                        "lambda.environment");
    return builder.CreateInsertValue(
        closure, builder.getInt1(!capture_names.empty() &&
                                 !stack_allocate_environment),
        2, "lambda.value");
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
            return node.is_float ? janus::Type::float_type()
                                 : janus::Type::double_type();
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
                                   Node, janus::ast::ArrayLiteralExpression>) {
            const auto inferred =
                analysis_.inferred_generic_arguments.find(&expression);
            std::vector<const janus::Type *> arguments;
            arguments.push_back(
                &resolve(inferred->second.front(), substitutions));
            const auto declaration =
                find_type_in_active_module(classes_, "Array");
            return ensure_class(declaration->first, arguments);
          } else if constexpr (std::is_same_v<
                                   Node, janus::ast::IdentifierExpression>) {
            std::string key = source_global_key(active_module_, node.name);
            if (!global_by_key_.contains(key)) {
              if (const auto exported = public_global_keys_.find(node.name);
                  exported != public_global_keys_.end())
                key = exported->second;
            }
            if (const auto global = global_by_key_.find(key);
                global != global_by_key_.end() &&
                global->second->declaration.is_constant)
              return resolve(global->second->declaration.declared_type,
                             substitutions);
            return *resolve_storage(node.name, locals).type;
          } else if constexpr (std::is_same_v<Node,
                                              janus::ast::LambdaExpression>) {
            std::unordered_map<std::string, Local> lambda_locals = locals;
            std::vector<const janus::Type *> parameters;
            std::vector<janus::ast::ParameterOwnership> ownerships;
            parameters.reserve(node.parameters.size());
            ownerships.reserve(node.parameters.size());
            for (const auto &parameter : node.parameters) {
              const janus::Type &type = resolve(parameter.type, substitutions);
              parameters.push_back(&type);
              ownerships.push_back(parameter.ownership);
              lambda_locals.insert_or_assign(parameter.name,
                                             Local{nullptr, &type});
            }
            const janus::Type &return_type =
                resolve(analysis_.inferred_generic_arguments.at(&expression)
                            .back(),
                        substitutions);
            return ensure_function_type(parameters, return_type,
                                        std::move(ownerships));
          } else if constexpr (std::is_same_v<Node,
                                              janus::ast::CallExpression>) {
            if (const Local *callable = find_storage(node.callee, locals);
                callable != nullptr &&
                callable->type->kind() == janus::TypeKind::Function)
              return *function_signature(*callable->type).return_type;
            if (node.callee == "panic" || node.callee == "print" ||
                node.callee == "println" || node.callee == "debug")
              return janus::Type::unit_type();
            if (node.callee == "__derivedHash")
              return janus::Type::usize_type();
            if (node.callee == "__derivedEquals")
              return janus::Type::bool_type();
            if (node.callee == "checkedCast") {
              const janus::Type &destination =
                  resolve(node.type_arguments.front(), substitutions);
              const auto error_declaration =
                  find_type_in_active_module(enums_, "NumericCastError");
              const janus::Type &error =
                  ensure_enum(error_declaration->first, {});
              const auto result_declaration =
                  find_type_in_active_module(enums_, "Result");
              return ensure_enum(result_declaration->first,
                                 {&destination, &error});
            }
            if (node.callee == "cstr")
              return ensure_pointer(janus::Type::byte_type());
            if (node.callee == "stringData")
              return ensure_pointer(janus::Type::byte_type());
            if (node.callee == "stringLength")
              return janus::Type::usize_type();
            if (node.callee == "stringView")
              return janus::Type::string_type();
            if (node.callee == "alloc" || node.callee == "realloc" ||
                node.callee == "reallocPreserving" || node.callee == "null") {
              const janus::Type &element =
                  resolve(node.type_arguments.front(), substitutions);
              return ensure_pointer(element);
            }
            if (node.callee == "sizeof" || node.callee == "alignof")
              return janus::Type::usize_type();
            if (node.callee == "free" || node.callee == "freeStorage")
              return janus::Type::unit_type();
            if (node.callee == "adoptReallocation") {
              const janus::Type &element =
                  resolve(node.type_arguments.front(), substitutions);
              return ensure_pointer(element);
            }
            if (node.callee == "owningCapture")
              return expression_type(*node.arguments[1], substitutions, locals);
            if (is_explicit_cast(node))
              return cast_destination(node, substitutions);
            const auto &callee =
                *find_in_active_module(functions_, node.callee)->second;
            Substitutions callee_substitutions;
            const std::vector<const janus::Type *> type_arguments =
                effective_type_arguments(
                    callee.type_parameters, node.type_arguments, &expression,
                    substitutions, node.location, node.callee);
            for (std::size_t index = 0; index < type_arguments.size(); ++index) {
              add_type_parameter_substitution(
                  callee_substitutions, callee.type_parameters[index],
                  *type_arguments[index]);
            }
            return resolve(callee.return_type, callee_substitutions);
          } else if constexpr (std::is_same_v<Node,
                                              janus::ast::NewExpression>) {
            std::vector<const janus::Type *> type_arguments;
            if (node.type_arguments.empty()) {
              if (const auto inferred =
                      analysis_.inferred_generic_arguments.find(&expression);
                  inferred != analysis_.inferred_generic_arguments.end())
                for (const janus::semantic::SemanticType &argument :
                     inferred->second)
                  type_arguments.push_back(&resolve(argument, substitutions));
            } else {
              for (const janus::ast::TypeReference &argument :
                   node.type_arguments)
                type_arguments.push_back(&resolve(argument, substitutions));
            }
            const auto declaration =
                find_type_in_active_module(classes_, node.class_name);
            return ensure_class(declaration->first, type_arguments);
          } else if constexpr (std::is_same_v<
                                   Node, janus::ast::MemberAccessExpression>) {
            if (const auto *identifier =
                    std::get_if<janus::ast::IdentifierExpression>(
                        &node.object->value);
                identifier != nullptr) {
              const auto declaration =
                  find_type_in_active_module(enums_, identifier->name);
              if (declaration != enums_.end())
                return ensure_enum(declaration->first, {});
            }
            if (analysis_.qualified_global_reads.contains(&node))
              return *resolve_qualified_global(node).type;
            const janus::Type &object_type =
                expression_type(*node.object, substitutions, locals);
            return *find_field(object_type.name(), node.member).second;
          } else if constexpr (std::is_same_v<
                                   Node, janus::ast::MethodCallExpression>) {
            if (const auto module = qualified_expression_name(*node.object);
                module.has_value() &&
                !locals.contains(module->substr(0, module->find('.')))) {
              const std::string qualified = *module + "." + node.method;
              if (const auto function =
                      find_in_active_module(functions_, qualified);
                  function != functions_.end()) {
                const auto &callee = *function->second;
                Substitutions callee_substitutions;
                const std::vector<const janus::Type *> type_arguments =
                    effective_type_arguments(
                        callee.type_parameters, node.type_arguments,
                        &expression, substitutions, node.location, qualified);
                for (std::size_t index = 0; index < type_arguments.size();
                     ++index)
                  add_type_parameter_substitution(
                      callee_substitutions, callee.type_parameters[index],
                      *type_arguments[index]);
                return resolve(callee.return_type, callee_substitutions);
              }
            }
            if (const auto *identifier =
                    std::get_if<janus::ast::IdentifierExpression>(
                        &node.object->value);
                identifier != nullptr) {
              const auto declaration =
                  find_type_in_active_module(enums_, identifier->name);
              if (declaration != enums_.end()) {
                std::vector<const janus::Type *> type_arguments;
                if (node.type_arguments.empty()) {
                  if (const auto inferred =
                          analysis_.inferred_generic_arguments.find(
                              &expression);
                      inferred != analysis_.inferred_generic_arguments.end())
                    for (const janus::semantic::SemanticType &argument :
                         inferred->second)
                      type_arguments.push_back(
                          &resolve(argument, substitutions));
                } else {
                  for (const janus::ast::TypeReference &argument :
                       node.type_arguments)
                    type_arguments.push_back(&resolve(argument, substitutions));
                }
                return ensure_enum(declaration->first, type_arguments);
              }
            }
            if (const auto extension =
                    analysis_.extension_calls.find(&expression);
                extension != analysis_.extension_calls.end()) {
              Substitutions extension_substitutions;
              std::size_t type_index = 0;
              for (const std::string &parameter :
                   extension->second.extension->type_parameters)
                add_type_parameter_substitution(
                    extension_substitutions, parameter,
                    resolve(extension->second.type_arguments[type_index++],
                            substitutions));
              for (const std::string &parameter :
                   extension->second.method->type_parameters)
                add_type_parameter_substitution(
                    extension_substitutions, parameter,
                    resolve(extension->second.type_arguments[type_index++],
                            substitutions));
              return resolve(extension->second.method->return_type,
                             extension_substitutions);
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
                const std::vector<const janus::Type *> type_arguments =
                    effective_type_arguments(
                        method.type_parameters, node.type_arguments,
                        &expression, substitutions, node.location,
                        std::string{object_type.name()} + "." + node.method);
                for (std::size_t index = 0; index < type_arguments.size();
                     ++index)
                  add_type_parameter_substitution(
                      method_substitutions, method.type_parameters[index],
                      *type_arguments[index]);
                return resolve(method.return_type, method_substitutions);
              }
            }
            return janus::Type::int_type();
          } else if constexpr (std::is_same_v<Node,
                                              janus::ast::IndexExpression>) {
            return resolve(
                analysis_.indexed_capabilities.at(&node).element_type);
          } else if constexpr (std::is_same_v<Node, janus::ast::IfExpression>) {
            return expression_type(*node.then_expression, substitutions,
                                   locals);
          } else if constexpr (std::is_same_v<Node,
                                              janus::ast::MatchExpression>) {
            const janus::Type &match_type =
                expression_type(*node.scrutinee, substitutions, locals);
            const janus::ast::MatchExpression::Arm &arm = node.arms.front();
            std::unordered_map<std::string, Local> arm_locals = locals;
            std::function<void(const janus::ast::MatchPattern &,
                               const janus::Type &)>
                add_pattern_locals;
            add_pattern_locals = [&](const janus::ast::MatchPattern &pattern,
                                     const janus::Type &type) {
              if (pattern.kind == janus::ast::MatchPattern::Kind::Alias) {
                add_pattern_locals(*pattern.nested, type);
                arm_locals.insert_or_assign(pattern.name,
                                            Local{nullptr, &type});
              } else if (pattern.kind == janus::ast::MatchPattern::Kind::Name) {
                bool enum_case = false;
                if (type.kind() == janus::TypeKind::Enum) {
                  const auto &specialization =
                      enum_specializations_.at(std::string{type.name()});
                  enum_case =
                      std::any_of(specialization.declaration->cases.begin(),
                                  specialization.declaration->cases.end(),
                                  [&](const auto &candidate) {
                                    return candidate.name == pattern.name;
                                  });
                }
                if (!enum_case)
                  arm_locals.insert_or_assign(pattern.name,
                                              Local{nullptr, &type});
              } else if (pattern.kind ==
                         janus::ast::MatchPattern::Kind::Constructor) {
                if (type.kind() == janus::TypeKind::Enum) {
                  const EnumSpecialization &specialization =
                      enum_specializations_.at(std::string{type.name()});
                  const auto enum_case =
                      std::find_if(specialization.declaration->cases.begin(),
                                   specialization.declaration->cases.end(),
                                   [&](const auto &candidate) {
                                     return candidate.name == pattern.name;
                                   });
                  for (std::size_t index = 0; index < pattern.children.size();
                       ++index) {
                    const janus::Type &child_type =
                        resolve(enum_case->payload_types[index],
                                specialization.substitutions);
                    add_pattern_locals(pattern.children[index], child_type);
                  }
                } else if (type.kind() == janus::TypeKind::Class ||
                           type.kind() == janus::TypeKind::Struct) {
                  const ClassSpecialization &specialization =
                      class_specializations_.at(std::string{type.name()});
                  for (std::size_t index = 0; index < pattern.children.size();
                       ++index) {
                    const janus::Type &child_type = resolve(
                        *specialization.declaration->constructor_fields[index]
                             .declared_type,
                        specialization.substitutions);
                    add_pattern_locals(pattern.children[index], child_type);
                  }
                }
              }
            };
            add_pattern_locals(arm.patterns.front(), match_type);
            return expression_type(*arm.expression, substitutions, arm_locals);
          } else if constexpr (std::is_same_v<Node,
                                              janus::ast::MoveExpression>) {
            return expression_type(*node.operand, substitutions, locals);
          } else if constexpr (std::is_same_v<Node,
                                              janus::ast::TryExpression>) {
            const auto protocol = analysis_.try_protocols.find(&node);
            if (protocol == analysis_.try_protocols.end())
              throw std::runtime_error{"missing analyzed Try protocol"};
            if (!protocol->second.output_type.is_concrete() &&
                !protocol->second.output_type.is_class() &&
                !protocol->second.output_type.is_enum() &&
                !protocol->second.output_type.is_pointer() &&
                !protocol->second.output_type.is_function())
              if (const auto substitution = substitutions.find(
                      protocol->second.output_type.parameter);
                  substitution != substitutions.end())
                return *substitution->second;
            return resolve(protocol->second.output_type);
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

  std::string active_source_name() const {
    if (active_module_.has_value())
      return *active_module_ + ".janus";
    return source_name_;
  }

  ::llvm::CallInst *emit_panic_call(::llvm::Value *data, ::llvm::Value *length,
                                    janus::SourceLocation location,
                                    ::llvm::IRBuilder<> &builder) {
    // Leave the frames installed: janus_panic_with_context drains them before
    // global finalization.  Each action owns a frame so that a second panic
    // raised by a destructor cannot skip the remaining actions in this scope.
    static_cast<void>(push_transient_panic_cleanup(builder));
    ::llvm::Value *file =
        builder.CreateGlobalString(active_source_name(), "panic.file");
    ::llvm::Value *function =
        builder.CreateGlobalString(active_function_, "panic.function");
    ::llvm::FunctionCallee panic_function = module_->getOrInsertFunction(
        "janus_panic_with_context",
        ::llvm::FunctionType::get(builder.getVoidTy(),
                                  {builder.getPtrTy(), builder.getInt64Ty(),
                                   builder.getPtrTy(), builder.getInt32Ty(),
                                   builder.getPtrTy(), builder.getInt32Ty()},
                                  false));
    return builder.CreateCall(
        panic_function,
        {data, length, file, builder.getInt32(location.line), function,
         builder.getInt32(static_cast<unsigned>(panic_trace_))});
  }

  void emit_integer_panic(std::string_view message,
                          janus::SourceLocation location,
                          ::llvm::IRBuilder<> &builder) {
    ::llvm::Value *data =
        builder.CreateGlobalString(message, "integer.panic.message");
    emit_panic_call(data, builder.getInt64(message.size()), location, builder);
    builder.CreateUnreachable();
  }

  ::llvm::Value *emit_integer_division(::llvm::Value *left,
                                       ::llvm::Value *right,
                                       const janus::Type &operand_type,
                                       bool is_remainder, bool is_unsigned,
                                       janus::SourceLocation location,
                                       ::llvm::IRBuilder<> &builder) {
    ::llvm::Value *zero = ::llvm::ConstantInt::get(right->getType(), 0, false);
    ::llvm::Value *divides_by_zero =
        builder.CreateICmpEQ(right, zero, "integer.division.zero");

    ::llvm::Function *function = builder.GetInsertBlock()->getParent();
    auto *zero_trap_block = ::llvm::BasicBlock::Create(
        context_, "integer.division.zero_trap", function);
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
      emit_integer_panic("integer division overflow\n", location, builder);
    }

    builder.SetInsertPoint(zero_trap_block);
    emit_integer_panic("integer division by zero\n", location, builder);

    function->insert(function->end(), valid_block);
    builder.SetInsertPoint(valid_block);
    if (is_remainder)
      return is_unsigned ? builder.CreateURem(left, right, "rem")
                         : builder.CreateSRem(left, right, "rem");
    return is_unsigned ? builder.CreateUDiv(left, right, "div")
                       : builder.CreateSDiv(left, right, "div");
  }

  ::llvm::Value *emit_controlled_binary_operation(
      janus::ast::BinaryOperator operation, ::llvm::Value *left,
      ::llvm::Value *right, const janus::Type &operand_type,
      janus::SourceLocation location, ::llvm::IRBuilder<> &builder) {
    if (operation == janus::ast::BinaryOperator::ShiftLeft ||
        operation == janus::ast::BinaryOperator::ShiftRight) {
      ::llvm::Value *invalid = builder.CreateICmpUGE(
          right,
          ::llvm::ConstantInt::get(right->getType(), operand_type.bit_width()),
          "shift.count.invalid");
      ::llvm::Function *function = builder.GetInsertBlock()->getParent();
      auto *panic_block = ::llvm::BasicBlock::Create(
          context_, "shift.count.panic", function);
      auto *valid_block =
          ::llvm::BasicBlock::Create(context_, "shift.count.valid");
      builder.CreateCondBr(invalid, panic_block, valid_block);
      builder.SetInsertPoint(panic_block);
      emit_integer_panic("shift count exceeds operand width\n", location,
                         builder);
      function->insert(function->end(), valid_block);
      builder.SetInsertPoint(valid_block);
      right = builder.CreateIntCast(right, left->getType(), false,
                                    "shift.count");
      if (operation == janus::ast::BinaryOperator::ShiftLeft)
        return builder.CreateShl(left, right, "shift.left");
      return operand_type.is_signed()
                 ? builder.CreateAShr(left, right, "shift.right")
                 : builder.CreateLShr(left, right, "shift.right");
    }

    const bool is_floating = operand_type.is_floating_point();
    const bool is_unsigned_integer =
        operand_type.kind() == janus::TypeKind::Char ||
        (operand_type.is_integer() && !operand_type.is_signed());
    switch (operation) {
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
                 : emit_integer_division(left, right, operand_type, false,
                                         is_unsigned_integer, location,
                                         builder);
    case janus::ast::BinaryOperator::Remainder:
      return emit_integer_division(left, right, operand_type, true,
                                   is_unsigned_integer, location, builder);
    case janus::ast::BinaryOperator::BitwiseAnd:
      return builder.CreateAnd(left, right, "bitwise.and");
    case janus::ast::BinaryOperator::BitwiseXor:
      return builder.CreateXor(left, right, "bitwise.xor");
    case janus::ast::BinaryOperator::BitwiseOr:
      return builder.CreateOr(left, right, "bitwise.or");
    default:
      throw std::logic_error{"unsupported arithmetic binary operator"};
    }
  }

  ::llvm::Value *emit_borrow_storage(
      const janus::ast::Expression &expression,
      const janus::Type &parameter_type, const Substitutions &substitutions,
      const std::unordered_map<std::string, Local> &locals,
      ::llvm::IRBuilder<> &builder) {
    if (const auto *identifier =
            std::get_if<janus::ast::IdentifierExpression>(&expression.value))
      return resolve_storage(identifier->name, locals).storage;
    const auto *load =
        std::get_if<janus::ast::MethodCallExpression>(&expression.value);
    if (load == nullptr || load->method != "load" ||
        load->arguments.size() != 1)
      return nullptr;
    const janus::Type &object_type =
        expression_type(*load->object, substitutions, locals);
    if (object_type.kind() != janus::TypeKind::Pointer ||
        &pointer_element(object_type) != &parameter_type)
      return nullptr;
    ::llvm::Value *pointer = emit_expression(
        *load->object, object_type, substitutions, locals, builder);
    ::llvm::Value *index = emit_expression(
        *load->arguments.front(), janus::Type::usize_type(), substitutions,
        locals, builder);
    return builder.CreateInBoundsGEP(lower_type(parameter_type, context_),
                                     pointer, index, "borrow.element");
  }

  ::llvm::Value *emit_parameter_argument(
      const janus::ast::FunctionDeclaration::Parameter &parameter,
      const janus::ast::Expression &expression,
      const janus::Type &parameter_type, const Substitutions &substitutions,
      const std::unordered_map<std::string, Local> &locals,
      ::llvm::IRBuilder<> &builder) {
    const bool indirect_borrow =
        parameter.ownership ==
            janus::ast::ParameterOwnership::BorrowMutable ||
        (parameter.ownership == janus::ast::ParameterOwnership::Borrow &&
         (parameter_type.kind() == janus::TypeKind::Struct ||
          parameter_type.kind() == janus::TypeKind::Enum));
    if (!indirect_borrow) {
      if (parameter.is_scoped)
        if (const auto *lambda =
                std::get_if<janus::ast::LambdaExpression>(&expression.value))
          return emit_lambda(*lambda, parameter_type, substitutions, locals,
                             builder, true);
      return emit_expression(expression, parameter_type, substitutions, locals,
                             builder);
    }
    if (::llvm::Value *storage = emit_borrow_storage(
            expression, parameter_type, substitutions, locals, builder))
      return storage;
    ::llvm::Value *value = emit_expression(expression, parameter_type,
                                           substitutions, locals, builder);
    ::llvm::Value *storage = create_entry_alloca(
        builder, lower_type(parameter_type, context_), "borrow.temporary");
    builder.CreateStore(value, storage);
    return storage;
  }

  ::llvm::Value *emit_declared_call(
      const janus::ast::FunctionDeclaration &callee,
      const std::vector<janus::ast::TypeReference> &call_type_arguments,
      const std::vector<std::unique_ptr<janus::ast::Expression>>
          &call_arguments,
      const janus::ast::Expression *expression_key,
      std::string_view result_name, const Substitutions &substitutions,
      const std::unordered_map<std::string, Local> &locals,
      ::llvm::IRBuilder<> &builder) {
    const std::vector<const janus::Type *> type_arguments =
        effective_type_arguments(callee.type_parameters, call_type_arguments,
                                 expression_key, substitutions,
                                 callee.location, result_name);
    ::llvm::Function *target = emit_function(callee, type_arguments);

    Substitutions callee_substitutions;
    for (std::size_t index = 0; index < type_arguments.size(); ++index)
      add_type_parameter_substitution(callee_substitutions,
                                      callee.type_parameters[index],
                                      *type_arguments[index]);
    std::vector<::llvm::Value *> arguments;
    arguments.reserve(call_arguments.size());
    for (std::size_t index = 0; index < call_arguments.size(); ++index) {
      if (index >= callee.parameters.size()) {
        const janus::Type &argument_type =
            expression_type(*call_arguments[index], substitutions, locals);
        ::llvm::Value *argument =
            emit_expression(*call_arguments[index], argument_type,
                            substitutions, locals, builder);
        if (argument_type.bit_width() < 32 && argument_type.is_integer())
          argument = builder.CreateIntCast(argument, builder.getInt32Ty(),
                                           argument_type.is_signed(),
                                           "vararg.integer");
        else if (argument_type.kind() == janus::TypeKind::Float)
          argument = builder.CreateFPExt(argument, builder.getDoubleTy(),
                                         "vararg.float");
        else if (argument_type.kind() == janus::TypeKind::Bool)
          argument =
              builder.CreateZExt(argument, builder.getInt32Ty(), "vararg.bool");
        arguments.push_back(argument);
        continue;
      }
      const janus::Type &parameter_type =
          resolve(callee.parameters[index].type, callee_substitutions);
      arguments.push_back(emit_parameter_argument(
          callee.parameters[index], *call_arguments[index], parameter_type,
          substitutions, locals, builder));
    }
    return target->getReturnType()->isVoidTy()
               ? emit_protected_call(target, arguments, builder)
               : emit_protected_call(target, arguments, builder,
                                     std::string{result_name} + ".result");
  }

  ::llvm::Value *emit_aggregate_field(::llvm::Value *value,
                                      const janus::Type &type, unsigned index,
                                      const janus::Type &field_type,
                                      ::llvm::IRBuilder<> &builder,
                                      std::string_view name) {
    if (type.kind() == janus::TypeKind::Class) {
      ::llvm::Value *address = builder.CreateStructGEP(
          llvm_class_types_.at(std::string{type.name()}), value, index,
          std::string{name} + ".address");
      return builder.CreateLoad(lower_type(field_type, context_), address,
                                std::string{name});
    }
    return builder.CreateExtractValue(value, index, std::string{name});
  }

  ::llvm::Value *emit_string_equal(::llvm::Value *left, ::llvm::Value *right,
                                   ::llvm::IRBuilder<> &builder) {
    ::llvm::Value *left_data =
        builder.CreateExtractValue(left, 0, "string.left.data");
    ::llvm::Value *left_length =
        builder.CreateExtractValue(left, 1, "string.left.length");
    ::llvm::Value *right_data =
        builder.CreateExtractValue(right, 0, "string.right.data");
    ::llvm::Value *right_length =
        builder.CreateExtractValue(right, 1, "string.right.length");
    ::llvm::Value *same_length =
        builder.CreateICmpEQ(left_length, right_length, "same.length");
    ::llvm::BasicBlock *length_block = builder.GetInsertBlock();
    ::llvm::Function *function = length_block->getParent();
    auto *compare_block =
        ::llvm::BasicBlock::Create(context_, "string.compare", function);
    auto *merge_block =
        ::llvm::BasicBlock::Create(context_, "string.equal", function);
    builder.CreateCondBr(same_length, compare_block, merge_block);
    builder.SetInsertPoint(compare_block);
    ::llvm::FunctionCallee memcmp_function = module_->getOrInsertFunction(
        "janus_memcmp",
        ::llvm::FunctionType::get(
            builder.getInt32Ty(),
            {builder.getPtrTy(), builder.getPtrTy(), builder.getInt64Ty()},
            false));
    ::llvm::Value *comparison = builder.CreateCall(
        memcmp_function, {left_data, right_data, left_length}, "memcmp");
    ::llvm::Value *same_bytes =
        builder.CreateICmpEQ(comparison, builder.getInt32(0), "same.bytes");
    builder.CreateBr(merge_block);
    builder.SetInsertPoint(merge_block);
    auto *equal = builder.CreatePHI(builder.getInt1Ty(), 2, "string.equals");
    equal->addIncoming(builder.getFalse(), length_block);
    equal->addIncoming(same_bytes, compare_block);
    return equal;
  }

  ::llvm::Value *emit_structural_equal(::llvm::Value *left,
                                       ::llvm::Value *right,
                                       const janus::Type &type,
                                       ::llvm::IRBuilder<> &builder) {
    if (type.kind() == janus::TypeKind::String)
      return emit_string_equal(left, right, builder);
    if (type.is_floating_point())
      return builder.CreateFCmpOEQ(left, right, "derived.equal");
    if (type.kind() != janus::TypeKind::Struct &&
        type.kind() != janus::TypeKind::Class &&
        type.kind() != janus::TypeKind::Enum)
      return builder.CreateICmpEQ(left, right, "derived.equal");

    if (type.kind() == janus::TypeKind::Struct ||
        type.kind() == janus::TypeKind::Class) {
      const ClassSpecialization &specialization =
          class_specializations_.at(std::string{type.name()});
      ::llvm::Value *equal = builder.getTrue();
      unsigned index = 0;
      const auto compare_field =
          [&](const janus::ast::ValueDeclaration &field) {
            const janus::Type &field_type =
                resolve(field.declared_type, specialization.substitutions);
            ::llvm::Value *left_field = emit_aggregate_field(
                left, type, index, field_type, builder, "derived.left.field");
            ::llvm::Value *right_field = emit_aggregate_field(
                right, type, index, field_type, builder, "derived.right.field");
            equal =
                builder.CreateAnd(equal,
                                  emit_structural_equal(left_field, right_field,
                                                        field_type, builder),
                                  "derived.fields.equal");
            ++index;
          };
      for (const auto &field : specialization.declaration->constructor_fields)
        compare_field(field);
      for (const auto &field : specialization.declaration->fields)
        compare_field(field);
      return equal;
    }

    const EnumSpecialization &specialization =
        enum_specializations_.at(std::string{type.name()});
    ::llvm::Value *left_tag =
        builder.CreateExtractValue(left, 0, "derived.left.tag");
    ::llvm::Value *right_tag =
        builder.CreateExtractValue(right, 0, "derived.right.tag");
    ::llvm::BasicBlock *start = builder.GetInsertBlock();
    ::llvm::Function *function = start->getParent();
    auto *dispatch =
        ::llvm::BasicBlock::Create(context_, "derived.enum.dispatch", function);
    auto *merge =
        ::llvm::BasicBlock::Create(context_, "derived.enum.equal", function);
    builder.CreateCondBr(builder.CreateICmpEQ(left_tag, right_tag), dispatch,
                         merge);
    builder.SetInsertPoint(dispatch);
    auto *switch_value = builder.CreateSwitch(
        left_tag, merge, specialization.declaration->cases.size());
    std::vector<std::pair<::llvm::Value *, ::llvm::BasicBlock *>> results;
    unsigned payload_index = 1;
    for (const auto &enum_case : specialization.declaration->cases) {
      auto *case_block = ::llvm::BasicBlock::Create(
          context_, "derived.enum." + enum_case.name, function);
      switch_value->addCase(builder.getInt32(enum_case.value), case_block);
      builder.SetInsertPoint(case_block);
      ::llvm::Value *equal = builder.getTrue();
      for (const auto &payload : enum_case.payload_types) {
        const janus::Type &payload_type =
            resolve(payload, specialization.substitutions);
        if (payload_type.kind() == janus::TypeKind::Unit)
          continue;
        equal = builder.CreateAnd(
            equal,
            emit_structural_equal(
                builder.CreateExtractValue(left, payload_index),
                builder.CreateExtractValue(right, payload_index), payload_type,
                builder),
            "derived.payload.equal");
        ++payload_index;
      }
      ::llvm::BasicBlock *end = builder.GetInsertBlock();
      builder.CreateBr(merge);
      results.emplace_back(equal, end);
    }
    builder.SetInsertPoint(merge);
    auto *equal = builder.CreatePHI(builder.getInt1Ty(), results.size() + 2,
                                    "derived.enum.equals");
    equal->addIncoming(builder.getFalse(), start);
    equal->addIncoming(builder.getTrue(), dispatch);
    for (const auto &[result, block] : results)
      equal->addIncoming(result, block);
    return equal;
  }

  static std::uint64_t nominal_hash(std::string_view text) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char byte : text) {
      hash ^= byte;
      hash *= 1099511628211ULL;
    }
    return hash;
  }

  ::llvm::Value *mix_hash(::llvm::Value *hash, ::llvm::Value *value,
                          ::llvm::IRBuilder<> &builder) {
    if (value->getType() != builder.getInt64Ty()) {
      if (value->getType()->isIntegerTy())
        value = builder.CreateZExtOrTrunc(value, builder.getInt64Ty());
      else
        value = builder.CreateBitCast(value, builder.getInt64Ty());
    }
    return builder.CreateMul(builder.CreateXor(hash, value),
                             builder.getInt64(1099511628211ULL),
                             "derived.hash.mix");
  }

  ::llvm::Value *emit_structural_hash(::llvm::Value *value,
                                      const janus::Type &type,
                                      ::llvm::IRBuilder<> &builder,
                                      ::llvm::Value *seed = nullptr) {
    ::llvm::Value *hash =
        seed != nullptr ? seed : builder.getInt64(nominal_hash(type.name()));
    if (type.kind() == janus::TypeKind::String) {
      ::llvm::Value *data =
          builder.CreateExtractValue(value, 0, "hash.string.data");
      ::llvm::Value *length =
          builder.CreateExtractValue(value, 1, "hash.string.length");
      ::llvm::FunctionCallee function = module_->getOrInsertFunction(
          "janus_hash_bytes",
          ::llvm::FunctionType::get(
              builder.getInt64Ty(),
              {builder.getPtrTy(), builder.getInt64Ty(), builder.getInt64Ty()},
              false));
      return builder.CreateCall(function, {data, length, hash}, "string.hash");
    }
    if (type.kind() != janus::TypeKind::Struct &&
        type.kind() != janus::TypeKind::Class &&
        type.kind() != janus::TypeKind::Enum) {
      if (type.is_floating_point()) {
        value = builder.CreateSelect(
            builder.CreateFCmpOEQ(
                value, ::llvm::ConstantFP::get(value->getType(), 0.0),
                "derived.hash.zero"),
            ::llvm::ConstantFP::get(value->getType(), 0.0), value,
            "derived.hash.normalized");
        ::llvm::Type *bits = type.kind() == janus::TypeKind::Float
                                 ? builder.getInt32Ty()
                                 : builder.getInt64Ty();
        value = builder.CreateBitCast(value, bits);
      }
      return mix_hash(hash, value, builder);
    }
    if (type.kind() == janus::TypeKind::Struct ||
        type.kind() == janus::TypeKind::Class) {
      const ClassSpecialization &specialization =
          class_specializations_.at(std::string{type.name()});
      unsigned index = 0;
      const auto hash_field = [&](const janus::ast::ValueDeclaration &field) {
        const janus::Type &field_type =
            resolve(field.declared_type, specialization.substitutions);
        hash = emit_structural_hash(emit_aggregate_field(value, type, index,
                                                         field_type, builder,
                                                         "derived.hash.field"),
                                    field_type, builder, hash);
        ++index;
      };
      for (const auto &field : specialization.declaration->constructor_fields)
        hash_field(field);
      for (const auto &field : specialization.declaration->fields)
        hash_field(field);
      return hash;
    }
    const EnumSpecialization &specialization =
        enum_specializations_.at(std::string{type.name()});
    ::llvm::Value *tag =
        builder.CreateExtractValue(value, 0, "derived.hash.tag");
    hash = mix_hash(hash, tag, builder);
    ::llvm::Function *function = builder.GetInsertBlock()->getParent();
    auto *merge =
        ::llvm::BasicBlock::Create(context_, "derived.enum.hash", function);
    auto *switch_value = builder.CreateSwitch(
        tag, merge, specialization.declaration->cases.size());
    std::vector<std::pair<::llvm::Value *, ::llvm::BasicBlock *>> results;
    unsigned payload_index = 1;
    for (const auto &enum_case : specialization.declaration->cases) {
      auto *case_block = ::llvm::BasicBlock::Create(
          context_, "derived.hash." + enum_case.name, function);
      switch_value->addCase(builder.getInt32(enum_case.value), case_block);
      builder.SetInsertPoint(case_block);
      ::llvm::Value *case_hash = hash;
      for (const auto &payload : enum_case.payload_types) {
        const janus::Type &payload_type =
            resolve(payload, specialization.substitutions);
        if (payload_type.kind() == janus::TypeKind::Unit)
          continue;
        case_hash = emit_structural_hash(
            builder.CreateExtractValue(value, payload_index), payload_type,
            builder, case_hash);
        ++payload_index;
      }
      ::llvm::BasicBlock *end = builder.GetInsertBlock();
      builder.CreateBr(merge);
      results.emplace_back(case_hash, end);
    }
    builder.SetInsertPoint(merge);
    auto *result = builder.CreatePHI(builder.getInt64Ty(), results.size() + 1,
                                     "derived.enum.hash.value");
    result->addIncoming(hash, switch_value->getParent());
    for (const auto &[case_hash, block] : results)
      result->addIncoming(case_hash, block);
    return result;
  }

  ::llvm::Value *emit_debug_text(std::string_view text,
                                 ::llvm::IRBuilder<> &builder) {
    ::llvm::Value *data =
        builder.CreateGlobalString(std::string{text}, "debug.text");
    ::llvm::FunctionCallee function = module_->getOrInsertFunction(
        "janus_write_stdout",
        ::llvm::FunctionType::get(builder.getVoidTy(),
                                  {builder.getPtrTy(), builder.getInt64Ty()},
                                  false));
    return builder.CreateCall(function, {data, builder.getInt64(text.size())});
  }

  void emit_debug_value(::llvm::Value *value, const janus::Type &type,
                        ::llvm::IRBuilder<> &builder) {
    if (type.kind() == janus::TypeKind::String) {
      emit_debug_text("\"", builder);
      ::llvm::FunctionCallee function = module_->getOrInsertFunction(
          "janus_write_stdout",
          ::llvm::FunctionType::get(builder.getVoidTy(),
                                    {builder.getPtrTy(), builder.getInt64Ty()},
                                    false));
      builder.CreateCall(function, {builder.CreateExtractValue(value, 0),
                                    builder.CreateExtractValue(value, 1)});
      emit_debug_text("\"", builder);
      return;
    }
    if (type.kind() != janus::TypeKind::Struct &&
        type.kind() != janus::TypeKind::Class &&
        type.kind() != janus::TypeKind::Enum) {
      std::string function_name;
      ::llvm::Value *argument = value;
      switch (type.kind()) {
      case janus::TypeKind::Int:
        function_name = "janus_print_int";
        break;
      case janus::TypeKind::UInt:
        function_name = "janus_print_uint";
        break;
      case janus::TypeKind::Long:
        function_name = "janus_print_long";
        break;
      case janus::TypeKind::ULong:
        function_name = "janus_print_ulong";
        break;
      case janus::TypeKind::USize:
        function_name = "janus_print_usize";
        break;
      case janus::TypeKind::ISize:
        function_name = "janus_print_isize";
        break;
      case janus::TypeKind::Double:
        function_name = "janus_print_double";
        break;
      case janus::TypeKind::Float:
        function_name = "janus_print_float";
        break;
      case janus::TypeKind::Bool:
        argument = builder.CreateZExt(value, builder.getInt8Ty(), "debug.bool");
        function_name = "janus_print_bool";
        break;
      case janus::TypeKind::Char:
        function_name = "janus_print_char";
        break;
      case janus::TypeKind::Byte:
      case janus::TypeKind::Short:
        argument = builder.CreateSExt(value, builder.getInt32Ty());
        function_name = type.kind() == janus::TypeKind::Byte
                            ? "janus_print_byte"
                            : "janus_print_short";
        break;
      case janus::TypeKind::UByte:
      case janus::TypeKind::UShort:
        argument = builder.CreateZExt(value, builder.getInt32Ty());
        function_name = type.kind() == janus::TypeKind::UByte
                            ? "janus_print_ubyte"
                            : "janus_print_ushort";
        break;
      default:
        return;
      }
      builder.CreateCall(module_->getOrInsertFunction(
                             function_name, ::llvm::FunctionType::get(
                                                builder.getVoidTy(),
                                                {argument->getType()}, false)),
                         {argument});
      return;
    }
    if (type.kind() == janus::TypeKind::Struct ||
        type.kind() == janus::TypeKind::Class) {
      const ClassSpecialization &specialization =
          class_specializations_.at(std::string{type.name()});
      emit_debug_text(specialization.declaration->name + " { ", builder);
      unsigned index = 0;
      const auto debug_field = [&](const janus::ast::ValueDeclaration &field) {
        if (index != 0)
          emit_debug_text(", ", builder);
        emit_debug_text(field.name + ": ", builder);
        const janus::Type &field_type =
            resolve(field.declared_type, specialization.substitutions);
        emit_debug_value(emit_aggregate_field(value, type, index, field_type,
                                              builder, "derived.debug.field"),
                         field_type, builder);
        ++index;
      };
      for (const auto &field : specialization.declaration->constructor_fields)
        debug_field(field);
      for (const auto &field : specialization.declaration->fields)
        debug_field(field);
      emit_debug_text(" }", builder);
      return;
    }
    const EnumSpecialization &specialization =
        enum_specializations_.at(std::string{type.name()});
    ::llvm::Value *tag =
        builder.CreateExtractValue(value, 0, "derived.debug.tag");
    ::llvm::Function *function = builder.GetInsertBlock()->getParent();
    auto *merge =
        ::llvm::BasicBlock::Create(context_, "derived.enum.debug", function);
    auto *switch_value = builder.CreateSwitch(
        tag, merge, specialization.declaration->cases.size());
    unsigned payload_index = 1;
    for (const auto &enum_case : specialization.declaration->cases) {
      auto *case_block = ::llvm::BasicBlock::Create(
          context_, "derived.debug." + enum_case.name, function);
      switch_value->addCase(builder.getInt32(enum_case.value), case_block);
      builder.SetInsertPoint(case_block);
      emit_debug_text(specialization.declaration->name + "." + enum_case.name,
                      builder);
      const bool has_payload = std::any_of(
          enum_case.payload_types.begin(), enum_case.payload_types.end(),
          [&](const janus::ast::TypeReference &payload) {
            return resolve(payload, specialization.substitutions).kind() !=
                   janus::TypeKind::Unit;
          });
      if (has_payload)
        emit_debug_text("(", builder);
      unsigned stored_index = 0;
      for (std::size_t index = 0; index < enum_case.payload_types.size();
           ++index) {
        const janus::Type &payload_type = resolve(
            enum_case.payload_types[index], specialization.substitutions);
        if (payload_type.kind() == janus::TypeKind::Unit)
          continue;
        if (stored_index != 0)
          emit_debug_text(", ", builder);
        emit_debug_value(
            builder.CreateExtractValue(value, payload_index + stored_index),
            payload_type, builder);
        ++stored_index;
      }
      payload_index += stored_index;
      if (has_payload)
        emit_debug_text(")", builder);
      builder.CreateBr(merge);
    }
    builder.SetInsertPoint(merge);
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
            const std::uint64_t value = node.is_negative
                                            ? std::uint64_t{0} - node.magnitude
                                            : node.magnitude;
            return ::llvm::ConstantInt::get(llvm_type, value,
                                            expected_type.is_signed());
          } else if constexpr (std::is_same_v<
                                   Node, janus::ast::ArrayLiteralExpression>) {
            const ClassSpecialization &specialization =
                class_specializations_.at(std::string{expected_type.name()});
            const auto &class_declaration = *specialization.declaration;
            ::llvm::StructType *class_type =
                llvm_class_types_.at(std::string{expected_type.name()});
            ::llvm::FunctionCallee malloc_function =
                module_->getOrInsertFunction(
                    "janus_alloc",
                    ::llvm::FunctionType::get(builder.getPtrTy(),
                                              {builder.getInt64Ty()}, false));
            ::llvm::Value *object = builder.CreateCall(
                malloc_function, {::llvm::ConstantExpr::getSizeOf(class_type)},
                "array.literal");

            auto initializer_locals = locals;
            ::llvm::Value *capacity = create_entry_alloca(
                builder, lower_type(janus::Type::usize_type(), context_),
                "array.literal.capacity");
            builder.CreateStore(builder.getInt64(node.elements.size()),
                                capacity);
            initializer_locals.insert_or_assign(
                class_declaration.constructor_parameters.front().name,
                Local{capacity, &janus::Type::usize_type()});
            unsigned field_index = 0;
            for (const auto &field_declaration : class_declaration.fields) {
              ::llvm::Value *field =
                  builder.CreateStructGEP(class_type, object, field_index++);
              const janus::Type &field_type =
                  resolve(field_declaration.declared_type,
                          specialization.substitutions);
              builder.CreateStore(
                  emit_expression(*field_declaration.initializer, field_type,
                                  specialization.substitutions,
                                  initializer_locals, builder),
                  field);
              initializer_locals.insert_or_assign(field_declaration.name,
                                                  Local{field, &field_type});
            }

            const janus::ast::FunctionDeclaration *push = nullptr;
            for (const auto &method : class_declaration.methods)
              if (method.name == "push")
                push = &method;
            ::llvm::Function *push_function = emit_function(
                *push, {}, &class_declaration, &specialization.substitutions,
                expected_type.name());
            const janus::Type &element_type = *specialization.substitutions.at(
                class_declaration.type_parameters.front());
            // Until construction completes, make the temporary participate in
            // the same panic/early-exit cleanup stack as other owned values.
            // Array's destructor uses its current length, so only successfully
            // pushed elements are destroyed.
            if (!active_cleanup_scopes_.empty())
              active_cleanup_scopes_.back().owned_values->push_back(
                  {object, &expected_type});
            for (const auto &element : node.elements) {
              ::llvm::Value *value = emit_expression(
                  *element, element_type, substitutions, locals, builder);
              builder.CreateCall(push_function, {object, value});
            }
            if (!active_cleanup_scopes_.empty())
              active_cleanup_scopes_.back().owned_values->pop_back();
            return object;
          } else if constexpr (std::is_same_v<
                                   Node, janus::ast::IdentifierExpression>) {
            if (const auto local = locals.find(node.name);
                local != locals.end() && local->second.is_constant)
              return local->second.storage;
            std::string key = source_global_key(active_module_, node.name);
            if (!global_by_key_.contains(key)) {
              if (const auto exported = public_global_keys_.find(node.name);
                  exported != public_global_keys_.end())
                key = exported->second;
            }
            if (const auto global = global_by_key_.find(key);
                global != global_by_key_.end() &&
                global->second->declaration.is_constant) {
              const janus::constant::Value &value =
                  evaluate_global_constant(*global->second);
              return emit_static_initializer(value, *value.type);
            }
            const Local &local = resolve_storage(node.name, locals);
            if (local.type->kind() == janus::TypeKind::Unit)
              return nullptr;
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
              ::llvm::Value *closure =
                  builder.CreateLoad(lower_type(*local->type, context_),
                                     local->storage, node.callee + ".closure");
              ::llvm::Value *code =
                  builder.CreateExtractValue(closure, 0, node.callee + ".code");
              ::llvm::Value *environment = builder.CreateExtractValue(
                  closure, 1, node.callee + ".environment");
              std::vector<::llvm::Value *> arguments{environment};
              for (std::size_t index = 0; index < node.arguments.size();
                   ++index) {
                const janus::Type &parameter = *signature.parameters[index];
                const janus::ast::ParameterOwnership ownership =
                    signature.parameter_ownership[index];
                const bool indirect_borrow =
                    ownership ==
                        janus::ast::ParameterOwnership::BorrowMutable ||
                    (ownership == janus::ast::ParameterOwnership::Borrow &&
                     (parameter.kind() == janus::TypeKind::Struct ||
                      parameter.kind() == janus::TypeKind::Enum));
                if (indirect_borrow) {
                  if (::llvm::Value *storage = emit_borrow_storage(
                          *node.arguments[index], parameter, substitutions,
                          locals, builder)) {
                    arguments.push_back(storage);
                  } else {
                    ::llvm::Value *value = emit_expression(
                        *node.arguments[index], parameter, substitutions,
                        locals, builder);
                    ::llvm::Value *temporary_storage = create_entry_alloca(
                        builder, lower_type(parameter, context_),
                        "borrow.temporary");
                    builder.CreateStore(value, temporary_storage);
                    arguments.push_back(temporary_storage);
                  }
                } else {
                  arguments.push_back(emit_expression(
                      *node.arguments[index], parameter, substitutions, locals,
                      builder));
                }
              }
              std::vector<::llvm::Type *> parameter_types{builder.getPtrTy()};
              for (std::size_t index = 0;
                   index < signature.parameters.size(); ++index) {
                const janus::Type *parameter = signature.parameters[index];
                const janus::ast::ParameterOwnership ownership =
                    signature.parameter_ownership[index];
                const bool indirect_borrow =
                    ownership ==
                        janus::ast::ParameterOwnership::BorrowMutable ||
                    (ownership == janus::ast::ParameterOwnership::Borrow &&
                     (parameter->kind() == janus::TypeKind::Struct ||
                      parameter->kind() == janus::TypeKind::Enum));
                parameter_types.push_back(
                    indirect_borrow ? builder.getPtrTy()
                                    : lower_type(*parameter, context_));
              }
              ::llvm::Type *callee_return_type =
                  signature.return_ownership ==
                          janus::ast::ReturnOwnership::BorrowMutable
                      ? builder.getPtrTy()
                      : lower_type(*signature.return_type, context_);
              auto *callee_type = ::llvm::FunctionType::get(
                  callee_return_type, parameter_types, false);
              ::llvm::Value *result =
                  signature.return_type->kind() == janus::TypeKind::Unit
                      ? emit_protected_indirect_call(callee_type, code,
                                                     arguments, builder)
                      : emit_protected_indirect_call(
                            callee_type, code, arguments, builder,
                            node.callee + ".call");
              return result;
            }
            if (node.callee == "debug") {
              const janus::Type &argument_type = expression_type(
                  *node.arguments.front(), substitutions, locals);
              ::llvm::Value *argument =
                  emit_expression(*node.arguments.front(), argument_type,
                                  substitutions, locals, builder);
              emit_debug_value(argument, argument_type, builder);
              return emit_debug_text("\n", builder);
            }
            if (node.callee == "__derivedHash" ||
                node.callee == "__derivedEquals") {
              const janus::Type &argument_type =
                  resolve(node.type_arguments.front(), substitutions);
              ::llvm::Value *left =
                  emit_expression(*node.arguments.front(), argument_type,
                                  substitutions, locals, builder);
              if (node.callee == "__derivedHash")
                return emit_structural_hash(left, argument_type, builder);
              ::llvm::Value *right =
                  emit_expression(*node.arguments[1], argument_type,
                                  substitutions, locals, builder);
              return emit_structural_equal(left, right, argument_type, builder);
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
                    ::llvm::FunctionType::get(builder.getVoidTy(),
                                              {builder.getInt32Ty()}, false));
                result = builder.CreateCall(function, {argument});
                break;
              }
              case janus::TypeKind::UInt: {
                ::llvm::FunctionCallee function = module_->getOrInsertFunction(
                    "janus_print_uint",
                    ::llvm::FunctionType::get(builder.getVoidTy(),
                                              {builder.getInt32Ty()}, false));
                result = builder.CreateCall(function, {argument});
                break;
              }
              case janus::TypeKind::Long: {
                ::llvm::FunctionCallee function = module_->getOrInsertFunction(
                    "janus_print_long",
                    ::llvm::FunctionType::get(builder.getVoidTy(),
                                              {builder.getInt64Ty()}, false));
                result = builder.CreateCall(function, {argument});
                break;
              }
              case janus::TypeKind::ULong: {
                ::llvm::FunctionCallee function = module_->getOrInsertFunction(
                    "janus_print_ulong",
                    ::llvm::FunctionType::get(builder.getVoidTy(),
                                              {builder.getInt64Ty()}, false));
                result = builder.CreateCall(function, {argument});
                break;
              }
              case janus::TypeKind::Byte: {
                ::llvm::FunctionCallee function = module_->getOrInsertFunction(
                    "janus_print_byte",
                    ::llvm::FunctionType::get(builder.getVoidTy(),
                                              {builder.getInt32Ty()}, false));
                ::llvm::Value *signed_argument = builder.CreateSExt(
                    argument, builder.getInt32Ty(), "print.byte.signed");
                result = builder.CreateCall(function, {signed_argument});
                break;
              }
              case janus::TypeKind::UByte: {
                ::llvm::FunctionCallee function = module_->getOrInsertFunction(
                    "janus_print_ubyte",
                    ::llvm::FunctionType::get(builder.getVoidTy(),
                                              {builder.getInt32Ty()}, false));
                ::llvm::Value *unsigned_argument = builder.CreateZExt(
                    argument, builder.getInt32Ty(), "print.ubyte.unsigned");
                result = builder.CreateCall(function, {unsigned_argument});
                break;
              }
              case janus::TypeKind::Short: {
                ::llvm::FunctionCallee function = module_->getOrInsertFunction(
                    "janus_print_short",
                    ::llvm::FunctionType::get(builder.getVoidTy(),
                                              {builder.getInt32Ty()}, false));
                ::llvm::Value *signed_argument = builder.CreateSExt(
                    argument, builder.getInt32Ty(), "print.short.signed");
                result = builder.CreateCall(function, {signed_argument});
                break;
              }
              case janus::TypeKind::UShort: {
                ::llvm::FunctionCallee function = module_->getOrInsertFunction(
                    "janus_print_ushort",
                    ::llvm::FunctionType::get(builder.getVoidTy(),
                                              {builder.getInt32Ty()}, false));
                ::llvm::Value *unsigned_argument = builder.CreateZExt(
                    argument, builder.getInt32Ty(), "print.ushort.unsigned");
                result = builder.CreateCall(function, {unsigned_argument});
                break;
              }
              case janus::TypeKind::USize: {
                ::llvm::FunctionCallee function = module_->getOrInsertFunction(
                    "janus_print_usize",
                    ::llvm::FunctionType::get(builder.getVoidTy(),
                                              {builder.getInt64Ty()}, false));
                result = builder.CreateCall(function, {argument});
                break;
              }
              case janus::TypeKind::ISize: {
                ::llvm::FunctionCallee function = module_->getOrInsertFunction(
                    "janus_print_isize",
                    ::llvm::FunctionType::get(builder.getVoidTy(),
                                              {builder.getInt64Ty()}, false));
                result = builder.CreateCall(function, {argument});
                break;
              }
              case janus::TypeKind::Double: {
                ::llvm::FunctionCallee function = module_->getOrInsertFunction(
                    "janus_print_double",
                    ::llvm::FunctionType::get(builder.getVoidTy(),
                                              {builder.getDoubleTy()}, false));
                result = builder.CreateCall(function, {argument});
                break;
              }
              case janus::TypeKind::Float: {
                ::llvm::FunctionCallee function = module_->getOrInsertFunction(
                    "janus_print_float",
                    ::llvm::FunctionType::get(builder.getVoidTy(),
                                              {builder.getFloatTy()}, false));
                result = builder.CreateCall(function, {argument});
                break;
              }
              case janus::TypeKind::Bool: {
                ::llvm::FunctionCallee function = module_->getOrInsertFunction(
                    "janus_print_bool",
                    ::llvm::FunctionType::get(builder.getVoidTy(),
                                              {builder.getInt8Ty()}, false));
                argument = builder.CreateZExt(argument, builder.getInt8Ty(),
                                              "print.bool");
                result = builder.CreateCall(function, {argument});
                break;
              }
              case janus::TypeKind::Char: {
                ::llvm::FunctionCallee function = module_->getOrInsertFunction(
                    "janus_print_char",
                    ::llvm::FunctionType::get(builder.getVoidTy(),
                                              {builder.getInt32Ty()}, false));
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
                result = builder.CreateCall(function,
                                            {newline, builder.getInt64(1)});
              }
              return result;
            }
            if (node.callee == "cstr") {
              ::llvm::Value *text = emit_expression(
                  *node.arguments.front(), janus::Type::string_type(),
                  substitutions, locals, builder);
              return builder.CreateExtractValue(text, 0, "cstr.data");
            }
            if (node.callee == "stringData" || node.callee == "stringLength") {
              ::llvm::Value *text = emit_expression(
                  *node.arguments.front(), janus::Type::string_type(),
                  substitutions, locals, builder);
              return builder.CreateExtractValue(
                  text, node.callee == "stringData" ? 0 : 1,
                  node.callee == "stringData" ? "string.data"
                                              : "string.length");
            }
            if (node.callee == "stringView") {
              const janus::Type &pointer_type =
                  expression_type(*node.arguments[0], substitutions, locals);
              ::llvm::Value *data =
                  emit_expression(*node.arguments[0], pointer_type,
                                  substitutions, locals, builder);
              ::llvm::Value *length =
                  emit_expression(*node.arguments[1], janus::Type::usize_type(),
                                  substitutions, locals, builder);
              ::llvm::Value *view = llvm::UndefValue::get(
                  lower_type(janus::Type::string_type(), context_));
              view =
                  builder.CreateInsertValue(view, data, 0, "string.view.data");
              return builder.CreateInsertValue(view, length, 1,
                                               "string.view.length");
            }
            if (node.callee == "panic") {
              ::llvm::Value *message = emit_expression(
                  *node.arguments.front(), janus::Type::string_type(),
                  substitutions, locals, builder);
              ::llvm::Value *data =
                  builder.CreateExtractValue(message, 0, "panic.data");
              ::llvm::Value *length =
                  builder.CreateExtractValue(message, 1, "panic.length");
              return emit_panic_call(data, length, node.location, builder);
            }
            if (node.callee == "alloc" || node.callee == "realloc" ||
                node.callee == "reallocPreserving" || node.callee == "null" ||
                node.callee == "sizeof" || node.callee == "alignof") {
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
            if (node.callee == "adoptReallocation") {
              const janus::Type &pointer_type =
                  expression_type(*node.arguments[1], substitutions, locals);
              static_cast<void>(emit_expression(*node.arguments[0],
                                                pointer_type, substitutions,
                                                locals, builder));
              return emit_expression(*node.arguments[1], pointer_type,
                                     substitutions, locals, builder);
            }
            if (node.callee == "owningCapture") {
              const janus::Type &closure_type =
                  expression_type(*node.arguments[1], substitutions, locals);
              return emit_expression(*node.arguments[1], closure_type,
                                     substitutions, locals, builder);
            }
            if (node.callee == "free" || node.callee == "freeStorage") {
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
            if (node.callee == "checkedCast") {
              const janus::Type &destination =
                  resolve(node.type_arguments.front(), substitutions);
              const janus::Type &source_type = expression_type(
                  *node.arguments.front(), substitutions, locals);
              ::llvm::Value *source =
                  emit_expression(*node.arguments.front(), source_type,
                                  substitutions, locals, builder);
              return emit_checked_numeric_cast(source, source_type, destination,
                                               builder);
            }
            if (is_explicit_cast(node)) {
              const janus::Type &conversion_type =
                  cast_destination(node, substitutions);
              const janus::Type &source_type = expression_type(
                  *node.arguments.front(), substitutions, locals);
              ::llvm::Value *source =
                  emit_expression(*node.arguments.front(), source_type,
                                  substitutions, locals, builder);
              if (node.callee == "saturatingCast")
                return emit_clamped_numeric_cast(source, source_type,
                                                 conversion_type, builder);
              if (node.callee == "truncatingCast")
                return emit_truncating_numeric_cast(source, source_type,
                                                    conversion_type, builder);
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
                *find_in_active_module(functions_, node.callee)->second;
            const std::vector<const janus::Type *> type_arguments =
                effective_type_arguments(
                    callee.type_parameters, node.type_arguments, &expression,
                    substitutions, node.location, node.callee);
            ::llvm::Function *target = emit_function(callee, type_arguments);

            Substitutions callee_substitutions;
            for (std::size_t index = 0; index < type_arguments.size();
                 ++index) {
              add_type_parameter_substitution(
                  callee_substitutions, callee.type_parameters[index],
                  *type_arguments[index]);
            }
            std::vector<::llvm::Value *> arguments;
            arguments.reserve(node.arguments.size());
            for (std::size_t index = 0; index < node.arguments.size();
                 ++index) {
              if (index >= callee.parameters.size()) {
                const janus::Type &argument_type = expression_type(
                    *node.arguments[index], substitutions, locals);
                ::llvm::Value *argument =
                    emit_expression(*node.arguments[index], argument_type,
                                    substitutions, locals, builder);
                if (argument_type.bit_width() < 32 &&
                    argument_type.is_integer())
                  argument = builder.CreateIntCast(
                      argument, builder.getInt32Ty(), argument_type.is_signed(),
                      "vararg.integer");
                else if (argument_type.kind() == janus::TypeKind::Float)
                  argument = builder.CreateFPExt(
                      argument, builder.getDoubleTy(), "vararg.float");
                else if (argument_type.kind() == janus::TypeKind::Bool)
                  argument = builder.CreateZExt(argument, builder.getInt32Ty(),
                                                "vararg.bool");
                arguments.push_back(argument);
                continue;
              }
              const janus::Type &parameter_type =
                  resolve(callee.parameters[index].type, callee_substitutions);
              arguments.push_back(emit_parameter_argument(
                  callee.parameters[index], *node.arguments[index],
                  parameter_type, substitutions, locals, builder));
            }
            return target->getReturnType()->isVoidTy()
                       ? emit_protected_call(target, arguments, builder)
                       : emit_protected_call(target, arguments, builder,
                                             node.callee + ".result");
          } else if constexpr (std::is_same_v<Node,
                                              janus::ast::NewExpression>) {
            const auto class_iterator =
                find_type_in_active_module(classes_, node.class_name);
            const auto &class_declaration = *class_iterator->second;
            std::vector<const janus::Type *> type_arguments;
            if (node.type_arguments.empty()) {
              if (const auto inferred =
                      analysis_.inferred_generic_arguments.find(&expression);
                  inferred != analysis_.inferred_generic_arguments.end())
                for (const janus::semantic::SemanticType &argument :
                     inferred->second)
                  type_arguments.push_back(&resolve(argument, substitutions));
            } else {
              for (const janus::ast::TypeReference &argument :
                   node.type_arguments)
                type_arguments.push_back(&resolve(argument, substitutions));
            }
            const janus::Type &object_type =
                ensure_class(class_iterator->first, type_arguments);
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
              ::llvm::Value *storage = create_entry_alloca(
                  builder, lower_type(parameter_type, context_),
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
            const auto enum_name = qualified_expression_name(*node.object);
            const auto enum_declaration =
                enum_name.has_value()
                    ? find_type_in_active_module(enums_, *enum_name)
                    : enums_.end();
            if (enum_name.has_value() && enum_declaration != enums_.end() &&
                (enum_name->find('.') == std::string::npos ||
                 !locals.contains(
                     enum_name->substr(0, enum_name->find('.'))))) {
              const janus::Type &enum_type =
                  ensure_enum(enum_declaration->first, {});
              auto *llvm_enum_type =
                  llvm_enum_types_.at(std::string{enum_type.name()});
              ::llvm::Value *value = ::llvm::UndefValue::get(llvm_enum_type);
              return builder.CreateInsertValue(
                  value,
                  builder.getInt32(
                      enum_case_value(enum_type.name(), node.member)),
                  0, "enum.value");
            }
            if (analysis_.qualified_global_reads.contains(&node)) {
              const Local &global = resolve_qualified_global(node);
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
              ::llvm::Value *object_value = emit_expression(
                  *node.object, object_type, substitutions, locals, builder);
              if (const auto ownership =
                      analysis_.call_return_ownership.find(node.object.get());
                  ownership != analysis_.call_return_ownership.end() &&
                  ownership->second == janus::ast::ReturnOwnership::BorrowMutable &&
                  object_type.kind() != janus::TypeKind::Struct)
                object_value = builder.CreateLoad(
                    builder.getPtrTy(), object_value,
                    node.member + ".mutable.borrow.object");
              if (object_type.kind() == janus::TypeKind::Struct) {
                object_pointer = create_entry_alloca(
                    builder, lower_type(object_type, context_),
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
            if (const auto module = qualified_expression_name(*node.object);
                module.has_value() &&
                !locals.contains(module->substr(0, module->find('.')))) {
              const std::string qualified = *module + "." + node.method;
              if (const auto function =
                      find_in_active_module(functions_, qualified);
                  function != functions_.end())
                return emit_declared_call(
                    *function->second, node.type_arguments, node.arguments,
                    &expression, qualified, substitutions, locals, builder);
            }
            const auto *identifier =
                std::get_if<janus::ast::IdentifierExpression>(
                    &node.object->value);
            const auto enum_name = qualified_expression_name(*node.object);
            const auto enum_declaration =
                enum_name.has_value()
                    ? find_type_in_active_module(enums_, *enum_name)
                    : enums_.end();
            if (enum_name.has_value() && enum_declaration != enums_.end() &&
                (enum_name->find('.') == std::string::npos ||
                 !locals.contains(
                     enum_name->substr(0, enum_name->find('.'))))) {
              std::vector<const janus::Type *> type_arguments;
              if (node.type_arguments.empty()) {
                if (const auto inferred =
                        analysis_.inferred_generic_arguments.find(&expression);
                    inferred != analysis_.inferred_generic_arguments.end())
                  for (const janus::semantic::SemanticType &argument :
                       inferred->second)
                    type_arguments.push_back(
                        &resolve(argument, substitutions));
              } else {
                for (const janus::ast::TypeReference &argument :
                     node.type_arguments)
                  type_arguments.push_back(&resolve(argument, substitutions));
              }
              const janus::Type &enum_type =
                  ensure_enum(enum_declaration->first, type_arguments);
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
                ::llvm::Value *payload = emit_expression(
                    *node.arguments[index], payload_type, substitutions, locals,
                    builder);
                if (payload_type.kind() == janus::TypeKind::Unit)
                  continue;
                value = builder.CreateInsertValue(
                    value, payload, field++, "enum.payload");
              }
              return value;
            }
            const janus::Type &object_type =
                expression_type(*node.object, substitutions, locals);
            if (const auto extension =
                    analysis_.extension_calls.find(&expression);
                extension != analysis_.extension_calls.end()) {
              const auto &resolved = extension->second;
              std::vector<const janus::Type *> type_arguments;
              type_arguments.reserve(resolved.type_arguments.size());
              for (const janus::semantic::SemanticType &argument :
                   resolved.type_arguments)
                type_arguments.push_back(&resolve(argument, substitutions));
              ::llvm::Function *target = emit_function(
                  *resolved.method, type_arguments, nullptr, nullptr, {},
                  nullptr, resolved.extension, resolved.receiver_ownership);
              Substitutions extension_substitutions;
              std::size_t type_index = 0;
              for (const std::string &parameter :
                   resolved.extension->type_parameters)
                add_type_parameter_substitution(
                    extension_substitutions, parameter,
                    *type_arguments[type_index++]);
              for (const std::string &parameter :
                   resolved.method->type_parameters)
                add_type_parameter_substitution(
                    extension_substitutions, parameter,
                    *type_arguments[type_index++]);
              const janus::ast::FunctionDeclaration::Parameter receiver{
                  "this", resolved.extension->target_type, node.location,
                  resolved.receiver_ownership ==
                          janus::ast::ParameterOwnership::Consume
                      ? janus::ast::ParameterOwnership::Unspecified
                      : resolved.receiver_ownership,
                  false};
              std::vector<::llvm::Value *> arguments;
              arguments.reserve(node.arguments.size() + 1);
              arguments.push_back(
                  emit_parameter_argument(receiver, *node.object, object_type,
                                          substitutions, locals, builder));
              for (std::size_t index = 0; index < node.arguments.size();
                   ++index) {
                const janus::Type &parameter_type =
                    resolve(resolved.method->parameters[index].type,
                            extension_substitutions);
                arguments.push_back(emit_parameter_argument(
                    resolved.method->parameters[index], *node.arguments[index],
                    parameter_type, substitutions, locals, builder));
              }
              return target->getReturnType()->isVoidTy()
                         ? emit_protected_call(target, arguments, builder)
                         : emit_protected_call(target, arguments, builder,
                                               node.method + ".result");
            }
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
                ::llvm::Value *storage = create_entry_alloca(
                    builder, lower_type(object_type, context_),
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
            if (method == nullptr)
              throw janus::CompileError{
                  janus::DiagnosticCode::BackendLegacy, node.location,
                  "backend cannot find semantically validated method '" +
                      std::string{object_type.name()} + "." + node.method +
                      "'"};
            const std::vector<const janus::Type *> method_type_arguments =
                effective_type_arguments(
                    method->type_parameters, node.type_arguments, &expression,
                    substitutions, node.location,
                    std::string{object_type.name()} + "." + node.method);
            ::llvm::Function *target = emit_function(
                *method, method_type_arguments, &class_declaration,
                &specialization.substitutions, object_type.name());
            Substitutions method_substitutions = specialization.substitutions;
            for (std::size_t index = 0; index < method_type_arguments.size();
                 ++index)
              add_type_parameter_substitution(
                  method_substitutions, method->type_parameters[index],
                  *method_type_arguments[index]);
            std::vector<::llvm::Value *> arguments;
            arguments.push_back(object_value);
            for (std::size_t index = 0; index < node.arguments.size();
                 ++index) {
              const janus::Type &parameter_type =
                  resolve(method->parameters[index].type, method_substitutions);
              arguments.push_back(emit_parameter_argument(
                  method->parameters[index], *node.arguments[index],
                  parameter_type, substitutions, locals, builder));
            }
            return target->getReturnType()->isVoidTy()
                       ? emit_protected_call(target, arguments, builder)
                       : emit_protected_call(target, arguments, builder,
                                             node.method + ".result");
          } else if constexpr (std::is_same_v<Node,
                                              janus::ast::IndexExpression>) {
            const janus::Type &object_type = expression_type(
                *node.container, substitutions, locals);
            ::llvm::Value *object_value = emit_expression(
                *node.container, object_type, substitutions, locals, builder);
            if (const auto ownership =
                    analysis_.call_return_ownership.find(node.container.get());
                ownership != analysis_.call_return_ownership.end() &&
                ownership->second ==
                    janus::ast::ReturnOwnership::BorrowMutable)
              object_value = builder.CreateLoad(builder.getPtrTy(), object_value,
                                                "index.mutable.object");
            const ClassSpecialization &specialization =
                class_specializations_.at(std::string{object_type.name()});
            const auto &capabilities =
                analysis_.indexed_capabilities.at(&node);
            ::llvm::Function *target = emit_function(
                *capabilities.read, {}, specialization.declaration,
                &specialization.substitutions, object_type.name());
            ::llvm::Value *index = emit_expression(
                *node.index, janus::Type::usize_type(), substitutions, locals,
                builder);
            return emit_protected_call(target, {object_value, index}, builder,
                                       "index.result");
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
            const janus::Type &match_type =
                expression_type(*node.scrutinee, substitutions, locals);
            const bool simple_enum =
                match_type.kind() == janus::TypeKind::Enum &&
                std::all_of(
                    node.arms.begin(), node.arms.end(), [](const auto &arm) {
                      if (arm.patterns.size() != 1 || arm.guard)
                        return false;
                      const auto &pattern = arm.patterns.front();
                      if (pattern.kind == janus::ast::MatchPattern::Kind::Name)
                        return true;
                      return pattern.kind ==
                                 janus::ast::MatchPattern::Kind::Constructor &&
                             std::all_of(
                                 pattern.children.begin(),
                                 pattern.children.end(), [](const auto &child) {
                                   return child.kind ==
                                          janus::ast::MatchPattern::Kind::Name;
                                 });
                    });
            if (simple_enum) {
              const janus::Type &enum_type = match_type;
              const EnumSpecialization &specialization =
                  enum_specializations_.at(std::string{enum_type.name()});
              ::llvm::Value *scrutinee = emit_expression(
                  *node.scrutinee, enum_type, substitutions, locals, builder);
              ::llvm::Value *tag =
                  builder.CreateExtractValue(scrutinee, 0, "match.tag");
              ::llvm::Function *function =
                  builder.GetInsertBlock()->getParent();
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
                  if (payload_type.kind() == janus::TypeKind::Unit) {
                    arm_locals.insert_or_assign(
                        arm.bindings[index], Local{nullptr, &payload_type});
                    continue;
                  }
                  ::llvm::Value *payload = builder.CreateExtractValue(
                      scrutinee, field++, arm.bindings[index] + ".payload");
                  ::llvm::Value *storage = create_entry_alloca(
                      builder, lower_type(payload_type, context_),
                      arm.bindings[index]);
                  builder.CreateStore(payload, storage);
                  arm_locals.insert_or_assign(arm.bindings[index],
                                              Local{storage, &payload_type});
                }
                ::llvm::Value *arm_value =
                    emit_expression(*arm.expression, expected_type,
                                    substitutions, arm_locals, builder);
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
            }

            ::llvm::Value *scrutinee = emit_expression(
                *node.scrutinee, match_type, substitutions, locals, builder);
            ::llvm::Function *function = builder.GetInsertBlock()->getParent();
            auto *merge_block =
                ::llvm::BasicBlock::Create(context_, "match.end");
            auto *unhandled_block =
                ::llvm::BasicBlock::Create(context_, "match.unhandled");
            std::vector<std::pair<::llvm::Value *, ::llvm::BasicBlock *>>
                incoming;
            ::llvm::Value *tag = nullptr;
            const EnumSpecialization *specialization = nullptr;
            if (match_type.kind() == janus::TypeKind::Enum) {
              specialization =
                  &enum_specializations_.at(std::string{match_type.name()});
              tag = builder.CreateExtractValue(scrutinee, 0, "match.tag");
            }
            struct PatternBinding {
              ::llvm::Value *value;
              const janus::Type *type;
            };
            using PatternBindingMap =
                std::unordered_map<std::string, PatternBinding>;
            std::function<::llvm::Value *(const janus::ast::MatchPattern &,
                                          ::llvm::Value *, const janus::Type &,
                                          PatternBindingMap &)>
                emit_pattern;
            emit_pattern = [&](const janus::ast::MatchPattern &pattern,
                               ::llvm::Value *value, const janus::Type &type,
                               PatternBindingMap &bindings) -> ::llvm::Value * {
              if (pattern.kind == janus::ast::MatchPattern::Kind::Wildcard)
                return builder.getTrue();
              if (type.kind() == janus::TypeKind::Unit) {
                if (pattern.kind == janus::ast::MatchPattern::Kind::Alias) {
                  static_cast<void>(emit_pattern(*pattern.nested, nullptr, type,
                                                 bindings));
                  bindings.insert_or_assign(
                      pattern.name, PatternBinding{nullptr, &type});
                } else if (pattern.kind ==
                           janus::ast::MatchPattern::Kind::Name) {
                  bindings.insert_or_assign(
                      pattern.name, PatternBinding{nullptr, &type});
                }
                return builder.getTrue();
              }
              if (pattern.kind == janus::ast::MatchPattern::Kind::Alias) {
                ::llvm::Value *condition =
                    emit_pattern(*pattern.nested, value, type, bindings);
                bindings.insert_or_assign(pattern.name,
                                          PatternBinding{value, &type});
                return condition;
              }
              if (pattern.literal != nullptr &&
                  type.kind() != janus::TypeKind::Enum) {
                ::llvm::Value *literal = emit_expression(
                    *pattern.literal, type, substitutions, locals, builder);
                return emit_structural_equal(value, literal, type, builder);
              }
              if (pattern.kind == janus::ast::MatchPattern::Kind::Name &&
                  (type.kind() != janus::TypeKind::Enum ||
                   std::none_of(
                       enum_specializations_.at(std::string{type.name()})
                           .declaration->cases.begin(),
                       enum_specializations_.at(std::string{type.name()})
                           .declaration->cases.end(),
                       [&](const auto &candidate) {
                         return candidate.name == pattern.name;
                       }))) {
                bindings.insert_or_assign(pattern.name,
                                          PatternBinding{value, &type});
                return builder.getTrue();
              }
              if (type.kind() == janus::TypeKind::Enum) {
                const EnumSpecialization &nested_specialization =
                    enum_specializations_.at(std::string{type.name()});
                ::llvm::Value *nested_tag =
                    builder.CreateExtractValue(value, 0, "pattern.tag");
                ::llvm::Value *condition =
                    builder.CreateICmpEQ(nested_tag,
                                         builder.getInt32(enum_case_value(
                                             type.name(), pattern.name)),
                                         "pattern.case");
                if (pattern.kind ==
                        janus::ast::MatchPattern::Kind::Constructor ||
                    pattern.kind == janus::ast::MatchPattern::Kind::Literal) {
                  const auto enum_case = std::find_if(
                      nested_specialization.declaration->cases.begin(),
                      nested_specialization.declaration->cases.end(),
                      [&](const auto &candidate) {
                        return candidate.name == pattern.name;
                      });
                  unsigned field =
                      enum_case_payload_start(type.name(), pattern.name);
                  for (std::size_t index = 0; index < pattern.children.size();
                       ++index) {
                    const janus::Type &child_type =
                        resolve(enum_case->payload_types[index],
                                nested_specialization.substitutions);
                    ::llvm::Value *child = nullptr;
                    if (child_type.kind() != janus::TypeKind::Unit)
                      child = builder.CreateExtractValue(
                          value, field++, "pattern.payload");
                    condition = builder.CreateAnd(
                        condition,
                        emit_pattern(pattern.children[index], child, child_type,
                                     bindings),
                        "pattern.and");
                  }
                }
                return condition;
              }
              if ((type.kind() == janus::TypeKind::Class ||
                   type.kind() == janus::TypeKind::Struct) &&
                  pattern.kind == janus::ast::MatchPattern::Kind::Constructor) {
                const ClassSpecialization &nested_specialization =
                    class_specializations_.at(std::string{type.name()});
                ::llvm::Value *condition = builder.getTrue();
                for (std::size_t index = 0; index < pattern.children.size();
                     ++index) {
                  const janus::Type &child_type =
                      resolve(*nested_specialization.declaration
                                   ->constructor_fields[index]
                                   .declared_type,
                              nested_specialization.substitutions);
                  ::llvm::Value *child = builder.CreateExtractValue(
                      value, static_cast<unsigned>(index), "pattern.field");
                  condition = builder.CreateAnd(
                      condition,
                      emit_pattern(pattern.children[index], child, child_type,
                                   bindings),
                      "pattern.and");
                }
                return condition;
              }
              return builder.getFalse();
            };
            for (std::size_t arm_index = 0; arm_index < node.arms.size();
                 ++arm_index) {
              const auto &arm = node.arms[arm_index];
              auto *candidate_block = ::llvm::BasicBlock::Create(
                  context_, "match.candidate", function);
              auto *next_block = arm_index + 1 == node.arms.size()
                                     ? unhandled_block
                                     : ::llvm::BasicBlock::Create(
                                           context_, "match.next", function);
              ::llvm::Value *matches = builder.getFalse();
              std::vector<::llvm::Value *> alternative_conditions;
              std::vector<PatternBindingMap> alternative_bindings;
              for (const auto &pattern : arm.patterns) {
                PatternBindingMap bindings;
                ::llvm::Value *condition =
                    emit_pattern(pattern, scrutinee, match_type, bindings);
                matches = builder.CreateOr(matches, condition, "pattern.or");
                alternative_conditions.push_back(condition);
                alternative_bindings.push_back(std::move(bindings));
              }
              builder.CreateCondBr(matches, candidate_block, next_block);
              builder.SetInsertPoint(candidate_block);
              std::unordered_map<std::string, Local> arm_locals = locals;
              if (!alternative_bindings.empty()) {
                for (const auto &[name, first_binding] :
                     alternative_bindings.front()) {
                  if (first_binding.type->kind() == janus::TypeKind::Unit) {
                    arm_locals.insert_or_assign(
                        name, Local{nullptr, first_binding.type});
                    continue;
                  }
                  ::llvm::Value *selected = first_binding.value;
                  for (std::size_t index = 1;
                       index < alternative_bindings.size(); ++index)
                    selected = builder.CreateSelect(
                        alternative_conditions[index],
                        alternative_bindings[index].at(name).value, selected,
                        name + ".alternative");
                  ::llvm::Value *storage = create_entry_alloca(
                      builder, lower_type(*first_binding.type, context_), name);
                  builder.CreateStore(selected, storage);
                  arm_locals.insert_or_assign(
                      name, Local{storage, first_binding.type});
                }
              }
              auto *body_block = candidate_block;
              if (arm.guard) {
                body_block = ::llvm::BasicBlock::Create(
                    context_, "match.guard.success", function);
                ::llvm::Value *guard =
                    emit_expression(*arm.guard, janus::Type::bool_type(),
                                    substitutions, arm_locals, builder);
                builder.CreateCondBr(guard, body_block, next_block);
                builder.SetInsertPoint(body_block);
              }
              ::llvm::Value *arm_value =
                  emit_expression(*arm.expression, expected_type, substitutions,
                                  arm_locals, builder);
              ::llvm::BasicBlock *arm_end = builder.GetInsertBlock();
              builder.CreateBr(merge_block);
              incoming.emplace_back(arm_value, arm_end);
              if (next_block != unhandled_block)
                builder.SetInsertPoint(next_block);
            }
            function->insert(function->end(), unhandled_block);
            builder.SetInsertPoint(unhandled_block);
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
            const auto protocol = analysis_.try_protocols.find(&node);
            if (protocol == analysis_.try_protocols.end())
              throw std::runtime_error{"missing analyzed Try protocol"};
            const auto protocol_cases = [&](const janus::Type &type,
                                            const janus::Type &output,
                                            const janus::Type &residual) {
              const EnumSpecialization &specialization =
                  enum_specializations_.at(std::string{type.name()});
              std::pair<std::string, std::string> names;
              for (const auto &candidate : specialization.declaration->cases) {
                if (candidate.payload_types.size() == 1) {
                  const janus::Type &payload =
                      resolve(candidate.payload_types.front(),
                              specialization.substitutions);
                  if (&payload == &output && names.first.empty())
                    names.first = candidate.name;
                  else if (&payload == &residual && names.second.empty())
                    names.second = candidate.name;
                } else if (candidate.payload_types.empty() &&
                           residual.kind() == janus::TypeKind::Unit)
                  names.second = candidate.name;
              }
              return names;
            };
            const auto resolve_protocol_type =
                [&](const janus::semantic::SemanticType &type)
                -> const janus::Type & {
              if (!type.is_concrete() && !type.is_class() && !type.is_enum() &&
                  !type.is_pointer() && !type.is_function())
                if (const auto substitution =
                        substitutions.find(type.parameter);
                    substitution != substitutions.end())
                  return *substitution->second;
              return resolve(type);
            };
            const janus::Type &output_type =
                resolve_protocol_type(protocol->second.output_type);
            const janus::Type &residual_type =
                resolve_protocol_type(protocol->second.residual_type);
            std::string success_case = protocol->second.success_case;
            std::string failure_case = protocol->second.failure_case;
            if (success_case.empty() || failure_case.empty()) {
              auto names =
                  protocol_cases(operand_type, output_type, residual_type);
              success_case = std::move(names.first);
              failure_case = std::move(names.second);
            }
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
            std::string return_failure_case =
                protocol->second.return_failure_case;
            if (return_failure_case.empty())
              return_failure_case =
                  protocol_cases(return_type, output_type, residual_type)
                      .second;
            auto *llvm_return_type = ::llvm::cast<::llvm::StructType>(
                lower_type(return_type, context_));
            ::llvm::Value *failure = ::llvm::UndefValue::get(llvm_return_type);
            failure = builder.CreateInsertValue(
                failure,
                builder.getInt32(
                    enum_case_value(return_type.name(), return_failure_case)),
                0, "try.failure.tag");
            if (residual_type.kind() != janus::TypeKind::Unit) {
              ::llvm::Value *error = builder.CreateExtractValue(
                  operand,
                  enum_case_payload_start(operand_type.name(), failure_case),
                  "try.residual");
              failure = builder.CreateInsertValue(
                  failure, error,
                  enum_case_payload_start(return_type.name(),
                                          return_failure_case),
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
            const bool is_shift =
                node.operation == janus::ast::BinaryOperator::ShiftLeft ||
                node.operation == janus::ast::BinaryOperator::ShiftRight;
            if (is_shift) {
              ::llvm::Value *count =
                  emit_expression(*node.right, janus::Type::usize_type(),
                                  substitutions, locals, builder);
              return emit_controlled_binary_operation(
                  node.operation, left, count, operand_type, node.location,
                  builder);
            }
            ::llvm::Value *right = emit_expression(
                *node.right, operand_type, substitutions, locals, builder);
            const bool derived_aggregate_equality =
                (operand_type.kind() == janus::TypeKind::Struct ||
                 operand_type.kind() == janus::TypeKind::Class) ||
                (operand_type.kind() == janus::TypeKind::Enum &&
                 has_derivation(
                     enum_specializations_.at(std::string{operand_type.name()})
                         .declaration->derivations,
                     janus::ast::DerivationKind::Equality));
            if ((node.operation == janus::ast::BinaryOperator::Equal ||
                 node.operation == janus::ast::BinaryOperator::NotEqual) &&
                derived_aggregate_equality) {
              ::llvm::Value *equal =
                  emit_structural_equal(left, right, operand_type, builder);
              return node.operation == janus::ast::BinaryOperator::NotEqual
                         ? builder.CreateNot(equal, "not.equal")
                         : equal;
            }
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
            case janus::ast::BinaryOperator::Subtract:
            case janus::ast::BinaryOperator::Multiply:
            case janus::ast::BinaryOperator::Divide:
            case janus::ast::BinaryOperator::Remainder:
            case janus::ast::BinaryOperator::BitwiseAnd:
            case janus::ast::BinaryOperator::BitwiseXor:
            case janus::ast::BinaryOperator::BitwiseOr:
              return emit_controlled_binary_operation(
                  node.operation, left, right, operand_type, node.location,
                  builder);
            case janus::ast::BinaryOperator::ShiftLeft:
            case janus::ast::BinaryOperator::ShiftRight:
              return nullptr;
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
                      "janus_memcmp",
                      ::llvm::FunctionType::get(builder.getInt32Ty(),
                                                {builder.getPtrTy(),
                                                 builder.getPtrTy(),
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
  std::string source_name_;
  janus::backend::llvm::PanicTraceMode panic_trace_;
  std::unordered_map<std::string, const janus::ast::FunctionDeclaration *>
      functions_;
  std::unordered_set<std::string> ambiguous_function_names_;
  std::vector<const janus::ast::GlobalDeclaration *> global_declarations_;
  std::unordered_map<std::string, const janus::ast::GlobalDeclaration *>
      global_by_key_;
  std::unordered_map<std::string, Local> global_storage_;
  std::unordered_map<std::string, std::string> public_global_keys_;
  const janus::semantic::AnalysisResult &analysis_;
  janus::constant::InitializationPlan initialization_plan_;
  std::unordered_set<std::string> constant_global_keys_;
  std::unordered_map<std::string, int> constant_states_;
  std::unordered_map<std::string, janus::constant::Value> constant_values_;
  ::llvm::Function *global_initializer_{};
  ::llvm::Function *global_finalizer_{};
  ::llvm::Function *panic_finalizer_{};
  std::vector<::llvm::Function *> global_initializers_;
  std::vector<::llvm::Function *> global_finalizers_;
  std::string global_lifecycle_suffix_;
  bool emitting_dependency_lifecycle_{};
  bool force_global_lifecycle_{};
  ::llvm::GlobalVariable *global_initialization_started_{};
  ::llvm::GlobalVariable *global_initialized_count_{};
  ::llvm::GlobalVariable *global_finalization_finished_{};
  std::unordered_map<std::string, const janus::ast::ClassDeclaration *>
      classes_;
  std::unordered_map<std::string, const janus::ast::EnumDeclaration *> enums_;
  std::unordered_set<std::string> scoped_type_names_;
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
  std::size_t panic_cleanup_index_{};
  bool emitting_panic_cleanup_{};
  bool emitting_inline_cleanup_{};
  const janus::Type *active_return_type_{};
  std::optional<std::string> active_module_;
  std::string active_function_;
  std::size_t string_literal_index_{};
  std::size_t lambda_index_{};
  std::optional<std::string> entry_module_;
  bool dependencies_only_{};
  janus::Target target_;
};

} // namespace

namespace janus::backend::llvm {

IrGenerator::IrGenerator(::llvm::LLVMContext &context, Target target) noexcept
    : context_{context}, target_{std::move(target)} {}

std::unique_ptr<::llvm::Module>
IrGenerator::generate(const ast::Program &program, std::string_view module_name,
                      PanicTraceMode panic_trace, bool dependencies_only) {
  semantic::Analyzer analyzer;
  const semantic::AnalysisResult analysis = analyzer.analyze(
      program, semantic::AnalysisOptions{.require_entry_point = false,
                                         .target = target_});
  return Generator{context_,    program,           analysis, module_name,
                   panic_trace, dependencies_only, target_}
      .generate();
}

} // namespace janus::backend::llvm
