#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "AST/AST.h"
#include "Semantics/Scope.h"

// Semantic analysis: name resolution and type checking.
//
// 2026-11-12: removed `using SourceLocation = SourceLoc;`, which hid the real
// MemoLang::SourceLocation behind a different struct, and DiagnosticSink:
// errors now go to the DiagnosticEngine every other stage uses, so they are
// printed with source context, counted, and set the exit code.
//
// 2026-11-12 (later): implemented in sema.cpp. One scope design: the
// MemoLang::Scope from Scope.h (parent pointers + SymbolId). The separate
// sema::Scope class (shared_ptr parents and push()/pop()) and the analyzer's
// own scopes_ stack were removed. Symbols live in a table indexed by SymbolId,
// and every name in the program is resolved to one, so later passes
// (destructive analysis, codegen) can tell shadowed variables apart.
// Functions take const AST references: analysis results are stored on the
// tree through Node::setResolvedType(), which is const.

namespace MemoLang {
class DiagnosticEngine;
class TypeContext;
class Type;

namespace sema {

struct Symbol {
  enum class Kind { Variable, Parameter, Function, Builtin };

  std::string name;
  const Type *type = nullptr;
  Kind kind = Kind::Variable;
  bool isFunction = false;  // kind == Function or Builtin.
  bool isParameter = false; // kind == Parameter.
  bool isLocal = false;     // Declared inside a function.
  bool isConst = false;
  std::uint32_t scopeDepth = 0; // 0 = module level.
  SourceRange location;         // Where it was declared.
  const FunctionDef *function = nullptr; // For Kind::Function.
};

class SemanticAnalyzer {
public:
  // `types` and `diags` must outlive the analyzer.
  SemanticAnalyzer(TypeContext &types, DiagnosticEngine &diags);
  ~SemanticAnalyzer();
  SemanticAnalyzer(const SemanticAnalyzer &) = delete;
  SemanticAnalyzer &operator=(const SemanticAnalyzer &) = delete;

  // Checks a whole file. Returns true if it found no errors.
  bool analyzeModule(const Module &module);
  bool analyzeFunction(const FunctionDef &function);
  bool analyzeStatement(const Statement &stmt);
  // Works out the type of `expr` (also stored as expr.resolvedType()).
  // Returns TypeContext::errorType() after reporting an error.
  const Type *inferExpressionType(const Expr &expr);
  bool analyzeExpression(const Expr &expr);

  // The type a type expression names, e.g. `matrix<float>`.
  const Type *resolveType(const TypeExpr &node);

  // Can a value of type `value` be stored in something of type `target`?
  [[nodiscard]] bool checkAssignment(const Type &target,
                                     const Type &value) const;
  // The result of `lhs op rhs` for arithmetic, or null if invalid.
  [[nodiscard]] const Type *checkArithmetic(BinaryOp op, const Type &lhs,
                                            const Type &rhs);
  // Can `lhs op rhs` be compared (==, <, ...)?
  [[nodiscard]] bool checkComparison(BinaryOp op, const Type &lhs,
                                     const Type &rhs) const;

  // Results for later passes.
  [[nodiscard]] const Symbol &symbol(SymbolId id) const;
  [[nodiscard]] const std::vector<Symbol> &symbols() const noexcept {
    return symbols_;
  }
  // The symbol a name refers to, or nullopt if it didn't resolve.
  [[nodiscard]] std::optional<SymbolId> symbolFor(const NameExpr &name) const;
  // The symbol a declaration introduced.
  [[nodiscard]] std::optional<SymbolId>
  declaredSymbol(const DeclareVar &decl) const;
  [[nodiscard]] const std::vector<SymbolId> &
  parameterSymbols(const FunctionDef &function) const;

  // One line per symbol, for --dump-symbols and debugging.
  [[nodiscard]] std::string dumpSymbolTable() const;

private:
  struct FunctionInfo;
  struct FunctionContext;
  class ScopeGuard;

  // Declarations
  SymbolId addSymbol(Symbol symbol);
  bool declare(const std::string &name, SymbolId id, const Node &where);
  [[nodiscard]] std::optional<SymbolId> lookup(std::string_view name) const;
  void declareFunctionSignature(const FunctionDef &function);
  void analyzeFunctionBody(const FunctionDef &function);
  const Type *functionReturnType(const FunctionDef &function,
                                 const Node &useSite);

  // Statements
  void analyzeBody(std::span<const std::unique_ptr<Statement>> body);
  void analyzeDeclareVar(const DeclareVar &decl);
  void analyzeAssign(const AssignStmt &assign);
  void analyzeReturn(const ReturnStmt &ret);
  void checkCondition(const Expr &condition, std::string_view what);

  // Expressions
  const Type *inferName(const NameExpr &name);
  const Type *inferUnary(const UnaryExpr &unary);
  const Type *inferBinary(const BinaryExpr &binary);
  const Type *combineBinary(const BinaryExpr &binary, const Type *lhs,
                            const Type *rhs);
  const Type *inferMatMul(const BinaryExpr &binary, const Type *lhs,
                          const Type *rhs);
  const Type *inferCall(const CallExpr &call);
  const Type *inferCast(const CastExpr &cast);
  const Type *commonNumericType(const Type &lhs, const Type &rhs);

  void error(const Node &where, std::string message);
  void note(SourceRange where, std::string message);

  TypeContext &types_;
  DiagnosticEngine &diagnostics_;
  std::vector<Symbol> symbols_;
  Scope global_;
  std::vector<std::unique_ptr<Scope>> localScopes_; // Innermost last.
  std::unordered_map<const NameExpr *, SymbolId> resolutions_;
  std::unordered_map<const DeclareVar *, SymbolId> declarations_;
  std::unordered_map<const FunctionDef *, std::unique_ptr<FunctionInfo>>
      functions_;
  std::unique_ptr<FunctionContext> topLevel_; // Code outside any function.
  FunctionContext *current_ = nullptr;        // The function being analyzed.
  std::size_t errorCountAtStart_ = 0;
};

} // namespace sema
} // namespace MemoLang
