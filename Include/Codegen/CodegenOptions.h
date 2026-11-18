#ifndef CODEGEN_OPTIONS_H_
#define CODEGEN_OPTIONS_H_
// Settings for code generation, chosen by the driver. (BackendDiagnostics'
// draft called this CodeGenerationType; it was never defined.)
#include <string>

namespace MemoLang {

struct CodegenOptions {
  // 0 = no optimization; 1-3 run LLVM's O1-O3 pipelines.
  // Compiler is set to loop if opt level is zero
  unsigned optLevel = 0;
  // Target triple; empty means the machine the compiler runs on.
  std::string targetTriple;
  // Run llvm::verifyModule on the generated module (cheap; keep it on).
  bool verify = true;
  // Report LLVM optimization remarks as notes.
  bool optimizationRemarks = false;
  // Emit `int main()`, which runs module-level code, calls MemoLang's
  // main() and prints its result. Off for modules linked into a host.
  bool emitEntryPoint = true;
};

} // namespace MemoLang
#endif // CODEGEN_OPTIONS_H_
