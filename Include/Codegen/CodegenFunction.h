#ifndef CODEGEN_FUNCTION_H_
#define CODEGEN_FUNCTION_H_
// Lowers the statements and expressions of one function (or of the module-
// level code in memo.init) to LLVM IR.
//--------------------------------------------------------------------------
// Values follow the interpreter (Interp/interpreter.cpp) exactly, so a
// compiled program returns what `MemoLang --run` prints:
//   - Arithmetic is done in i64 (both operands integers) or double, then
//     converted to the expression's type: integers wrap around, f32 rounds.
//   - Errors only visible at run time (division by zero, shift out of range,
//     float too large for an integer, ...) call memolang_rt_panic.
//   - Matrices are reference-counted handles. Every matrix-valued expression
//     yields one reference the code that uses it owns. Reading a variable
//     that destructive analysis marked as consumed moves its reference out
//     (the slot becomes null); any other read adds a reference. Operations
//     release their operands, so a consumed matrix is freed the moment the
//     operation using it is done. Scopes release what their variables still
//     hold when they end (LifetimeUtils.h).
//--------------------------------------------------------------------------
// 2026-11-17: added.
#include <cstdint>
#include <llvm/IR/DerivedTypes.h>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <llvm/IR/IRBuilder.h>

#include "AST/AST.h"
#include "Codegen/LifetimeUtils.h"
#include "Typecontext.h"
#include "Support/Compiler.h"

namespace MemoLang {
class CodegenModule;
class TypeContext;

class CodegenFunction {
public:
  // `definition` is null for memo.init (module-level code).
  CodegenFunction(CodegenModule &cgm MEMOLANG_LIFETIMEBOUND, llvm::Function *function MEMOLANG_LIFETIMEBOUND,
                  const FunctionDef *definition MEMOLANG_LIFETIMEBOUND);
  ~CodegenFunction();
  CodegenFunction(const CodegenFunction &) = delete;
  CodegenFunction &operator=(const CodegenFunction &) = delete;

  // Stores the parameters into their variables.
  void emitPrologue();
  void emitBody(std::span<const std::unique_ptr<Statement>> body);
  void emitStatement(const Statement &stmt);
  
  void finish();

private:
  // Statements
  void emitDeclareVar(const DeclareVar &decl);
  void emitAssign(const AssignStmt &assign);
  void emitReturn(const ReturnStmt &ret);
  void emitIf(const IfStmt &ifStmt);
  void emitWhile(const WhileStmt &loop);
  void emitBreak(const BreakStmt &stmt);
  // Runs `body` in a new scope.
  void emitScopedBody(std::span<const std::unique_ptr<Statement>> body);

  // Expressions: each returns a value of the lowered expr.resolvedType().
  llvm::Value *emitExpr(const Expr &expr);
  llvm::Value *emitName(const NameExpr &name);
  llvm::Value *emitInterpolatedString(const InterpolatedString &interpolatedstring);
  llvm::Value *emitUnary(const UnaryExpr &unary);
  llvm::Value *emitBinary(const BinaryExpr &binary);
  llvm::Value *emitLogical(const BinaryExpr &binary, llvm::Value *lhs);
  llvm::Value *emitCall(const CallExpr &call);
  llvm::Value *emitRuntimeExtern(const FunctionDef &function,
                                 std::vector<llvm::Value *> &args,
                                 const CallExpr &call);
  // One embedded value of an interpolated string, as text.
  llvm::Value *emitToString(const Expr &value, std::string_view spec);

  // `lhs op rhs` (not `and`/`or`), converted to `result`. Releases matrix
  // operands.
  llvm::Value *emitBinaryOp(BinaryOp op, llvm::Value *lhs, const Type *lhsType,
                            llvm::Value *rhs, const Type *rhsType,
                            const Type *result, const Node &where);
  llvm::Value *emitIntOp(BinaryOp op, llvm::Value *a, llvm::Value *b,
                         const Node &where, bool isUnsigned);
  llvm::Value *emitFloatOp(BinaryOp op, llvm::Value *a, llvm::Value *b,
                           const Node &where);
  llvm::Value *emitMatrixOp(BinaryOp op, llvm::Value *lhs, const Type *lhsType,
                            llvm::Value *rhs, const Type *rhsType,
                            const Node &where);

  // Conversions, as the interpreter's convertTo().
  llvm::Value *convert(llvm::Value *value MEMOLANG_LIFETIMEBOUND, const Type *from, const Type *to);
  llvm::Value *toI64(llvm::Value *value, const Type *from);
  llvm::Value *toF64(llvm::Value *value, const Type *from);
  llvm::Value *defaultValue(const Type *type);

  // Variables
  struct Slot {
    llvm::Value *address = nullptr; // An alloca or a global.
    const Type *type = nullptr;
  };
  Slot slotFor(std::uint32_t symbol, const Node &where);
  // Where an assignment target lives: a variable's slot, or the address in
  // `p` for `*p` (checked for null). Used by assignment, `++`/`--` and `&`.
  Slot emitLValue(const Expr &target);
  llvm::AllocaInst *createEntryAlloca(llvm::Type *type, const std::string &name);
  void declareLocal(std::uint32_t symbol, const Type *type,
                    const std::string &name, llvm::Value *initial);

  // Runtime checks and calls
  // panicIf is set so that matrices, on memory release using operands ( @ ) always panics.
  void panicIf(llvm::Value *condition, const std::string &message);
  void release(llvm::Value *matrix);
  void retain(llvm::Value *matrix);
  void emitCleanupsDownTo(std::size_t depth);

  [[nodiscard]] bool reachable() const;
  [[nodiscard]] const Type *returnType() const;
  [[nodiscard]] static bool isMatrix(const Type *type);

  CodegenModule &cgm_;
  TypeContext &types_;
  llvm::Function *function_;
  const FunctionDef *definition_;
  llvm::Type* GetFunctionLLVMType(const Type *type);
  
  llvm::IRBuilder<> builder_;
  llvm::BasicBlock *entry_ = nullptr;
  std::vector<LifetimeScope> scopes_{};
  struct Loop {
    llvm::BasicBlock *exit;
    std::size_t scopeDepth; // Scopes to close on `break`.
  };
  std::vector<Loop> loops_{};
  std::unordered_map<std::uint32_t, Slot> locals_{};
};

} // namespace MemoLang
#endif // CODEGEN_FUNCTION_H_
