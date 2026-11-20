#ifndef CODEGEN_ACTION_H_
#define CODEGEN_ACTION_H_

//---------------------------------------------------------------------------
// Header file for forward declaration of Back-end actions.
// Actions are split into three catagories: 
// + No output: Output discarded, used for testing end-to-end
// + Emit LLVM: Emit LLVM IR, used for testing
// + Emit bitcode: Emit bitcode
// + Emit object: .o file
// + Emit codegen only: Runs the backend, so raw output to test code generation.
//---------------------------------------------------------------------------
// What the driver asks code generation to produce, and the action that runs
// the whole back end for one input: lower the AST (ModuleBuilder), set the
// target, optimize, and write the output.
//
// 2026-11-17: implemented. The Emit*Action subclasses drafted in
// CodegenAction.cpp (clang's naming) are here, as shorthands that fix the
// BackendAction.
#include <memory>
#include <string>
#include <string_view>

#include "Diag/BackendDiagnostics.h"
#include "Codegen/CodegenModule.h"
#include "Codegen/CodegenOptions.h"

namespace llvm {
class LLVMContext;
class Module;
class TargetMachine;
} // namespace llvm

namespace MemoLang {
struct Module; // One parsed file (AST.h).

enum class BackendAction {
  EmitNothing,     // Lower to IR only; takeModule() returns it (tests, JIT).
  EmitLLVM,        // Textual IR (.ll).
  EmitBitcode,     // Bitcode (.bc).
  EmitAssembly,    // Target assembly (.s).
  EmitObject,      // Object file (.o).
  EmitCodeGenOnly, // Run the whole back end, discard the output.
};

class CodegenAction {
public:
  CodegenAction(BackendAction action, llvm::LLVMContext &context,
                const CodegenInputs &inputs, CodegenOptions options = {});
  ~CodegenAction();
  CodegenAction(const CodegenAction &) = delete;
  CodegenAction &operator=(const CodegenAction &) = delete;

  // Lowers `ast` and writes the result to `outputPath` ("-" = stdout;
  // ignored for EmitNothing / EmitCodeGenOnly). Returns false if errors
  // were reported.
  // Takes in output path and the module to output the corresponding module to the output.
  // Execution takes the module parameter and runs it through:
  // + BackendDiagnostgic/LLVMContext(If MemoLang back end is not available)
  // + prepareTarget(): the target machine, created first
  // + ModuleBuilder: handleModule() -> handleTranslationUnit(), with the
  //   target's triple and data layout already on the module
  bool execute(const Module &ast, const std::string &outputPath);

  [[nodiscard]] BackendAction getAction() const noexcept { return action_; }
  // The generated module, after execute().
  [[nodiscard]] std::unique_ptr<llvm::Module> takeModule();

  // The output file for `inputPath`: examples/a.memo -> a.ll, a.bc, a.s, a.o.
  [[nodiscard]] static std::string defaultOutputPath(std::string_view inputPath,
                                                     BackendAction action);

private:
  // Creates targetMachine_. ModuleBuilder stamps its triple and data layout
  // on the module before lowering.
  bool prepareTarget();
  void optimize(llvm::Module &module);
  bool emitOutput(llvm::Module &module, const std::string &outputPath);
// action_ determines emit success
  BackendAction action_;
  llvm::LLVMContext &context_;
  CodegenInputs inputs_;
  CodegenOptions options_;
  BackendDiagnostics backendDiagnostics_;
  std::unique_ptr<llvm::TargetMachine> targetMachine_;
  std::unique_ptr<llvm::Module> module_;
};

// Shorthands, after clang's FrontendActions.
// Constructors for CodeGenerationActions.

// Forward declaration for Assembly emitter to default/changed file path
class EmitAssemblyAction : public CodegenAction {
public:
  EmitAssemblyAction(llvm::LLVMContext &context, const CodegenInputs &inputs,
                     CodegenOptions options = {})
      : CodegenAction(BackendAction::EmitAssembly, context, inputs,
                      std::move(options)) {}
};
// Forward declaration for Bitcode emitter to default/changed file path

class EmitBCAction : public CodegenAction {
public:
  EmitBCAction(llvm::LLVMContext &context, const CodegenInputs &inputs,
               CodegenOptions options = {})
      : CodegenAction(BackendAction::EmitBitcode, context, inputs,
                      std::move(options)) {}
};
// Forward declaration for LLVMIR emitter to default/changed file path

class EmitLLVMAction : public CodegenAction {
public:
  EmitLLVMAction(llvm::LLVMContext &context, const CodegenInputs &inputs,
                 CodegenOptions options = {})
      : CodegenAction(BackendAction::EmitLLVM, context, inputs,
                      std::move(options)) {}
};
// Forward declaration for Only LLVM emitter to default/changed file path

class EmitLLVMOnlyAction : public CodegenAction {
public:
  EmitLLVMOnlyAction(llvm::LLVMContext &context, const CodegenInputs &inputs,
                     CodegenOptions options = {})
      : CodegenAction(BackendAction::EmitNothing, context, inputs,
                      std::move(options)) {}
};
// Forward declaration for only codegeneration emitter to default/changed file path

class EmitCodeGenOnlyAction : public CodegenAction {
public:
  EmitCodeGenOnlyAction(llvm::LLVMContext &context, const CodegenInputs &inputs,
                        CodegenOptions options = {})
      : CodegenAction(BackendAction::EmitCodeGenOnly, context, inputs,
                      std::move(options)) {}
};
// Forward declaration for .so files emitter to default/changed file path

class EmitObjAction : public CodegenAction {
public:
  EmitObjAction(llvm::LLVMContext &context, const CodegenInputs &inputs,
                CodegenOptions options = {})
      : CodegenAction(BackendAction::EmitObject, context, inputs,
                      std::move(options)) {}
};

} // namespace MemoLang
#endif // CODEGEN_ACTION_H_
