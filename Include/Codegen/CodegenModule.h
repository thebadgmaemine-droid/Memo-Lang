#ifndef CODEGEN_MODULE_H_
#define CODEGEN_MODULE_H_
// Per-module code generation state: owns the llvm::Module being built and
// keeps the function table, so every emitter shares one place to look up
// what has already been lowered. Not implemented yet (template).
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace llvm {
class Function;
class FunctionType;
class LLVMContext;
class Module;
class Type;
} // namespace llvm

namespace MemoLang {
class DiagnosticEngine;
class FunctionDef; // AST.h
class Statement;   // AST.h
class Type;        // Semantic type (Types/Typecontext.h).
class TypeContext;

class CodegenModule {
public:
  CodegenModule(llvm::LLVMContext &context, TypeContext &types,
                DiagnosticEngine &diagnostics, std::string_view moduleName);
  ~CodegenModule(); // Out of line: llvm::Module is incomplete here.
  CodegenModule(const CodegenModule &) = delete;
  CodegenModule &operator=(const CodegenModule &) = delete;

  [[nodiscard]] llvm::LLVMContext &getLLVMContext() const { return context_; }
  [[nodiscard]] TypeContext &getTypes() const { return types_; }
  [[nodiscard]] DiagnosticEngine &getDiags() const { return diagnostics_; }
  [[nodiscard]] llvm::Module *getModule() const { return module_.get(); }

  // Lowers one top-level statement (function, global, extern).
  void emitTopLevel(const Statement &statement);
  // Emits the body of `function`. Returns false if errors were reported.
  bool emitFunction(const FunctionDef &function);

  // Returns the llvm::Function for `name`, declaring it if it doesn't exist.
  llvm::Function *getOrCreateFunction(std::string_view name,
                                      llvm::FunctionType *type);
  // Maps a semantic type to its LLVM type (null if it can't be lowered).
  llvm::Type *lowerType(const Type *type);

  // Finishes the module (verification, metadata). Call once, after emitting.
  void release();
  // Hands the finished module to the caller; getModule() is null afterwards.
  [[nodiscard]] std::unique_ptr<llvm::Module> takeModule();

private:
  llvm::LLVMContext &context_;
  TypeContext &types_;
  DiagnosticEngine &diagnostics_;
  std::unique_ptr<llvm::Module> module_;
  std::unordered_map<std::string, llvm::Function *> functions_{};
};

} // namespace MemoLang
#endif // CODEGEN_MODULE_H_
