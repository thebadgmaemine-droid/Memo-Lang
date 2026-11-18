#ifndef CALLUTILS_H_
#define CALLUTILS_H_
// Helpers for lowering calls: the order arguments are evaluated in, and which
// parameter each argument of a call (positional or named) goes to.
//
// Evaluation order is a language rule, not a target ABI choice: destructive
// reads make it observable (in `f(a @ b, g(a))` the order decides which use
// consumes `a`), so MemoLang always evaluates arguments left to right, in
// source order, like the interpreter and destructive analysis do.
//
// 2026-11-17: implemented. The draft used `EvaluationOrder` but declared the
// enum as `DirEvaluation`, and took a C++ `TargetCXXABI`, which MemoLang has
// no use for.
#include <cstddef>
#include <vector>

#include "AST/AST.h"

namespace MemoLang {

enum class EvaluationOrder {
  LeftToRight, // Source order: the only order MemoLang uses.
  RightToLeft, // For testing purposes
};

// The order `call`'s arguments are evaluated in.
[[nodiscard]] constexpr EvaluationOrder
getCallArgEvaluationOrder(const CallExpr &) noexcept {
  return EvaluationOrder::LeftToRight; // ALways left to right
}

[[nodiscard]] constexpr bool
shouldEvaluateArgsLeftToRight(EvaluationOrder order) noexcept {
  return order == EvaluationOrder::LeftToRight; // Always left to right
}

// For each argument of `call`, in source order, the index of the parameter
// of `callee` it is passed to: arguments are positional unless named
// (`sub(b = 1, a = 10)`). Semantic analysis has already rejected unknown,
// duplicate and missing arguments, so every index is valid.
[[nodiscard]] inline std::vector<std::size_t>
mapCallArguments(const CallExpr &call, const FunctionDef &callee) {
  auto params = callee.params();
  auto names = call.paramNames();
  std::vector<std::size_t> paramForArg(call.args().size());
  for (std::size_t i = 0; i < paramForArg.size(); ++i) {
    paramForArg[i] = i;
    if (names.empty() || names[i].empty())
      continue;
    for (std::size_t p = 0; p < params.size(); ++p)
      if (params[p].name == names[i])
        paramForArg[i] = p;
  }
  return paramForArg;
}

} // namespace MemoLang

#endif // CALLUTILS_H_
