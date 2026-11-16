#ifndef LIFETIMEUTILS_H_
#define LIFETIMEUTILS_H_
// Helpers for emitting the lifetime of stack values: llvm.lifetime.start/end
// markers and the cleanups that run when a scope ends. Not implemented yet
// (template).
#include <cstdint>
#include <vector>

namespace llvm {
class AllocaInst;
class BasicBlock;
class Value;
} // namespace llvm

namespace MemoLang {
class CodegenModule;

// Marks the start of `alloca`'s lifetime (llvm.lifetime.start).
void emitLifetimeStart(CodegenModule &cgm, llvm::AllocaInst *alloca,
                       std::uint64_t sizeInBytes);
// Marks the end of `alloca`'s lifetime (llvm.lifetime.end).
void emitLifetimeEnd(CodegenModule &cgm, llvm::AllocaInst *alloca,
                     std::uint64_t sizeInBytes);

// The values owned by one lexical scope; their cleanups run in reverse order
// when the scope ends (normal exit, return or break).
class LifetimeScope {
public:
  explicit LifetimeScope(CodegenModule &cgm) : cgm_(cgm) {}
  LifetimeScope(const LifetimeScope &) = delete;
  LifetimeScope &operator=(const LifetimeScope &) = delete;

  // Registers `alloca` to be ended when this scope exits.
  void track(llvm::AllocaInst *alloca, std::uint64_t sizeInBytes);
  // Emits the end-of-lifetime markers and cleanups into `exitBlock`.
  void emitCleanups(llvm::BasicBlock *exitBlock);

private:
  struct Tracked {
    llvm::AllocaInst *alloca;
    std::uint64_t size;
  };
  CodegenModule &cgm_;
  std::vector<Tracked> tracked_{};
};

} // namespace MemoLang
#endif // LIFETIMEUTILS_H_
