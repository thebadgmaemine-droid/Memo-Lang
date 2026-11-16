#ifndef LINKER_H_
#define LINKER_H_
// Links the llvm::Modules generated for each imported file into one module
// before it is emitted. Not implemented yet (template).
#include <memory>

namespace llvm {
class Module;
} // namespace llvm

namespace MemoLang {
class DiagnosticEngine;

class Linker {
public:
  Linker(llvm::Module &destination, DiagnosticEngine &diagnostics);

  // Moves `source` into the destination module. Returns false on conflicts
  // (e.g. two definitions of the same function), which are reported.
  bool linkInModule(std::unique_ptr<llvm::Module> source);

  [[nodiscard]] llvm::Module &getDestination() const { return destination_; }

private:
  llvm::Module &destination_;
  DiagnosticEngine &diagnostics_;
};

} // namespace MemoLang
#endif // LINKER_H_
