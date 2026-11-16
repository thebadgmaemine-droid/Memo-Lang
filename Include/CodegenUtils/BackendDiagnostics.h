#ifndef BACKENDDIAGNOSTIC_H
#define BACKENDDIAGNOSTIC_H

#include "Diag/diagengine.h"

namespace llvm {
class DiagnosticInfo;
class DiagnosticInfoDontCall;
class DiagnosticInfoMisExpect;
class DiagnosticInfoOptimizationBase;
class DiagnosticInfoOptimizationFailure;
class DiagnosticInfoResourceLimit;
class DiagnosticInfoSrcMgr;
class DiagnosticInfoStackSize;
class DiagnosticInfoUnsupported;
class DiagnosticInfoUnsupportedTargetIntrinsic;
class DiagnosticInfoWithLocationBase;
class Function;
class Module;
class OptimizationRemarkAnalysisAliasing;
class OptimizationRemarkAnalysisFPCommute;
} // namespace llvm

namespace MemoLang {
class CodeGenerationType;
class DiagnosticEngine; // Call from diagengine
class SourceManager;

class BackendDiagnostics {
public:
  // Converts diagnostics emitted by LLVM's back end into MemoLang
  // diagnostics. Only message, Source location uses LLVM's
    BackendDiagnostics(DiagnosticEngine &Diags,
                     const CodeGenerationType &CodeGenType)
      : Diags(Diags), CodeGenType(CodeGenType) {}
    // Determine current linkage
    void SetCurrrentLink(llvm::Module *M) { CurrentLinkedModule = M; }
    // Used to pass llvm Diagnostic emits to MemoLang
    void HandleDiagnostics(const llvm::DiagnosticInfo &DiagInfo);
    private:
    // Forward declaration for Diag constructor
    DiagnosticEngine &Diags;
    // Forward declaration for CodeGenType ( important !! )
    const CodeGenerationType &CodeGenType;
    // Forward declaration for get linked module
    llvm::Module *CurrentLinkedModule = nullptr;
    // Source message handler
    void SrcMgrDiagHandler(const llvm::DiagnosticInfoSrcMgr &D);
    // Handler for outputting stacksize
    bool StackSizeDiagHandler(const llvm::DiagnosticInfoStackSize &D);
    // Default DiagHandler fallback
    void UnsupportedDiagHandler(const llvm::DiagnosticInfoUnsupported &D);
    // Used for testing and debugging optimizations
    void EmitOptimizationMessage(const llvm::DiagnosticInfoOptimizationBase &D,
                               unsigned DiagID);
    // Part of optimization api
    void OptimizationRemarkHandler(const llvm::DiagnosticInfoOptimizationBase &D);
    // Part of optimization api
    void OptimizationRemarkHandler(const llvm::OptimizationRemarkAnalysisFPCommute &D);
    // Part of optimization api
    void OptimizationRemarkHandler(const llvm::OptimizationRemarkAnalysisAliasing &D);
    // Part of optimization api
    void OptimizationFailureHandler(const llvm::DiagnosticInfoOptimizationFailure &D);
    // If we miss-expect an error, use this.
    void DontCallDiagHandler(const llvm::DiagnosticInfoDontCall &D);
    // If we miss-expect an error and want to replace, use this.
    void MisExpectDiagHandler(const llvm::DiagnosticInfoMisExpect &D);
   

};
}; // namespace MemoLang

#endif // backenddiagnostic.h