#include "Codegen/ModuleBuilder.h"

#include "Codegen/CodegenModule.h"

#include <llvm/IR/Module.h>

namespace MemoLang {

ModuleBuilder::ModuleBuilder(llvm::LLVMContext &context, TypeContext &types,
                             DiagnosticEngine &diagnostics,
                             std::string moduleName)
    : diagnostics_(diagnostics), moduleName_(std::move(moduleName)),
      codegen_(std::make_unique<CodegenModule>(context, types, diagnostics,
                                               moduleName_)) {}

ModuleBuilder::~ModuleBuilder() = default;

bool ModuleBuilder::handleModule(const Module &,
                                 const std::vector<const Module *> &) {
  return false;
}

void ModuleBuilder::handleTranslationUnit() {}

llvm::Module *ModuleBuilder::getModule() const { return codegen_->getModule(); }

std::unique_ptr<llvm::Module> ModuleBuilder::releaseModule() {
  return codegen_->takeModule();
}

} // namespace MemoLang
