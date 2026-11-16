#include "Codegen/CodegenAction.h"

#include <llvm/IR/Module.h>

namespace MemoLang {

// Constructor
CodegenAction::CodegenAction(BackendAction action, llvm::LLVMContext &context,
                             TypeContext &types, DiagnosticEngine &diagnostics)
    : action_(action), context_(context), types_(types),
      diagnostics_(diagnostics) {}
// Default state 
CodegenAction::~CodegenAction() = default;
// Intakes a module and a string ( preferably of a mutable type ). The code execution then takes in AST context, Functionblock and use the LLVM api
bool CodegenAction::execute(const Module &, const std::string &) {
  return false;
}
//
std::unique_ptr<llvm::Module> CodegenAction::takeModule() {
  return std::move(module_);
}
/* 
Emitter go from AssemblyAction -> CodeGenAction -> ASTFrontEndAction -> FrontendAction
*/



bool CodegenAction::emitOutput(llvm::Module &, const std::string &) {
  return false;
}
class EmitAssemblyAction : public CodegenAction {
  virtual void anchor();
public:
  EmitAssemblyAction(llvm::LLVMContext *_VMContext = nullptr);
};

class EmitBCAction : public CodegenAction {
  virtual void anchor();
public:
  EmitBCAction(llvm::LLVMContext *_VMContext = nullptr);
};

class EmitLLVMAction : public CodegenAction {
  virtual void anchor();
public:
  EmitLLVMAction(llvm::LLVMContext *_VMContext = nullptr);
};

class EmitLLVMOnlyAction : public CodegenAction {
  virtual void anchor();
public:
  EmitLLVMOnlyAction(llvm::LLVMContext *_VMContext = nullptr);
};

class EmitCodeGenOnlyAction : public CodegenAction {
  virtual void anchor();
public:
  EmitCodeGenOnlyAction(llvm::LLVMContext *_VMContext = nullptr);
};

} // namespace MemoLang
