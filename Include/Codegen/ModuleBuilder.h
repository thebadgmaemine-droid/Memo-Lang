#ifndef MODULE_BUILDER_H_
#define MODULE_BUILDER_H_
//----------------------------------------------------------------------
// ModuleBuilder header files stores the templates for modules
// As well as attributes used by the design.
//----------------------------------------------------------------------
// Entry point from the front-end into code generation: takes parsed and
// checked modules (AST.h) and drives a CodegenModule over them.
//
// 2026-11-17: implemented.
//----------------------------------------------------------------------
#include <memory>
#include <string>
#include <vector>
#include "Codegen/CodegenModule.h"
namespace llvm {
class LLVMContext;
class Module;
class TargetMachine;
} // namespace llvm
namespace MemoLang {
struct Module; // One parsed file (AST.h).
class ModuleBuilder {
public:
  ModuleBuilder(llvm::LLVMContext &context, const CodegenInputs &inputs,
                const CodegenOptions &options, std::string moduleName,
                const llvm::TargetMachine *target = nullptr);
  ~ModuleBuilder();
  ModuleBuilder(const ModuleBuilder &) = delete;
  ModuleBuilder &operator=(const ModuleBuilder &) = delete;
  //----------------------------------------------------------------------
  // Lowers `ast`, and before it the modules it imports, into one LLVM
  // module. The imported modules must have been analyzed by the same
  // semantic analyzer. Their module-level code runs first, in the order
  // given; the entry point calls `ast`'s main(). Returns false on errors.
  //----------------------------------------------------------------------
  bool handleModule(const Module &ast,
                    const std::vector<const Module *> &imported = {});
  // Called once every module has been handled: finishes and verifies.
  void handleTranslationUnit();
 // Getter for codegenModule
  [[nodiscard]] CodegenModule &getCodegenModule() const { return *codegen_; }
  [[nodiscard]] llvm::Module *getModule() const;
  // Hands the finished module to the caller.
  [[nodiscard]] std::unique_ptr<llvm::Module> releaseModule();

private:
  DiagnosticEngine &diagnostics_;
  std::string moduleName_;
  std::unique_ptr<CodegenModule> codegen_;
};

} // namespace MemoLang
#endif // MODULE_BUILDER_H_
