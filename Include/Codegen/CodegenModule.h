#ifndef CODEGEN_MODULE_H_
#define CODEGEN_MODULE_H_
// Per-module code generation state: owns the llvm::Module being built, the
// table of lowered functions and module-level variables, and the runtime
// functions generated code calls (Runtime/runtime_abi.h). Function bodies are
// lowered by CodegenFunction.
//
// Layout of a generated module:
//   memo.<name>   one function per MemoLang function (external linkage);
//                 extern functions keep their own name and the C ABI.
//   memo.init     runs the module-level statements, in order.
//   memo.fini     releases the module-level matrices.
//   main          (CodegenOptions::emitEntryPoint) begin the runtime, init,
//                 call MemoLang's main(), print what it returned, fini, end.
//
// 2026-11-17: implemented.
#include <cstdint>
#include <llvm-23/llvm/IR/DerivedTypes.h>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Codegen/CodegenOptions.h"
#include "Source/SourceLocation.h"

namespace llvm {
class TargetMachine;
class Constant;
class Function;
class FunctionCallee;
class FunctionType;
class GlobalVariable;
class LLVMContext;
class Module;
class Type;
} // namespace llvm

namespace MemoLang {
class CodegenFunction;
class Destructive;
class DiagnosticEngine;
class FunctionDef; // AST.h
class Statement;   // AST.h
class Type;        // Semantic type (Types/Typecontext.h).
class TypeContext;
struct Module; // One parsed file (AST.h).
namespace sema {
class SemanticAnalyzer;
}

// What code generation reads from the front end. Semantic and destructive
// analysis must have run on the module without errors.
// (ASTContext is meant to own all of this; until then it's passed as a bundle.)
struct CodegenInputs {
  TypeContext &types;
  DiagnosticEngine &diags;
  const sema::SemanticAnalyzer &sema;
  const Destructive &destructive;
};

// The runtime functions generated code calls (see Runtime/runtime_abi.h).
enum class RuntimeFn {
  Begin,
  End,
  Random,
  Zeros,
  Identity,
  Copy,
  Matmul,
  Add,
  Scale,
  Sum,
  Retain,
  Release,
  StrConcat,
  StrCompare,
  IPow,
  Panic,
  ShiftError,
  PrintInt,
  PrintFloat,
  PrintBool,
  PrintStr,
  PrintMatrix,
  PrintNothing,
};

class CodegenModule {
public:
  CodegenModule(llvm::LLVMContext &context, const CodegenInputs &inputs,
                const CodegenOptions &options, std::string_view moduleName,
                const llvm::TargetMachine *target = nullptr);
  ~CodegenModule(); // Out of line: llvm::Module is incomplete here.
  CodegenModule(const CodegenModule &) = delete;
  CodegenModule &operator=(const CodegenModule &) = delete;

  [[nodiscard]] llvm::LLVMContext &getLLVMContext() const { return context_; }
  [[nodiscard]] TypeContext &getTypes() const { return inputs_.types; }
  [[nodiscard]] DiagnosticEngine &getDiags() const { return inputs_.diags; }
  [[nodiscard]]  const sema::SemanticAnalyzer &getSema() const {
    return inputs_.sema;
  }
  [[nodiscard]] const Destructive &getDestructive() const {
    return inputs_.destructive;
  }
  [[nodiscard]] const CodegenOptions &getOptions() const { return options_; }
  [[nodiscard]] llvm::Module *getModule() const { return module_.get(); }

  // Declares every function and module-level variable of `ast`, so bodies
  // can refer to them in any order. Call before emitTopLevel().
  void declareModule(const Module &ast);
  // Lowers one top-level statement: a function body, or module-level code
  // (appended to memo.init).
  void emitTopLevel(const Statement &statement);
  // Emits the body of `function`. Returns false if errors were reported.
  bool emitFunction(const FunctionDef &function);

  // Returns the llvm::Function for `name`, declaring it if it doesn't exist.
  llvm::Function *getOrCreateFunction(std::string_view name,
                                      llvm::FunctionType *type);
  // The function `function` was declared as (null for built-in externs,
  // which lower to runtime calls).
  [[nodiscard]] llvm::Function *functionFor(const FunctionDef &function) const;
  // The global for a module-level variable, or null if `symbol` isn't one.
  [[nodiscard]] llvm::GlobalVariable *globalFor(std::uint32_t symbol) const;
  // Maps a semantic type to its LLVM type (null if it can't be lowered).
  llvm::Type *lowerType(const Type *type);
  // An LLVM function type for `function` (null after reporting an error).
  llvm::FunctionType *lowerFunctionType(const FunctionDef &function);
  // Declares (once) and returns a runtime function.
  llvm::FunctionCallee runtime(RuntimeFn fn);
  // A private constant for a string literal (shared by equal literals).
  llvm::Constant *stringConstant(std::string_view text);  
  // Nested functions struct to capture value pointers incase of mutations
  llvm::StructType *capturedVariables(llvm::PointerType Capturednest);
  // Nested function type
llvm::FunctionType* nestedFunctions(llvm::StructType* EnvTy,
                                    const llvm::FunctionType& OrigFnTy);
  // Is `function` one of the externs the runtime implements (random_matrix,
  // zeros_matrix, identity_matrix, matrix_sum)?
  [[nodiscard]] static bool isRuntimeExtern(const FunctionDef &function);

  void error(SourceRange where, std::string message);
  [[nodiscard]] bool hadErrors() const;

  // Finishes the module: closes memo.init, emits memo.fini and the entry
  // point, and verifies. Call once, after emitting.
  void release();
  // Hands the finished module to the caller; getModule() is null afterwards.
  [[nodiscard]] std::unique_ptr<llvm::Module> takeModule();
  void markNoUnwind(llvm::Module &Module);

private:
  void emitEntryPoint(const FunctionDef &mainFunction);
  CodegenFunction &initFunction();

  llvm::LLVMContext &context_;
  CodegenInputs inputs_;
  CodegenOptions options_;
  std::size_t errorsAtStart_ = 0;
  std::unique_ptr<llvm::Module> module_;
  std::unordered_map<std::string, llvm::Function *> functions_{};
  std::unordered_map<const FunctionDef *, llvm::Function *> lowered_{};
  std::unordered_map<std::uint32_t, llvm::GlobalVariable *> globals_{};
  std::vector<llvm::GlobalVariable *> matrixGlobals_{}; // Released by fini.
  std::unordered_map<std::string, llvm::Constant *> strings_{};
  std::unordered_map<int, llvm::Function *> runtimeFns_{};
  std::unique_ptr<CodegenFunction> init_;
  const FunctionDef *mainFunction_ = nullptr;
  bool released_ = false;
};

} // namespace MemoLang
#endif // CODEGEN_MODULE_H_
