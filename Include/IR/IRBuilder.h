#pragma once
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
#include <llvm/IR/IRBuilder.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <unordered_set>

namespace llvm {
class BasicBlock;
class Function;
class LLVMContext;
class Module;
class Type;
class Value;
} // namespace llvm
namespace MemoLang {
class DiagnosticEngine;
class SourceManager;
class TypeContext; // Semantic types; to be written with semantic analysis
                   // (Semantics/sema.h).
struct Module;     // One parsed file (AST.h).
}
class IRGenerator {
public:
  IRGenerator(llvm::LLVMContext &theContext, TypeContext &types,
              DiagnosticEngine &diagnostics);
  [[nodiscard]] llvm::Module *getmodule() const; // module placeholder
  // Lowers `ast` to LLVM IR. Returns false if errors were reported.
  bool emit(const Module &ast, const std::string &moduleName,
            const std::vector<const Module *> *imported = nullptr);
  void SetModuleSources(
      std::unordered_map<const Module *, const SourceManager *> sources);
  // Gets the file each module is read from, the dianosticengine uses this.
private:
  // Empty for now // Updating 10/30/2026
  // // Updating 10/30/2026
  llvm::Type*lowers(const Type *type);
  llvm::Value *
  emitAnyTypeMatch(llvm::IRBuilder<> &builder, llvm::value value,
                   const Type *target); // Has target, has builder has value.
  std::unordered_map<std::string, llvm::Function*> functions_{};
  std::unordered_map<std::string, const FunctionDef*> functionDefs_{};
  std::unordered_map<std::string, bool> externFunctions_{};
  std::unordered_set<std::string> reachable_{};




}; // namespace MemoLang


#endif // IR_BUILDER_H
