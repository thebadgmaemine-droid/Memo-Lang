#ifndef BACKENDDIAGNOSTIC_H
#define BACKENDDIAGNOSTIC_H
//---------------------------------------------------------------------------
// Converts diagnostics emitted by LLVM's back end (the verifier, the linker,
// optimization passes, instruction selection, ...) into MemoLang
// diagnostics.
//
// Locations: an llvm::DiagnosticLocation is file + line + column, taken from
// the debug info (DILocation) of the instruction or function involved. It is
// not a packed 32-bit value like Clang's SourceLocation. toSourceRange() maps
// it to a MemoLang SourceRange per diagnostic, filling in the byte offset
// from the DiagnosticEngine's SourceManager. Codegen emits no debug info yet,
// so for now every location is invalid and diagnostics are reported without
// a position.
//
// Install it on the LLVMContext for as long as code generation runs
// (CodegenAction::execute does): see BackendDiagnostics::install().
//
// 2026-11-17: implemented. CodeGenerationType  is now
// CodegenOptions; SetCurrrentLink is SetCurrentLink; EmitOptimizationMessage
// takes a MemoLang Severity instead of a clang-style DiagID.
// 2026-11-20: LLVM locations are converted to SourceRanges (toSourceRange).
// The DIlocation member was removed: each diagnostic carries its own
// location, so the conversion is a function, not stored state.
//---------------------------------------------------------------------------
#include <memory>
#include <string>
#include <utility>

#include "Codegen/CodegenOptions.h"
#include "Diag/diag.h"
#include "Source/SourceLocation.h"
#include "Support/Compiler.h"

namespace llvm {
// A struct in LLVM (IR/DiagnosticHandler.h). MSVC puts class/struct into
// mangled names, so declaring it `class` made the LLVMContext handler calls
// unresolved at link time on Windows (GCC and Clang don't mind).
struct DiagnosticHandler;
class DiagnosticInfo;
class DiagnosticInfoDontCall;
class DiagnosticInfoMisExpect;
class DiagnosticInfoOptimizationBase;
class DiagnosticInfoOptimizationFailure;
class DiagnosticInfoSrcMgr;
class DiagnosticInfoStackSize;
class DiagnosticInfoUnsupported;
class DiagnosticLocation;
class LLVMContext;
class Module;
class OptimizationRemarkAnalysisAliasing;
class OptimizationRemarkAnalysisFPCommute;
} // namespace llvm

namespace MemoLang {
class DiagnosticEngine;

class BackendDiagnostics {
public:
  BackendDiagnostics(DiagnosticEngine &diags MEMOLANG_LIFETIMEBOUND, const CodegenOptions &codeGenOpts MEMOLANG_LIFETIMEBOUND)
      : Diags(diags), CodeGenOpts(codeGenOpts) {}

  // Routes `context`'s diagnostics here until the returned guard is
  // destroyed; then the previous handler is restored.
  class InstallGuard {
  public:
    InstallGuard(llvm::LLVMContext &context MEMOLANG_LIFETIMEBOUND,
                 BackendDiagnostics &diagnostics);
    ~InstallGuard();
    InstallGuard(const InstallGuard &) = delete;
    InstallGuard &operator=(const InstallGuard &) = delete;

  private:
    llvm::LLVMContext &context_;
    std::unique_ptr<llvm::DiagnosticHandler> previous_;
  };
  [[nodiscard]] InstallGuard
  install(llvm::LLVMContext &context MEMOLANG_LIFETIMEBOUND) {
    return InstallGuard(context, *this);
  }

  // The module being linked in, if any, to name it in messages.
  void SetCurrentLink(llvm::Module *M) { CurrentLinkedModule = M; }
  // Used to pass llvm Diagnostic emits to MemoLang.
  void HandleDiagnostics(const llvm::DiagnosticInfo &DiagInfo);

  [[nodiscard]] bool remarksEnabled() const noexcept {
    return CodeGenOpts.optimizationRemarks;
  }

  // Maps an LLVM location to a range in Diags' source file. Returns an
  // invalid range (reported without a position) if the location is invalid,
  // in another file, or past the end of the file. The range covers one
  // character: LLVM only gives a start.
  [[nodiscard]] SourceRange
  toSourceRange(const llvm::DiagnosticLocation &Loc) const;

private:
  DiagnosticEngine &Diags;
  const CodegenOptions &CodeGenOpts;
  llvm::Module *CurrentLinkedModule = nullptr;

  void report(int llvmSeverity, SourceRange range, std::string message);
  void report(int llvmSeverity, std::string message) {
    report(llvmSeverity, SourceRange{}, std::move(message));
  }
  // Inline-asm / source manager messages.
  void SrcMgrDiagHandler(const llvm::DiagnosticInfoSrcMgr &D);
  // A function's stack frame is too large. Returns false if not handled.
  bool StackSizeDiagHandler(const llvm::DiagnosticInfoStackSize &D);
  // Something the target can't generate.
  void UnsupportedDiagHandler(const llvm::DiagnosticInfoUnsupported &D);
  // Optimization remarks (reported as notes, when enabled).
  void EmitOptimizationMessage(const llvm::DiagnosticInfoOptimizationBase &D,
                               Severity severity);
  void OptimizationRemarkHandler(const llvm::DiagnosticInfoOptimizationBase &D);
  void
  OptimizationRemarkHandler(const llvm::OptimizationRemarkAnalysisFPCommute &D);
  void
  OptimizationRemarkHandler(const llvm::OptimizationRemarkAnalysisAliasing &D);
  void OptimizationFailureHandler(const llvm::DiagnosticInfoOptimizationFailure &D);
  // A call to a function marked "dontcall-error"/"dontcall-warn".
  void DontCallDiagHandler(const llvm::DiagnosticInfoDontCall &D);
  // Branch weights that disagree with llvm.expect.
  void MisExpectDiagHandler(const llvm::DiagnosticInfoMisExpect &D);
};

} // namespace MemoLang

#endif // BACKENDDIAGNOSTIC_H
