#include "Codegen/Linker.h"

#include <llvm/IR/Module.h>
#include <llvm/Linker/Linker.h>

#include "Diag/diagengine.h"

namespace MemoLang {

Linker::Linker(llvm::Module &destination, DiagnosticEngine &diagnostics)
    : destination_(destination), diagnostics_(diagnostics) {}

bool Linker::linkInModule(std::unique_ptr<llvm::Module> source) {
  if (!source)
    return true;
  std::string name = source->getModuleIdentifier();
  // LLVM reports the details (e.g. "symbol multiply defined") through the
  // context's diagnostic handler (BackendDiagnostics); this adds which
  // modules were involved.
  if (::llvm::Linker::linkModules(destination_, std::move(source))) {
    diagnostics_.error(SourceRange{}, "failed to link module '" + name +
                                          "' into '" +
                                          destination_.getModuleIdentifier() +
                                          "'");
    return false;
  }
  return true;
}

} // namespace MemoLang
