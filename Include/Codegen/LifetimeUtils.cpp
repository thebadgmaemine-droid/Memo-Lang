#include "Codegen/LifetimeUtils.h"

#include "Codegen/CodegenModule.h"

namespace MemoLang {

void emitLifetimeStart(CodegenModule &, llvm::AllocaInst *, std::uint64_t) {}

void emitLifetimeEnd(CodegenModule &, llvm::AllocaInst *, std::uint64_t) {}

void LifetimeScope::track(llvm::AllocaInst *alloca, std::uint64_t sizeInBytes) {
  tracked_.push_back({alloca, sizeInBytes});
}

void LifetimeScope::emitCleanups(llvm::BasicBlock *) {}

} // namespace MemoLang
