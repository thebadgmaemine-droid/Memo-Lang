#ifndef MODULE_BUILDER_H_
#define MODULE_BUILDER_H_
// Entry point from the front-end into code generation: takes parsed and
// checked modules (AST.h) and drives a CodegenModule over them.
// Not implemented yet (template).
#include <memory>
#include <string>
#include <vector>

namespace llvm {
class LLVMContext;
class Module;
} // namespace llvm

namespace MemoLang {
class CodegenModule;
class DiagnosticEngine;
class TypeContext;
struct Module; // One parsed file (AST.h).

class ModuleBuilder {
public:
  ModuleBuilder(llvm::LLVMContext &context, TypeContext &types,
                DiagnosticEngine &diagnostics, std::string moduleName);
  ~ModuleBuilder();
  ModuleBuilder(const ModuleBuilder &) = delete;
  ModuleBuilder &operator=(const ModuleBuilder &) = delete;

  // Lowers `ast` (and the modules it imports). Returns false on errors.
  bool handleModule(const Module &ast,
                    const std::vector<const Module *> &imported = {});
  // Called once every module has been handled.
  void handleTranslationUnit();

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
