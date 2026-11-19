#ifndef BACKENDDIAGNOSTIC_H
#define BACKENDDIAGNOSTIC_H
//---------------------------------------------------------------------------
// Converts diagnostics emitted by LLVM's back end (the verifier, the linker,
// optimization passes, instruction selection, ...) into MemoLang
// diagnostics. Only the message carries over: generated code has no debug
// locations yet, so they are reported without a source position.
//
// Install it on the LLVMContext for as long as code generation runs
// (CodegenAction::execute does): see BackendDiagnostics::install().
//
// 2026-11-17: implemented. CodeGenerationType  is now
// CodegenOptions; SetCurrrentLink is SetCurrentLink; EmitOptimizationMessage
// takes a MemoLang Severity instead of a clang-style DiagID.
//---------------------------------------------------------------------------
#include <memory>

#include "Codegen/CodegenOptions.h"
#include "Diag/diag.h"

namespace llvm {
class DiagnosticHandler;
class DiagnosticInfo;
class DiagnosticInfoDontCall;
class DiagnosticInfoMisExpect;
class DiagnosticInfoOptimizationBase;
class DiagnosticInfoOptimizationFailure;
class DiagnosticInfoSrcMgr;
class DiagnosticInfoStackSize;
class DiagnosticInfoUnsupported;
class LLVMContext;
class Module;
class OptimizationRemarkAnalysisAliasing;
class OptimizationRemarkAnalysisFPCommute;
} // namespace llvm

namespace MemoLang {
class DiagnosticEngine;

class BackendDiagnostics {
public:
  BackendDiagnostics(DiagnosticEngine &Diags, const CodegenOptions &CodeGenOpts)
      : Diags(Diags), CodeGenOpts(CodeGenOpts) {}

  // Routes `context`'s diagnostics here until the returned guard is
  // destroyed; then the previous handler is restored.
  class InstallGuard {
  public:
    InstallGuard(llvm::LLVMContext &context, BackendDiagnostics &diagnostics);
    ~InstallGuard();
    InstallGuard(const InstallGuard &) = delete;
    InstallGuard &operator=(const InstallGuard &) = delete;

  private:
    llvm::LLVMContext &context_;
    std::unique_ptr<llvm::DiagnosticHandler> previous_;
  };
  [[nodiscard]] InstallGuard install(llvm::LLVMContext &context) {
    return InstallGuard(context, *this);
  }

  // The module being linked in, if any, to name it in messages.
  void SetCurrentLink(llvm::Module *M) { CurrentLinkedModule = M; }
  // Used to pass llvm Diagnostic emits to MemoLang.
  void HandleDiagnostics(const llvm::DiagnosticInfo &DiagInfo);

  [[nodiscard]] bool remarksEnabled() const noexcept {
    return CodeGenOpts.optimizationRemarks;
  }

private:
  DiagnosticEngine &Diags;
  const CodegenOptions &CodeGenOpts;
  llvm::Module *CurrentLinkedModule = nullptr;
  

  void report(int llvmSeverity, std::string message);
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
