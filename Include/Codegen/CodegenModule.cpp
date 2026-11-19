// ---------------------------------------------------------------------------
// CodegenModule.cpp: module-level code generation (see CodegenModule.h)
// ---------------------------------------------------------------------------
#include "Codegen/CodegenModule.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

#include "AST/AST.h"
#include "Codegen/CodegenFunction.h"
#include "Diag/diagengine.h"
#include "Semantics/sema.h"
#include "Types/Typecontext.h"

namespace MemoLang {
namespace {

const FunctionType *signatureOf(const FunctionDef &function) {
  return dynCast<FunctionType>(function.resolvedType());
}

} // namespace

CodegenModule::CodegenModule(llvm::LLVMContext &context,
                             const CodegenInputs &inputs,
                             const CodegenOptions &options,
                             std::string_view moduleName)
    : context_(context), inputs_(inputs), options_(options),
      errorsAtStart_(inputs.diags.errorCount()),
      module_(std::make_unique<llvm::Module>(moduleName, context)) {}

CodegenModule::~CodegenModule() = default;

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------
void CodegenModule::error(SourceRange where, std::string message) {
  inputs_.diags.error(where, std::move(message));
}
// Returns false if no error. Used for modulecheck
bool CodegenModule::hadErrors() const {
  return inputs_.diags.errorCount() != errorsAtStart_;
}

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------
// Accesses Kind and search for types. Corresponding types returns the llvm Type
// alongside context.
llvm::Type *CodegenModule::lowerType(const Type *type) {
  if (!type)
    return nullptr;
  switch (type->kind()) {
  case Type::Kind::Void:
    return llvm::Type::getVoidTy(context_);
  case Type::Kind::Bool:
    return llvm::Type::getInt1Ty(context_);
  case Type::Kind::Int:
    return llvm::Type::getIntNTy(context_,
                                 static_cast<const IntType *>(type)->bits());
  case Type::Kind::Float:
    return static_cast<const FloatType *>(type)->bits() == 32
               ? llvm::Type::getFloatTy(context_)
               : llvm::Type::getDoubleTy(context_);
  // Matrix and string types are not available using LLVM api and must be custom
  // fitted for project
  case Type::Kind::Str:    // const char *
  case Type::Kind::Matrix: // MemoMatrix *
    return llvm::PointerType::getUnqual(context_);
  default:
    return nullptr; // Error, Auto, Function, Pointer, Generic, Vector.
  }
}

llvm::FunctionType *
CodegenModule::lowerFunctionType(const FunctionDef &function) {
  const FunctionType *signature = signatureOf(function);
  if (!signature) {
    error(function.range(),
          "internal error: function '" + function.name() + "' has no type");
    return nullptr;
  }
  std::vector<llvm::Type *> params;
  for (const Type *param : signature->params()) {
    llvm::Type *lowered = lowerType(param);
    if (!lowered || lowered->isVoidTy()) {
      error(function.range(), "code generation doesn't support parameters of "
                              "type " +
                                  Detail::displayOrUnknown(param));
      return nullptr;
    }
    params.push_back(lowered);
  }
  llvm::Type *result = lowerType(signature->returnType());
  if (!result) {
    error(function.range(),
          "code generation doesn't support the return type " +
              Detail::displayOrUnknown(signature->returnType()));
    return nullptr;
  }
  return llvm::FunctionType::get(result, params, false);
}

// ---------------------------------------------------------------------------
// Declarations
// ---------------------------------------------------------------------------
bool CodegenModule::isRuntimeExtern(const FunctionDef &function) {
  if (!function.isExtern())
    return false;
  const std::string &name = function.name();
  std::size_t arity = function.params().size();
  return ((name == "random_matrix" || name == "zeros_matrix") && arity == 2) ||
         ((name == "identity_matrix" || name == "matrix_sum") && arity == 1);
}

llvm::Function *CodegenModule::getOrCreateFunction(std::string_view name,
                                                   llvm::FunctionType *type) {
  std::string key(name);
  if (auto it = functions_.find(key); it != functions_.end())
    return it->second;
  llvm::Function *function = llvm::Function::Create(
      type, llvm::Function::ExternalLinkage, key, module_.get());
  functions_.emplace(std::move(key), function);
  return function;
}

llvm::Function *CodegenModule::functionFor(const FunctionDef &function) const {
  auto it = lowered_.find(&function);
  return it == lowered_.end() ? nullptr : it->second;
}

llvm::GlobalVariable *CodegenModule::globalFor(std::uint32_t symbol) const {
  auto it = globals_.find(symbol);
  return it == globals_.end() ? nullptr : it->second;
}
// For module edeclaration, also looks for memo.---
void CodegenModule::declareModule(const Module &ast) {
  for (const auto &stmt : ast.body) {
    if (stmt->kind() == NodeKind::FunctionDef) {
      const auto &function = static_cast<const FunctionDef &>(*stmt);
      if (isRuntimeExtern(function)) {
        lowered_[&function] = nullptr; // Lowered to runtime calls.
        continue;
      }
      llvm::FunctionType *type = lowerFunctionType(function);
      if (!type)
        continue;
      // Functions are prefixed so they can't clash with C symbols
      // (a `main`, `abs`, ...); externs keep their C name.
      std::string name = function.isExtern() ? function.externName()
                                             : "memo." + function.name();
      llvm::Function *lowered = getOrCreateFunction(name, type);
      if (lowered->getFunctionType() != type) {
        error(function.range(),
              "extern '" + name + "' is declared twice with different types");
        continue;
      }
      if (!function.isExtern()) {
        auto params = function.params();
        for (std::size_t i = 0; i < params.size(); ++i)
          lowered->getArg(static_cast<unsigned>(i))->setName(params[i].name);
      }
      lowered_[&function] = lowered;
      if (function.name() == "main" && !function.isExtern())
        mainFunction_ = &function;
    } else if (stmt->kind() == NodeKind::DeclareVar) {
      // Module-level variables live in globals; memo.init gives them their
      // initial values in source order.
      const auto &decl = static_cast<const DeclareVar &>(*stmt);
      auto id = getSema().declaredSymbol(decl);
      llvm::Type *type = lowerType(decl.resolvedType());
      if (!id || !type || type->isVoidTy()) {
        error(decl.range(), "code generation doesn't support this variable's "
                            "type " +
                                Detail::displayOrUnknown(decl.resolvedType()));
        continue;
      }
      auto *global = new llvm::GlobalVariable(
          *module_, type, /*isConstant=*/false,
          llvm::GlobalValue::InternalLinkage,
          llvm::Constant::getNullValue(type), "memo.g." + decl.name());
      globals_[id->value] = global;
      if (decl.resolvedType()->kind() == Type::Kind::Matrix)
        matrixGlobals_.push_back(global);
    }
  }
}

// ---------------------------------------------------------------------------
// Bodies
// ---------------------------------------------------------------------------
// If the function isn't initalized, use LLVM api for a void type no args
// function
CodegenFunction &CodegenModule::initFunction() {
  if (!init_) {

    auto *type =
        llvm::FunctionType::get(llvm::Type::getVoidTy(context_), false);
    llvm::Function *init = llvm::Function::Create(
        type, llvm::Function::InternalLinkage, "memo.init", module_.get());
    init_ = std::make_unique<CodegenFunction>(*this, init, nullptr);
  }
  return *init_;
}
bool CodegenModule::emitFunction(const FunctionDef &function) {
  if (function.isExtern())
    return true;
  llvm::Function *lowered = functionFor(function);
  if (!lowered)
    return false; // Its signature was already reported.
  std::size_t before = inputs_.diags.errorCount();
  CodegenFunction emitter(*this, lowered, &function);
  emitter.emitPrologue();
  emitter.emitBody(function.body());
  emitter.finish();
  return inputs_.diags.errorCount() == before;
}

void CodegenModule::emitTopLevel(const Statement &statement) {
  if (statement.kind() == NodeKind::FunctionDef) {
    emitFunction(static_cast<const FunctionDef &>(statement));
    return;
  }
  initFunction().emitStatement(statement);
}

// ---------------------------------------------------------------------------
// Runtime functions and constants
// ---------------------------------------------------------------------------
llvm::FunctionCallee CodegenModule::runtime(RuntimeFn fn) {
  // Runs each time a function is found. The runtime function is casted then
  // searched to find the appropriate key.
  int key = static_cast<int>(fn);
  if (auto it = runtimeFns_.find(key); it != runtimeFns_.end())
    return it->second;

  // Type definition
  llvm::Type *voidTy = llvm::Type::getVoidTy(context_);
  llvm::Type *ptr = llvm::PointerType::getUnqual(context_);
  llvm::Type *i32 = llvm::Type::getInt32Ty(context_);
  llvm::Type *i64 = llvm::Type::getInt64Ty(context_);
  llvm::Type *f64 = llvm::Type::getDoubleTy(context_);
  auto type = [](llvm::Type *result, std::vector<llvm::Type *> params) {
    return llvm::FunctionType::get(result, params, false);
  };

  const char *name = nullptr;
  llvm::FunctionType *signature = nullptr;
  bool noReturn = false;
  switch (fn) {
    // Maps each RuntimeFn ( runtime function code calls ) to a signature and
    // type
  case RuntimeFn::Begin:
    name = "memolang_rt_begin";
    signature = type(voidTy, {});
    break;
  case RuntimeFn::End:
    name = "memolang_rt_end";
    signature = type(i32, {});
    break;
  case RuntimeFn::Random:
    name = "memolang_rt_random";
    signature = type(ptr, {i64, i64, i32});
    break;
  case RuntimeFn::Zeros:
    name = "memolang_rt_zeros";
    signature = type(ptr, {i64, i64, i32});
    break;
  case RuntimeFn::Identity:
    name = "memolang_rt_identity";
    signature = type(ptr, {i64, i32});
    break;
  case RuntimeFn::Copy:
    name = "memolang_rt_copy";
    signature = type(ptr, {ptr});
    break;
  case RuntimeFn::Matmul:
    name = "memolang_rt_matmul";
    signature = type(ptr, {ptr, ptr});
    break;
  case RuntimeFn::Add:
    name = "memolang_rt_add";
    signature = type(ptr, {ptr, ptr, i32});
    break;
  case RuntimeFn::Scale:
    name = "memolang_rt_scale";
    signature = type(ptr, {ptr, f64});
    break;
  case RuntimeFn::Sum:
    name = "memolang_rt_sum";
    signature = type(f64, {ptr});
    break;
  case RuntimeFn::Retain:
    name = "memolang_rt_retain";
    signature = type(voidTy, {ptr});
    break;
  case RuntimeFn::Release:
    name = "memolang_rt_release";
    signature = type(voidTy, {ptr});
    break;
  case RuntimeFn::StrConcat:
    name = "memolang_rt_str_concat";
    signature = type(ptr, {ptr, ptr});
    break;
  case RuntimeFn::StrCompare:
    name = "memolang_rt_str_compare";
    signature = type(i32, {ptr, ptr});
    break;
  case RuntimeFn::IPow:
    name = "memolang_rt_ipow";
    signature = type(i64, {i64, i64});
    break;
  case RuntimeFn::Panic:
    name = "memolang_rt_panic";
    signature = type(voidTy, {ptr});
    noReturn = true;
    break;
  case RuntimeFn::ShiftError:
    name = "memolang_rt_shift_error";
    signature = type(voidTy, {i64});
    noReturn = true;
    break;
  case RuntimeFn::PrintInt:
    name = "memolang_rt_print_int";
    signature = type(voidTy, {i64});
    break;
  case RuntimeFn::PrintFloat:
    name = "memolang_rt_print_float";
    signature = type(voidTy, {f64});
    break;
  case RuntimeFn::PrintBool:
    name = "memolang_rt_print_bool";
    signature = type(voidTy, {i32});
    break;
  case RuntimeFn::PrintStr:
    name = "memolang_rt_print_str";
    signature = type(voidTy, {ptr});
    break;
  case RuntimeFn::PrintMatrix:
    name = "memolang_rt_print_matrix";
    signature = type(voidTy, {ptr});
    break;
  case RuntimeFn::PrintNothing:
    name = "memolang_rt_print_nothing";
    signature = type(voidTy, {});
    break;
  }
  llvm::Function *function = llvm::Function::Create(
      signature, llvm::Function::ExternalLinkage, name, module_.get());
  // The runtime turns every error into memolang_rt_panic(); nothing it
  // throws reaches generated code (docs/codegen-internals, runtime ABI).
  function->setDoesNotThrow();
  if (noReturn) {
    function->setDoesNotReturn();
    function->addFnAttr(llvm::Attribute::Cold);
  }
  runtimeFns_[key] = function;
  return function;
}
// String type
llvm::Constant *CodegenModule::stringConstant(std::string_view text) {
  std::string key(text);
  if (auto it = strings_.find(key); it != strings_.end())
    return it->second;
  llvm::Constant *data =
      llvm::ConstantDataArray::getString(context_, key, true);
  auto *global =
      new llvm::GlobalVariable(*module_, data->getType(), /*isConstant=*/true,
                               llvm::GlobalValue::PrivateLinkage, data, ".str");
  global->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
  global->setAlignment(llvm::Align(1));
  strings_.emplace(std::move(key), global);
  return global;
}

// ---------------------------------------------------------------------------
// Finishing
// ---------------------------------------------------------------------------
void CodegenModule::emitEntryPoint(const FunctionDef &mainFunction) {
  if (!mainFunction.params().empty()) {
    error(mainFunction.range(), "main() must not take parameters");
    return;
  }
  llvm::Function *userMain = functionFor(mainFunction);
  if (!userMain)
    return;
  auto *type = llvm::FunctionType::get(llvm::Type::getInt32Ty(context_), false);
  llvm::Function *entry = getOrCreateFunction("main", type);
  llvm::IRBuilder<> builder(llvm::BasicBlock::Create(context_, "entry", entry));

  builder.CreateCall(runtime(RuntimeFn::Begin));
  builder.CreateCall(module_->getFunction("memo.init"));
  llvm::Value *result = builder.CreateCall(userMain);

  const Type *returns =
      dynCast<FunctionType>(mainFunction.resolvedType())->returnType();
  switch (returns->kind()) {
  case Type::Kind::Int: {
    bool isSigned = static_cast<const IntType *>(returns)->isSigned();
    builder.CreateCall(
        runtime(RuntimeFn::PrintInt),
        {builder.CreateIntCast(result, builder.getInt64Ty(), isSigned)});
    break;
  }
  case Type::Kind::Float:
    builder.CreateCall(runtime(RuntimeFn::PrintFloat),
                       {builder.CreateFPExt(result, builder.getDoubleTy())});
    break;
  case Type::Kind::Bool:
    builder.CreateCall(runtime(RuntimeFn::PrintBool),
                       {builder.CreateZExt(result, builder.getInt32Ty())});
    break;
  case Type::Kind::Str:
    builder.CreateCall(runtime(RuntimeFn::PrintStr), {result});
    break;
    // Auto calls mat release
  case Type::Kind::Matrix:
    builder.CreateCall(runtime(RuntimeFn::PrintMatrix), {result});
    builder.CreateCall(runtime(RuntimeFn::Release), {result});
    break;
  default:
    builder.CreateCall(runtime(RuntimeFn::PrintNothing));
    break;
  }
  builder.CreateCall(module_->getFunction("memo.fini"));
  builder.CreateRet(builder.CreateCall(runtime(RuntimeFn::End)));

// Marks generated functions (memo.*, the entry point) nounwind 
// call something that may unwind, until nothing changes.
  llvm::SmallPtrSet<const llvm::Function *, 32> mayUnwind;
  auto callMayUnwind = [&](const llvm::CallBase &call) {
    if (call.doesNotThrow()) // Call-site or callee attribute.
      return false;
    const llvm::Function *callee = call.getCalledFunction();
    if (!callee || callee->isDeclaration())
      return true;
    return mayUnwind.contains(callee);
  };
  auto anyCallMayUnwind = [&](llvm::Function &function) {
    for (llvm::Instruction &inst : llvm::instructions(function))
      if (auto *call = llvm::dyn_cast<llvm::CallBase>(&inst);
          call && callMayUnwind(*call))
        return true;
    return false;
  };

  for (bool changed = true; changed;) {
    changed = false;
    for (llvm::Function &function : *module_)
      if (!function.isDeclaration() && !mayUnwind.contains(&function) &&
          anyCallMayUnwind(function)) {
        mayUnwind.insert(&function);
        changed = true;
      }
  }
 // Sets
  for (llvm::Function &function : *module_) {
    if (function.isDeclaration())
      continue;
    if (!mayUnwind.contains(&function))
      function.setDoesNotThrow();
    for (llvm::Instruction &inst : llvm::instructions(function))
      if (auto *call = llvm::dyn_cast<llvm::CallBase>(&inst);
          call && !callMayUnwind(*call))
        call->setDoesNotThrow();
  }
}

void markNoUnwind(llvm::Module &Module) {
  for (llvm::Function &F : Module) {
    if (F.isDeclaration())
      continue;               // don't assert things about external code
    F.setDoesNotThrow();
  }
}

}

// Marks release for module when called ( use paired with consume flags )
void MemoLang::CodegenModule::release() {
  if (released_)
    return;
  released_ = true;


  // memo.init: the module-level code.
  initFunction().finish();

  // memo.fini: module-level matrices are released when the program ends.
  auto *voidFn =
      llvm::FunctionType::get(llvm::Type::getVoidTy(context_), false);
  llvm::Function *fini = llvm::Function::Create(
      voidFn, llvm::Function::InternalLinkage, "memo.fini", module_.get());
  {
    llvm::IRBuilder<> builder(
        llvm::BasicBlock::Create(context_, "entry", fini));
    for (llvm::GlobalVariable *global : matrixGlobals_) {
      llvm::Value *matrix = builder.CreateLoad(builder.getPtrTy(), global);
      builder.CreateCall(runtime(RuntimeFn::Release), {matrix});
      builder.CreateStore(llvm::Constant::getNullValue(builder.getPtrTy()),
                          global);
    }
    builder.CreateRetVoid();
  }

  if (options_.emitEntryPoint && mainFunction_)
    emitEntryPoint(*mainFunction_);

  markNoUnwind(*module_);
  if (options_.verify && !hadErrors()) {
    std::string problems;
    llvm::raw_string_ostream out(problems);
    if (llvm::verifyModule(*module_, &out))
      error(SourceRange{}, "internal error: code generation produced invalid "
                           "LLVM IR:\n" +
                               problems);
  }
} // namespace MemoLang

std::unique_ptr<llvm::Module> MemoLang::CodegenModule::takeModule() {
  return std::move(module_);
}

