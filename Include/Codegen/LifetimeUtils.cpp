#include "Codegen/LifetimeUtils.h"

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>

#include "Codegen/CodegenModule.h"

namespace MemoLang {

void emitLifetimeStart(llvm::IRBuilderBase &builder, llvm::AllocaInst *alloca) {
  builder.CreateLifetimeStart(alloca);
}

void emitLifetimeEnd(llvm::IRBuilderBase &builder, llvm::AllocaInst *alloca) {
  builder.CreateLifetimeEnd(alloca);
}

void LifetimeScope::track(llvm::AllocaInst *slot, bool ownsMatrix) {
  tracked_.push_back({slot, ownsMatrix});
}

void LifetimeScope::emitCleanups(llvm::IRBuilderBase &builder) const {
  for (auto it = tracked_.rbegin(); it != tracked_.rend(); ++it) {
    if (it->ownsMatrix) {
      llvm::Value *matrix =
          builder.CreateLoad(builder.getPtrTy(), it->slot, "dead");
      builder.CreateCall(cgm_.runtime(RuntimeFn::Release), {matrix});
    }
    emitLifetimeEnd(builder, it->slot);
  }
}

} // namespace MemoLang
