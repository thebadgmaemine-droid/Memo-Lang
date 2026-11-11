#ifndef IR_BUILDER_H
#define IR_BUILDER_H
// Interface for LLVM IR generation (the codegen stage). Not implemented yet.
//
// 2026-10-17: made valid C++ to match the new front-end. It was written for
// the old Kaleidoscope-style code and didn't compile: `namespace memo-lang`
// (a hyphen isn't allowed in a name) is now `MemoLang`, the class is named
// IRGenerator to match its constructor (and to avoid clashing with
// llvm::IRBuilder), emit() got a return type, the missing `;` after the class
// was added, and every type it uses is declared.
//
// 2026-11-11: the class had ended up outside `MemoLang` (so IRBuilder.cpp's
// MemoLang::IRGenerator definitions didn't compile), emit()/SetModuleSources()
// took llvm::Module instead of the AST's Module, FunctionDef wasn't declared,
// emitAnyTypeMatch took an llvm::Value by value (it can't be copied), and
// lowers() mapped llvm::Type to llvm::Type instead of a semantic type.
#include <llvm/IR/IRBuilder.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace llvm {
class Function;
class LLVMContext;
class Module;
class Type;
class Value;
} // namespace llvm

namespace MemoLang {
class DiagnosticEngine;
class SourceManager;
class TypeContext; // Semantic types (Types/Typecontext.h).
class Type;        // One semantic type (Types/Typecontext.h).
class FunctionDef; // AST.h
struct Module;     // One parsed file (AST.h).

class IRGenerator {
public:
  IRGenerator(llvm::LLVMContext &theContext, TypeContext &types,
              DiagnosticEngine &diagnostics);
  [[nodiscard]] llvm::Module *getmodule() const; // module placeholder
  // Lowers `ast` to LLVM IR. Returns false if errors were reported.
  bool emit(const Module &ast, const std::string &moduleName,
            const std::vector<const Module *> *imported = nullptr);
  // Gets the file each module is read from; the DiagnosticEngine uses this.
  void SetModuleSources(
      std::unordered_map<const Module *, const SourceManager *> sources);

private:
  // Empty for now // Updating 10/30/2026
  // Maps a semantic type to its LLVM type.
  llvm::Type *lowers(const Type *type);

  int emitAnyTypeMatch(llvm::IRBuilder<> &builder, llvm::Value *value,
                       const llvm::Type *target); // Has target, builder, value.
  std::unordered_map<std::string, llvm::Function *> functions_{};
  std::unordered_map<std::string, const FunctionDef *> functionDefs_{};
  std::unordered_map<std::string, bool> externFunctions_{};
  std::unordered_set<std::string> reachable_{};
};

} // namespace MemoLang
#endif // IR_BUILDER_H
