#include "Codegen/ModuleBuilder.h"

#include <llvm/IR/Module.h>

#include "AST/AST.h"
#include "Diag/diagengine.h"

namespace MemoLang {

ModuleBuilder::ModuleBuilder(llvm::LLVMContext &context,
                             const CodegenInputs &inputs,
                             const CodegenOptions &options,
                             std::string moduleName,
                             const llvm::TargetMachine *target)
    : diagnostics_(inputs.diags), moduleName_(std::move(moduleName)),
      codegen_(std::make_unique<CodegenModule>(context, inputs, options,
                                               moduleName_, target)) {}

ModuleBuilder::~ModuleBuilder() = default;
void ModuleBuilder::prepareImportedModule(const Module &module) {
  if(std::ranges::find(importedFiles_, &module) != importedFiles_.end()) {
    return;
  }
  importedFiles_.push_back(&module);
  codegen_->declareModule(module);
}

bool ModuleBuilder::handleModule(const Module &ast,
                                 const std::vector<const Module *> &imported) {
  ModuleBuilder::importedFiles_ = imported;
  codegen_->declareModule(ast);
  std::size_t before = diagnostics_.errorCount();
  // Declare everything first, so any body can call any function.
  for (const Module *module : imported)
    codegen_->declareModule(*module);
  codegen_->declareModule(ast); // Last, so its main() is the entry point.
  for (const Module *module : imported)
    for (const auto &stmt : module->body)
  // Emits the top level expression based on the statement.
  // Reminder here that codegen_ denotes the current value for generation
      codegen_->emitTopLevel(*stmt);
  for (const auto &stmt : ast.body)
    codegen_->emitTopLevel(*stmt);
  return diagnostics_.errorCount() == before;
}

void ModuleBuilder::handleTranslationUnit() { codegen_->release(); }

llvm::Module *ModuleBuilder::getModule() const { return codegen_->getModule(); }

std::unique_ptr<llvm::Module> ModuleBuilder::releaseModule() {
  return codegen_->takeModule();
}


} // namespace MemoLang
