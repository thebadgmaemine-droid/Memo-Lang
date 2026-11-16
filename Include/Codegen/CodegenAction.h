#ifndef CODEGEN_ACTION_H_
#define CODEGEN_ACTION_H_
// What the driver asks code generation to produce, and the action that runs
// the whole back end for one input. Not implemented yet (template).
#include <memory>
#include <string>

namespace llvm {
class LLVMContext;
class Module;
} // namespace llvm

namespace MemoLang {
class DiagnosticEngine;
class TypeContext;
struct Module; // One parsed file (AST.h).

enum class BackendAction {
  EmitNothing,  // Run codegen only (for tests).
  EmitLLVM,     // Textual IR (.ll).
  EmitBitcode,  // Bitcode (.bc).
  EmitAssembly, // Target assembly (.s).
  EmitObject,   // Object file (.o).
};

class CodegenAction {
public:
  CodegenAction(BackendAction action, llvm::LLVMContext &context,
                TypeContext &types, DiagnosticEngine &diagnostics);
  ~CodegenAction();
  CodegenAction(const CodegenAction &) = delete;
  CodegenAction &operator=(const CodegenAction &) = delete;

  // Lowers `ast` and writes the result to `outputPath` ("-" = stdout).
  // Returns false if errors were reported.
  bool execute(const Module &ast, const std::string &outputPath);

  [[nodiscard]] BackendAction getAction() const noexcept { return action_; }
  // The generated module, for EmitNothing / tests.
  [[nodiscard]] std::unique_ptr<llvm::Module> takeModule();

private:
  bool emitOutput(llvm::Module &module, const std::string &outputPath);

  BackendAction action_;
  llvm::LLVMContext &context_;
  TypeContext &types_;
  DiagnosticEngine &diagnostics_;
  std::unique_ptr<llvm::Module> module_;
};

} // namespace MemoLang
#endif // CODEGEN_ACTION_H_
