#ifndef LIFETIMEUTILS_H_
#define LIFETIMEUTILS_H_
//--------------------------------------------------------------------------
// Lifetimes of stack values: llvm.lifetime.start/end markers, and the
// cleanups that run when a lexical scope ends.
//
// A scope records its variables in declaration order. When it ends (falling
// off the end, `return`, `break`) its cleanups run in reverse: matrix
// variables release the reference they still hold (a consumed variable holds
// null, and releasing null does nothing), then each stack slot's lifetime
// ends. A `return` or `break` runs the cleanups of every scope it leaves,
// innermost first, without closing them, since code after the jump in the
// same scope still uses them.
//
// 2026-11-17: implemented. LLVM 22 dropped the size operand of the lifetime
// intrinsics, so the helpers no longer take one.
//--------------------------------------------------------------------------
#include <vector>

namespace llvm {
class AllocaInst;
class IRBuilderBase;
} // namespace llvm

namespace MemoLang {
class CodegenModule;

// Marks the start / end of `alloca`'s lifetime at the builder's position.
void emitLifetimeStart(llvm::IRBuilderBase &builder, llvm::AllocaInst *alloca);
void emitLifetimeEnd(llvm::IRBuilderBase &builder, llvm::AllocaInst *alloca);

class LifetimeScope {
public:
  explicit LifetimeScope(CodegenModule &cgm) : cgm_(cgm) {}

  // Registers a variable's slot; `ownsMatrix` slots hold a matrix reference
  // that is released when the scope ends.
  void track(llvm::AllocaInst *slot, bool ownsMatrix);
  // Emits the cleanups at the builder's position (which must not be
  // terminated). The scope stays open: emitting twice (a `return` inside the
  // scope, then its normal end) is fine; each path runs them once.
  void emitCleanups(llvm::IRBuilderBase &builder) const;

  [[nodiscard]] bool empty() const noexcept { return tracked_.empty(); }

private:
  struct Tracked {
    llvm::AllocaInst *slot;
    bool ownsMatrix;
  };
  CodegenModule &cgm_;
  std::vector<Tracked> tracked_{};
};

} // namespace MemoLang
#endif // LIFETIMEUTILS_H_
