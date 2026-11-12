#ifndef DESTRUCTIVE_H_
#define DESTRUCTIVE_H_

#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "AST/AST.h"

// Destructive-read (consumption) analysis: the core of MemoLang.
//
// A matrix owns a GPU buffer. When an operation has exclusive use of a matrix
// variable, it consumes it: the buffer can be freed or reused right away, and
// the variable may not be read again until it is given a new value.
//
// Rules (2026-11-12; adjust them here as the design evolves):
//   - A matrix variable is consumed when it is
//       * an operand of `@`              (a @ b consumes a and b),
//       * an argument of a function call (ownership moves to the callee),
//       * moved into another variable    (matrix<float> b = a;  b = a;),
//       * returned                       (return a;).
//   - Reading it without exclusive use doesn't consume it: m * 2.0, -m, m + n.
//   - copy(m) gives a fresh matrix and leaves m alone. Use it to keep a
//     matrix that an operation would otherwise consume: copy(m) @ v.
//   - Using a consumed matrix is an error, with a note where it was consumed.
//     Assigning the variable a new value makes it usable again.
//   - After `if`, a matrix consumed on only one branch is "maybe consumed";
//     using it is an error too. Loops are analyzed until the states stop
//     changing, so a matrix consumed in the body is caught on the next
//     iteration.
//   - Temporaries (results of `@`, calls, copy()) are always consumed by the
//     operation that uses them; they have no name, so nothing can reuse them.
//
// For code generation, isConsumed() and consumeFlags() tell which operands of
// each `@` and which call arguments hand over their buffer: the consume_a /
// consume_b flags from the report.
//
// Run it after semantic analysis has succeeded: it uses the symbol each name
// resolved to (sema::SemanticAnalyzer::symbolFor), so shadowed variables are
// told apart, and each expression's type, to know which values are matrices.
//
// History: the first version (2026-11-09) walked the tree and kept a
// name -> State map, but never consumed anything. Rewritten 2026-11-12 with
// the rules above; State (Live / Consumed / MaybeConsumed) is still the
// Visitor::State enum from AST.h.

namespace MemoLang {
class DiagnosticEngine;
namespace sema {
class SemanticAnalyzer;
}

class Destructive {
public:
  using State = Visitor::State;

  // `sema` must already have analyzed the module. Both must outlive this.
  Destructive(const sema::SemanticAnalyzer &sema, DiagnosticEngine &diags);

  // Checks every function and the module-level code. Returns true if no
  // matrix is used after being consumed.
  bool analyzeModule(const Module &module);

  // Does `operand` (an operand of `@`, a call argument, a moved or returned
  // value) hand its buffer over, so the runtime may free or reuse it?
  [[nodiscard]] bool isConsumed(const Expr &operand) const;

  struct ConsumeFlags {
    bool lhs = false; // consume_a
    bool rhs = false; // consume_b
  };
  [[nodiscard]] ConsumeFlags consumeFlags(const BinaryExpr &matmul) const;

private:
  struct VarState {
    State state = State::Live;
    const Node *consumedBy = nullptr; // Where it was consumed, for the note.
  };
  // What is known at one point of the program, keyed by SymbolId::value.
  // Variables not in the map are live.
  struct Flow {
    std::unordered_map<std::uint32_t, VarState> vars;
    bool reachable = true; // False after `return` or `break`.
  };

  static Flow merge(const Flow &a, const Flow &b);
  static bool sameStates(const Flow &a, const Flow &b);

  void analyzeFunction(const FunctionDef &function);
  void analyzeBody(std::span<const std::unique_ptr<Statement>> body,
                   Flow &flow);
  void analyzeStatement(const Statement &stmt, Flow &flow);
  void analyzeWhile(const WhileStmt &loop, Flow &flow);
  void evaluate(const Expr &expr, Flow &flow);
  void use(const NameExpr &name, Flow &flow);
  void consume(const Expr &operand, const Node &consumer, Flow &flow);
  // The tracked variable `expr` names: a matrix variable or parameter.
  [[nodiscard]] std::optional<std::uint32_t>
  trackedSymbol(const Expr &expr) const;

  // A loop worked out without reporting: its state on entry and on exit.
  struct LoopResult {
    Flow entry;
    Flow exit;
  };

  const sema::SemanticAnalyzer &sema_;
  DiagnosticEngine &diags_;
  bool reporting_ = true; // Off while a loop is iterated to its fixed point.
  std::unordered_set<const Expr *> reported_;
  // Variables already reported in the current function: one error each.
  std::unordered_set<std::uint32_t> reportedVars_;
  // Loop results by loop and entry state. Without this, every round of every
  // enclosing loop re-analyzed the nested loops: exponential in the nesting
  // depth (100 nested loops never finished).
  std::unordered_map<const WhileStmt *, std::vector<LoopResult>> loopCache_;
  std::unordered_set<const Expr *> consumed_;
  std::vector<std::vector<Flow>> breakFlows_; // One list per enclosing loop.
};

} // namespace MemoLang

#endif // DESTRUCTIVE_H_
