// ---------------------------------------------------------------------------
// CodegenAction.cpp: runs the back end for one input (CodegenAction.h)
//
//   AST --ModuleBuilder--> llvm::Module --(O1-O3 pipeline)--> .ll/.bc/.s/.o
// ---------------------------------------------------------------------------
#include "Codegen/CodegenAction.h"

#include <filesystem>

#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>

#include "AST/AST.h"
#include "Codegen/ModuleBuilder.h"
#include "Diag/diagengine.h"

namespace MemoLang {
// Codegen action constructor
CodegenAction::CodegenAction(BackendAction action, llvm::LLVMContext &context,
                             const CodegenInputs &inputs,
                             CodegenOptions options)
    : action_(action), context_(context), inputs_(inputs),
      options_(std::move(options)),
      backendDiagnostics_(inputs.diags, options_) {}
// Default non-overrides of CodegenAction
CodegenAction::~CodegenAction() = default;
// Sets default output path of emitted program
std::string CodegenAction::defaultOutputPath(std::string_view inputPath,
                                             BackendAction action) {
  std::filesystem::path path(inputPath);
  const char *extension = ".o";
  switch (action) {
  case BackendAction::EmitLLVM: extension = ".ll"; break;
  case BackendAction::EmitBitcode: extension = ".bc"; break;
  case BackendAction::EmitAssembly: extension = ".s"; break;
  default: break;
  }
  return path.stem().string() + extension;
}
// Execute: 
// Installs BackendDiagnostic including InstallGuard
// Builds module using ModuleBuilder
// use prepareTarget() once per process, look up triple to create the following: 
// TargetMachine, Triple stamp and Data layout
// optimize() if opt level > 0 ( as said before, loops until no longer zero )
// emitOutput() to file.
bool CodegenAction::execute(const Module &ast, const std::string &outputPath) {
  targetMachine_.reset();

  std::size_t errorsBefore = inputs_.diags.errorCount();
  auto failed = [&] { return inputs_.diags.errorCount() != errorsBefore; };
  // LLVM's own diagnostics (verifier, linker, passes, isel) go to the
  // DiagnosticEngine while this runs.
  auto guard = backendDiagnostics_.install(context_);
  

  // The target comes first so the module is lowered with its data layout.
  // IR outputs (.ll/.bc) still work without one; they just carry no triple.
  bool needsTarget = action_ == BackendAction::EmitAssembly ||
                     action_ == BackendAction::EmitObject ||
                     action_ == BackendAction::EmitCodeGenOnly;
  if (!prepareTarget() && needsTarget)
    return false;

  ModuleBuilder builder(context_, inputs_, options_, ast.name,
                        targetMachine_.get());
  builder.handleModule(ast);
  builder.handleTranslationUnit();
  module_ = builder.releaseModule();
  if (failed() || !module_)
    return false;

  if (options_.optLevel > 0)
    optimize(*module_);
  return emitOutput(*module_, outputPath) && !failed();
}

// Explicitly lambda for thread safety, if optpasses force this more than once then only one pass is done
bool CodegenAction::prepareTarget() {
  static const bool initialized = [] {
    // Initialize native to make sure it is linked in
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
    return true;
    // Trailing "()" invokes The lambda after initialization 
  }();
  // Explicitly cast to void
  (void)initialized;

  llvm::Triple triple(options_.targetTriple.empty()
                          ? llvm::sys::getDefaultTargetTriple()
                          : options_.targetTriple);
  std::string error;
  const llvm::Target *target = llvm::TargetRegistry::lookupTarget(triple, error);
  if (!target) {
    inputs_.diags.error(SourceRange{}, "no code generator for target '" +
                                           triple.str() + "': " + error);
    return false;
  }
  llvm::CodeGenOptLevel level = llvm::CodeGenOptLevel::None;
  switch (options_.optLevel) {
  case 0: break;
  case 1: level = llvm::CodeGenOptLevel::Less; break;
  case 2: level = llvm::CodeGenOptLevel::Default; break;
  default: level = llvm::CodeGenOptLevel::Aggressive; break;
  }
  targetMachine_.reset(target->createTargetMachine(
      triple, "generic", "", llvm::TargetOptions(), llvm::Reloc::PIC_,
      std::nullopt, level));
  if (!targetMachine_) {
    inputs_.diags.error(SourceRange{}, "can't create a target machine for '" +
                                           triple.str() + "'");
    return false;
  }
  return true;
}
// For optimization passes
void CodegenAction::optimize(llvm::Module &module) {
  // Declarations
  llvm::LoopAnalysisManager loops;
  llvm::FunctionAnalysisManager functions;
  llvm::CGSCCAnalysisManager cgscc;
  llvm::ModuleAnalysisManager modules;
  llvm::PassBuilder passes(targetMachine_.get());
  // Registers each and pass through LLVM api
  passes.registerModuleAnalyses(modules);
  passes.registerCGSCCAnalyses(cgscc);
  passes.registerFunctionAnalyses(functions);
  passes.registerLoopAnalyses(loops);
  passes.crossRegisterProxies(loops, functions, cgscc, modules);
// Opt level
  llvm::OptimizationLevel level = llvm::OptimizationLevel::O2;
  if (options_.optLevel == 1)
    level = llvm::OptimizationLevel::O1;
  else if (options_.optLevel >= 3)
    level = llvm::OptimizationLevel::O3;
  // Build
  passes.buildPerModuleDefaultPipeline(level).run(module, modules);
}
// Emits output to file path
bool CodegenAction::emitOutput(llvm::Module &module,
                               const std::string &outputPath) {

  if (action_ == BackendAction::EmitNothing)
    return true;
// Check flag for file mode/type
  bool text = action_ == BackendAction::EmitLLVM ||
              action_ == BackendAction::EmitAssembly;
// Opens output file stream
// If the action is EmitCodeGenOnly, the compiler runs the backend code generation passes 
// (to verify no backend errors occur), but discards the output. 
  std::error_code ec;
  std::unique_ptr<llvm::raw_fd_ostream> file;
  if (action_ != BackendAction::EmitCodeGenOnly) {
    file = std::make_unique<llvm::raw_fd_ostream>(
      // OF: human readable files, OF_none: non-readable binary
        outputPath, ec, text ? llvm::sys::fs::OF_Text : llvm::sys::fs::OF_None);
    if (ec) {
      // Checks for has error and stops if DiagEngine collected enough errors
      inputs_.diags.error(SourceRange{}, "can't write '" + outputPath +
                                             "': " + ec.message());
      return false;
    }
  }
// If not EmitCodeGenOnly:
// Emits and write output based on codegen options.
// Discard of codegenonly
  switch (action_) {
  case BackendAction::EmitLLVM:
    module.print(*file, nullptr);
    break;
  case BackendAction::EmitBitcode:
    llvm::WriteBitcodeToFile(module, *file);
    break;
  case BackendAction::EmitAssembly:
  case BackendAction::EmitObject:
  case BackendAction::EmitCodeGenOnly: {
    llvm::raw_null_ostream discard;
    // Class for emits file while maintaining overwrite ability.
    // When compiler panics and restores, this is useful ( backwards seek )
    llvm::raw_pwrite_stream &out =
        file ? static_cast<llvm::raw_pwrite_stream &>(*file) : discard;
    llvm::legacy::PassManager codegen;
    auto fileType = action_ == BackendAction::EmitAssembly
                        ? llvm::CodeGenFileType::AssemblyFile
                        : llvm::CodeGenFileType::ObjectFile;
                        // Base : codegen and dumps into null since cannot emit
    if (targetMachine_->addPassesToEmitFile(codegen, out, nullptr, fileType)) {
      inputs_.diags.error(SourceRange{}, "the target can't emit this kind of "
                                         "file");
      return false;
    }
    codegen.run(module);
    break;
  }
  case BackendAction::EmitNothing:
    break;
  }
  if (file) {
    // if has file, flush file
    file->flush();
    if (file->has_error()) {
      // Hands errors to diagengine.h, and fails to write with message
      inputs_.diags.error(SourceRange{}, "error writing '" + outputPath + "'");
      file->clear_error();
      return false;
    }
  }
  return true;
}
// intakes module and cast it into a temp value that has to be put back
// Unless error, which fails to write anyways and send to errors
std::unique_ptr<llvm::Module> CodegenAction::takeModule() {
  return std::move(module_);
}

} // namespace MemoLang
