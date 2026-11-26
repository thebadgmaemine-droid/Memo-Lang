#ifndef MEMOLANG_INTERPRETER_H_
#define MEMOLANG_INTERPRETER_H_

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "AST/AST.h"
#include "Runtime/runtime.h"
#include "Support/Compiler.h"

// The interpreter: runs a checked MemoLang program by walking its tree, and
// calls the matrix runtime for matrix work. It is how MemoLang programs run
// until code generation exists, and how the memory effect of destructive
// reads is measured.
//
// Two modes, for comparing:
//   - Destructive: a matrix variable's buffer is released the moment the
//     variable is consumed (`@`, a call, a move, `return`), using the consume
//     flags from destructive analysis (Analysis/destructive.h).
//   - Baseline: what a language without destructive reads does, like C++ or
//     Rust: a named matrix is released when its scope ends or the variable is
//     given a new value. copy() costs nothing here (it just shares the
//     matrix), because without destructive reads nobody needs to copy.
// In both modes a temporary (the result of `a @ b` used by another `@`) is
// released as soon as it has been used, as in C++ and in PyTorch.
//
// Built-in externs a program can declare and call:
//   extern def random_matrix(int rows, int cols) -> matrix<f32>;   (or float)
//   extern def zeros_matrix(int rows, int cols) -> matrix<f32>;
//   extern def identity_matrix(int n) -> matrix<f32>;
//   extern def matrix_sum(matrix<f32> m) -> f32;
//
// History: added 2026-11-12.

namespace MemoLang {
class Destructive;
namespace sema {
class SemanticAnalyzer;
}

class Interpreter {
public:
  enum class Mode { Destructive, Baseline };

  struct Result {
    bool ok = false;
    std::string value; // What main() returned, as text.
    std::string error; // Set when !ok.
  };

  // `sema` and `destructive` must have analyzed `module` without errors.
  Interpreter(const Module &module MEMOLANG_LIFETIMEBOUND, const sema::SemanticAnalyzer &sema MEMOLANG_LIFETIMEBOUND,
              const Destructive &destructive MEMOLANG_LIFETIMEBOUND, runtime::Runtime &runtime MEMOLANG_LIFETIMEBOUND,
              Mode mode);

  // Runs the module-level statements, then main() (no parameters).
  Result run();

  // Every variable's value lives in a Cell owned by its scope. A pointer
  // (`&x`) refers to the cell without owning it, so a variable's matrix is
  // still freed when its scope ends, and following a pointer to a variable
  // that no longer exists is a run-time error instead of a crash.
  struct Cell;
  struct Pointer {
    std::weak_ptr<Cell> cell;
    bool hasTarget = false; // False for a pointer that was never given one.
  };
  using Value = std::variant<std::monostate, std::int64_t, double, bool,
                             std::string, runtime::MatrixRef, Pointer>;
  struct Cell {
    Value value;
  };

private:
  enum class Flow { Normal, Break, Return };
  using Scope = std::unordered_map<std::uint32_t, std::shared_ptr<Cell>>;
  struct Frame {
    const FunctionDef *function = nullptr; // Null for module-level code.
    std::vector<Scope> scopes;
  };

  Value callFunction(const FunctionDef &function, std::vector<Value> args);
  Value callExtern(const FunctionDef &function, std::vector<Value> &args);

  // execBody opens a block scope; execStatements runs in the current one.
  Flow execBody(std::span<const std::unique_ptr<Statement>> body);
  Flow execStatements(std::span<const std::unique_ptr<Statement>> body);
  Flow exec(const Statement &stmt);

  // In destructive mode, evaluating a name that destructive analysis marked
  // as consumed moves the matrix out of the variable, so its buffer is freed
  // as soon as the operation using it is done.
  Value eval(const Expr &expr);
  Value evalBinary(const BinaryExpr &binary);
  Value evalUnary(const UnaryExpr &unary);
  Value evalCall(const CallExpr &call);
  Value evalCast(const CastExpr &cast);
  // The operand types tell u64 bits apart from int64 ones.
  Value applyBinary(BinaryOp op, const Value &lhs, const Value &rhs,
                    const Type *lhsType, const Type *rhsType);
  // Converts a value to a declared type (int to float, i32 wrap-around, ...),
  // as semantic analysis allows. Also used for casts. `from` is the value's
  // static type, when known (a u64 converts to float as unsigned).
  static Value convertTo(Value value, const Type *type,
                         const Type *from = nullptr);

  Value *lookup(std::uint32_t symbol);
  std::shared_ptr<Cell> lookupCell(std::uint32_t symbol);
  void declare(std::uint32_t symbol, Value value);
  std::uint32_t symbolOf(const NameExpr &name) const;
  // The cell an assignment target names: a variable, or `*p`. `type` gets
  // the target's static type.
  std::shared_ptr<Cell> cellFor(const Expr &target, const Type *&type);
  // The cell `pointer` refers to; fails if it has none or it is gone.
  static std::shared_ptr<Cell> follow(const Value &pointer);

  const Module &module_;
  const sema::SemanticAnalyzer &sema_;
  const Destructive &destructive_;
  runtime::Runtime &runtime_;
  Mode mode_;
  Scope globals_;
  std::vector<Frame> frames_;
  Value returnValue_;
  int callDepth_ = 0;
};

// One measured run of main().
struct RunReport {
  Interpreter::Result result;
  runtime::MemoryStats stats; // After the run: everything should be freed.
  double totalMs = 0;         // Wall-clock time of the whole run.
};

// Runs main() in `mode` with a fresh runtime (same random numbers every
// time) and measures it.
RunReport runProgram(const Module &module, const sema::SemanticAnalyzer &sema,
                     const Destructive &destructive, Interpreter::Mode mode);

} // namespace MemoLang

#endif // MEMOLANG_INTERPRETER_H_
