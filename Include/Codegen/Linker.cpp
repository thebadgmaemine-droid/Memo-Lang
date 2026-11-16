#include "Codegen/Linker.h"

#include <llvm/IR/Module.h>

namespace MemoLang {

Linker::Linker(llvm::Module &destination, DiagnosticEngine &diagnostics)
    : destination_(destination), diagnostics_(diagnostics) {}

bool Linker::linkInModule(std::unique_ptr<llvm::Module>) { return false; }

} // namespace MemoLang
