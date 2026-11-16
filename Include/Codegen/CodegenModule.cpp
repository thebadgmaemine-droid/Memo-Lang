#include "Codegen/CodegenModule.h"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

namespace MemoLang {

CodegenModule::CodegenModule(llvm::LLVMContext &context, TypeContext &types,
                             DiagnosticEngine &diagnostics,
                             std::string_view moduleName)
    : context_(context), types_(types), diagnostics_(diagnostics),
      module_(std::make_unique<llvm::Module>(moduleName, context)) {}

CodegenModule::~CodegenModule() = default;

void CodegenModule::emitTopLevel(const Statement &) {}

bool CodegenModule::emitFunction(const FunctionDef &) { return false; }

llvm::Function *CodegenModule::getOrCreateFunction(std::string_view,
                                                   llvm::FunctionType *) {
  return nullptr;
}

llvm::Type *CodegenModule::lowerType(const Type *) { return nullptr; }

void CodegenModule::release() {}

std::unique_ptr<llvm::Module> CodegenModule::takeModule() {
  return std::move(module_);
}

} // namespace MemoLang
