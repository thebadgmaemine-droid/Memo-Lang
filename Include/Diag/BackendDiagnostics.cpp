// ---------------------------------------------------------------------------
// BackendDiagnostics.cpp: LLVM diagnostics -> MemoLang diagnostics
// ---------------------------------------------------------------------------
#include "BackendDiagnostics.h"

#include <filesystem>
#include <string>
#include <system_error>

#include <llvm/IR/DiagnosticHandler.h>
#include <llvm/IR/DiagnosticInfo.h>
#include <llvm/IR/DiagnosticPrinter.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_ostream.h>

#include "Diag/diagengine.h"
#include "Source/SourceManager.h"

namespace MemoLang {
namespace {


// Generated functions are named memo.<name>; show the MemoLang name.
std::string displayFunctionName(llvm::StringRef name) {
  if (name.starts_with("memo."))
  // returns the function after memo.
    return name.drop_front(5).str();
  // if no memo -> returns all

  return name.str();


}
/* 
Example code:
def memo.Add(params) 
  ... |
      |
      |
      V
the name is found with memo so it will move to Add using drop_front
displayFunctionName returns function name without memo.

*/

// Whether `llvmPath` (from debug info) names the file `ours`.
bool sameFile(const std::string &llvmPath, const std::string &ours) {
  if (llvmPath == ours)
    return true;
  std::error_code ec1, ec2;
  auto a = std::filesystem::weakly_canonical(llvmPath, ec1);
  auto b = std::filesystem::weakly_canonical(ours, ec2);
  return !ec1 && !ec2 && a == b;
}

// The LLVM-side handler: forwards everything to BackendDiagnostics.
class Handler final : public llvm::DiagnosticHandler {
public:
  explicit Handler(BackendDiagnostics &diagnostics)
      : diagnostics_(diagnostics) {}

  bool handleDiagnostics(const llvm::DiagnosticInfo &info) override {
    diagnostics_.HandleDiagnostics(info);
    return true; // Handled: LLVM must not print it (or exit on errors).
  }
  bool isAnalysisRemarkEnabled(llvm::StringRef) const override {
    return diagnostics_.remarksEnabled();
  }
  bool isMissedOptRemarkEnabled(llvm::StringRef) const override {
    return diagnostics_.remarksEnabled();
  }
  bool isPassedOptRemarkEnabled(llvm::StringRef) const override {
    return diagnostics_.remarksEnabled();
  }
  bool isAnyRemarkEnabled() const override {
    return diagnostics_.remarksEnabled();
  }

private:
  BackendDiagnostics &diagnostics_;
};

} // namespace
// InstallGuard takes the previous handler with getDiagnosticHandler()
// (which moves it out) and puts it back in the destructor. That makes the
// handler safe to install on a context that someone else configured, such as
// a test harness.
BackendDiagnostics::InstallGuard::InstallGuard(llvm::LLVMContext &context,
                                               BackendDiagnostics &diagnostics)
    : context_(context), previous_(context.getDiagnosticHandler()) {
  context.setDiagnosticHandler(std::make_unique<Handler>(diagnostics));
}


BackendDiagnostics::InstallGuard::~InstallGuard() {
  context_.setDiagnosticHandler(std::move(previous_));
}
SourceRange
BackendDiagnostics::toSourceRange(const llvm::DiagnosticLocation &Loc) const {
  if (!Loc.isValid() || Loc.getLine() == 0)
    return {};
  const SourceManager &sources = Diags.sources();
  if (!sameFile(Loc.getRelativePath().str(), sources.fileName()) &&
      !sameFile(Loc.getAbsolutePath(), sources.fileName()))
    return {};
  SourceLocation begin = sources.locationFor(Loc.getLine(), Loc.getColumn());
  if (!begin.isValid())
    return {};
  SourceLocation end = begin;
  ++end.offset;
  ++end.column;
  return {begin, end};
}

// Reports using LLVM diagnostics.
void BackendDiagnostics::report(int llvmSeverity, SourceRange range,
                                std::string message) {
  Severity severity = Severity::Note;
  switch (static_cast<llvm::DiagnosticSeverity>(llvmSeverity)) {
  case llvm::DS_Error: severity = Severity::Error; break;
  case llvm::DS_Warning: severity = Severity::Warning; break;
  case llvm::DS_Remark:
  case llvm::DS_Note: severity = Severity::Note; break;
  }
  if (CurrentLinkedModule)
    message += " (while linking '" +
               CurrentLinkedModule->getModuleIdentifier() + "')";
  Diags.report(severity, range, "LLVM: " + message);
}
// Implementation of LLVm diagnostic pass to Memolang's.
void BackendDiagnostics::HandleDiagnostics(const llvm::DiagnosticInfo &DI) {
  switch (DI.getKind()) {
  case llvm::DK_SrcMgr:
    SrcMgrDiagHandler(llvm::cast<llvm::DiagnosticInfoSrcMgr>(DI));
    return;
  case llvm::DK_StackSize:
    if (StackSizeDiagHandler(llvm::cast<llvm::DiagnosticInfoStackSize>(DI)))
      return;
    break; // Fall back to the generic message.
  case llvm::DK_Unsupported:
    UnsupportedDiagHandler(llvm::cast<llvm::DiagnosticInfoUnsupported>(DI));
    return;
  case llvm::DK_DontCall:
    DontCallDiagHandler(llvm::cast<llvm::DiagnosticInfoDontCall>(DI));
    return;
  case llvm::DK_MisExpect:
    MisExpectDiagHandler(llvm::cast<llvm::DiagnosticInfoMisExpect>(DI));
    return;
  case llvm::DK_OptimizationFailure:
    OptimizationFailureHandler(
        llvm::cast<llvm::DiagnosticInfoOptimizationFailure>(DI));
    return;
  case llvm::DK_OptimizationRemarkAnalysisFPCommute:
    OptimizationRemarkHandler(
        llvm::cast<llvm::OptimizationRemarkAnalysisFPCommute>(DI));
    return;
  case llvm::DK_OptimizationRemarkAnalysisAliasing:
    OptimizationRemarkHandler(
        llvm::cast<llvm::OptimizationRemarkAnalysisAliasing>(DI));
    return;
  default:
    if (auto *remark = llvm::dyn_cast<llvm::DiagnosticInfoOptimizationBase>(&DI)) {
      OptimizationRemarkHandler(*remark);
      return;
    }
    break;
  }
  // Anything else: let LLVM format it.
  std::string message;
  llvm::raw_string_ostream out(message);
  llvm::DiagnosticPrinterRawOStream printer(out);
  DI.print(printer);
  report(DI.getSeverity(), std::move(message));
}

void BackendDiagnostics::SrcMgrDiagHandler(const llvm::DiagnosticInfoSrcMgr &D) {
  report(D.getSeverity(), D.getSMDiag().getMessage().str());
}
// Stack frame for iteratiion. A limit is set for iterative loop to prevent
// prevent performance leak. Memory leak by design is not possible
bool BackendDiagnostics::StackSizeDiagHandler (
    const llvm::DiagnosticInfoStackSize &D) {
  if (D.getSeverity() != llvm::DS_Warning && D.getSeverity() != llvm::DS_Error)
    return false;
  report(D.getSeverity(), toSourceRange(D.getLocation()),
         "stack frame size (" + std::to_string(D.getResourceSize()) +
             " bytes) exceeds the limit (" + std::to_string(D.getResourceLimit()) +
             " bytes) in function '" +
             displayFunctionName(D.getFunction().getName()) + "'");
  return true;
}

void BackendDiagnostics::UnsupportedDiagHandler(
    const llvm::DiagnosticInfoUnsupported &D) {
  report(D.getSeverity(), toSourceRange(D.getLocation()),
         D.getMessage().str() + " (in function '" +
             displayFunctionName(D.getFunction().getName()) + "')");
}

void BackendDiagnostics::EmitOptimizationMessage(
    const llvm::DiagnosticInfoOptimizationBase &D, Severity severity) {
  std::string message = D.getMsg();
  std::string where = displayFunctionName(D.getFunction().getName());
  Diags.report(severity, toSourceRange(D.getLocation()),
               "LLVM [" + D.getPassName().str() + "]: " + message +
                   (where.empty() ? "" : " (in function '" + where + "')"));
}


void BackendDiagnostics::OptimizationRemarkHandler (
    const llvm::DiagnosticInfoOptimizationBase &D) {
  // Remarks are opt-in; failures to optimize are reported separately.
  if (remarksEnabled() && D.isEnabled())
    EmitOptimizationMessage(D, Severity::Note);
}

void BackendDiagnostics::OptimizationRemarkHandler (
    const llvm::OptimizationRemarkAnalysisFPCommute &D) {
  // Vectorizing float reductions would reorder additions.
  OptimizationRemarkHandler(
      static_cast<const llvm::DiagnosticInfoOptimizationBase &>(D));
}

void BackendDiagnostics::OptimizationRemarkHandler(
    const llvm::OptimizationRemarkAnalysisAliasing &D) {
  // Pointers that might alias blocked an optimization.
  OptimizationRemarkHandler(
      static_cast<const llvm::DiagnosticInfoOptimizationBase &>(D));
}

void BackendDiagnostics::OptimizationFailureHandler(
    const llvm::DiagnosticInfoOptimizationFailure &D) {
  EmitOptimizationMessage(D, Severity::Warning);
}

void BackendDiagnostics::DontCallDiagHandler(
    const llvm::DiagnosticInfoDontCall &D) {
  std::string message = "call to '" +
                        displayFunctionName(D.getFunctionName()) +
                        "' is marked do-not-call";
  if (!D.getNote().empty())
    message += ": " + D.getNote().str();
  report(D.getSeverity(), std::move(message));
}

void BackendDiagnostics::MisExpectDiagHandler(
    const llvm::DiagnosticInfoMisExpect &D) {
  report(D.getSeverity(), toSourceRange(D.getLocation()), D.getMsg().str());
}

} // namespace MemoLang
