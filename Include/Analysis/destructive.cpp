// ---------------------------------------------------------------------------
// destructive.cpp: stage 4 of the compiler pipeline
//
//   ... -> Semantic analysis -> [Destructive analysis] -> (codegen)
//
// Follows each matrix variable through the program as Live, Consumed or
// MaybeConsumed (see the rules in destructive.h). Each statement updates a
// Flow: the state of every tracked variable at that point.
//   - `if`: both branches start from the same Flow; afterwards the two are
//     merged. A variable consumed on one branch only becomes MaybeConsumed.
//   - `while`: the body runs any number of times, so it is analyzed again and
//     again (without reporting) from the merge of "before the loop" and "end
//     of the body", until nothing changes. A final pass from that fixed point
//     reports errors. States only move up Live -> MaybeConsumed / Consumed, so
//     this takes a few rounds.
//   - `return` and `break` end a path; code after them isn't checked again.
//
// History: rewritten 2026-11-12 (destructive.cpp contained only #includes).
// ---------------------------------------------------------------------------
#include "Analysis/destructive.h"

#include <string>
#include <utility>

#include "Diag/diagengine.h"
#include "Semantics/sema.h"
#include "Types/Typecontext.h"

namespace MemoLang {
namespace {

// Loop analysis stops after this many rounds even if states still change
// (they can't: each round only moves states up a 3-level lattice).
constexpr int kMaxLoopRounds = 16;

bool isConsumable(const Expr &expr) {
  const Type *type = expr.resolvedType();
  return type != nullptr && type->isConsumable();
}

} // namespace

Destructive::Destructive(const sema::SemanticAnalyzer &sema,
                         DiagnosticEngine &diags)
    : sema_(sema), diags_(diags) {}

bool Destructive::isConsumed(const Expr &operand) const {
  return consumed_.count(&operand) != 0;
}

Destructive::ConsumeFlags
Destructive::consumeFlags(const BinaryExpr &matmul) const {
  return {isConsumed(matmul.lhs()), isConsumed(matmul.rhs())};
}

// ---------------------------------------------------------------------------
// Flows
// ---------------------------------------------------------------------------
Destructive::Flow Destructive::merge(const Flow &a, const Flow &b) {
  if (!a.reachable)
    return b;
  if (!b.reachable)
    return a;
  Flow result = a;
  auto combine = [&](std::uint32_t id, const VarState &other) {
    VarState &mine = result.vars[id]; // Missing means Live.
    if (mine.state == other.state) {
      if (!mine.consumedBy)
        mine.consumedBy = other.consumedBy;
      return;
    }
    mine.state = State::MaybeConsumed;
    if (!mine.consumedBy)
      mine.consumedBy = other.consumedBy;
  };
  for (const auto &[id, state] : b.vars)
    combine(id, state);
  // Variables only in `a` are Live in `b`.
  for (const auto &[id, state] : a.vars)
    if (b.vars.count(id) == 0 && state.state != State::Live)
      result.vars[id].state = State::MaybeConsumed;
  return result;
}

bool Destructive::sameStates(const Flow &a, const Flow &b) {
  if (a.reachable != b.reachable)
    return false;
  auto stateIn = [](const Flow &flow, std::uint32_t id) {
    auto it = flow.vars.find(id);
    return it == flow.vars.end() ? State::Live : it->second.state;
  };
  for (const auto &[id, state] : a.vars)
    if (stateIn(b, id) != state.state)
      return false;
  for (const auto &[id, state] : b.vars)
    if (stateIn(a, id) != state.state)
      return false;
  return true;
}

// ---------------------------------------------------------------------------
// Module, functions, statements
// ---------------------------------------------------------------------------
bool Destructive::analyzeModule(const Module &module) {
  std::size_t errorsBefore = diags_.errorCount();
  reportedVars_.clear();
  Flow moduleFlow; // Code outside any function.
  for (const auto &stmt : module.body) {
    if (stmt->kind() == NodeKind::FunctionDef)
      analyzeFunction(static_cast<const FunctionDef &>(*stmt));
    else if (moduleFlow.reachable)
      analyzeStatement(*stmt, moduleFlow);
  }
  return diags_.errorCount() == errorsBefore;
}

void Destructive::analyzeFunction(const FunctionDef &function) {
  if (function.isExtern())
    return;
  reportedVars_.clear();
  Flow flow; // Parameters start Live, like every variable not in the map.
  analyzeBody(function.body(), flow);
}

void Destructive::analyzeBody(std::span<const std::unique_ptr<Statement>> body,
                              Flow &flow) {
  for (const auto &stmt : body) {
    if (!flow.reachable)
      return; // Code after `return` / `break` never runs.
    analyzeStatement(*stmt, flow);
  }
}

void Destructive::analyzeStatement(const Statement &stmt, Flow &flow) {
  switch (stmt.kind()) {
  case NodeKind::ExprStmt:
    evaluate(static_cast<const ExprStmt &>(stmt).expr(), flow);
    break;
  case NodeKind::DeclareVar: {
    const auto &decl = static_cast<const DeclareVar &>(stmt);
    if (const Expr *init = decl.init()) {
      evaluate(*init, flow);
      consume(*init, decl, flow); // `matrix<float> b = a;` moves `a`.
    }
    if (auto id = sema_.declaredSymbol(decl))
      flow.vars.erase(id->value); // A new variable starts Live.
    break;
  }
  case NodeKind::AssignStmt: {
    const auto &assign = static_cast<const AssignStmt &>(stmt);
    if (assign.op() == AssignOp::Assign) {
      evaluate(assign.value(), flow);
      consume(assign.value(), assign, flow); // `b = a;` moves `a`.
    } else {
      // `m += n` reads both and writes `m`.
      if (assign.target().kind() == NodeKind::NameExpr)
        use(static_cast<const NameExpr &>(assign.target()), flow);
      evaluate(assign.value(), flow);
    }
    if (auto id = trackedSymbol(assign.target()))
      flow.vars.erase(*id); // A new value: usable again.
    break;
  }
  case NodeKind::ReturnStmt: {
    const auto &ret = static_cast<const ReturnStmt &>(stmt);
    if (const Expr *value = ret.value()) {
      evaluate(*value, flow);
      consume(*value, ret, flow);
    }
    flow.reachable = false;
    break;
  }
  case NodeKind::IfStmt: {
    const auto &ifStmt = static_cast<const IfStmt &>(stmt);
    evaluate(ifStmt.condition(), flow);
    Flow thenFlow = flow;
    analyzeBody(ifStmt.thenBody(), thenFlow);
    Flow elseFlow = std::move(flow);
    analyzeBody(ifStmt.elseBody(), elseFlow);
    flow = merge(thenFlow, elseFlow);
    break;
  }
  case NodeKind::WhileStmt:
    analyzeWhile(static_cast<const WhileStmt &>(stmt), flow);
    break;
  case NodeKind::BreakStmt:
    if (!breakFlows_.empty())
      breakFlows_.back().push_back(flow);
    flow.reachable = false;
    break;
  default:
    break; // Nested functions are rejected by semantic analysis.
  }
}

void Destructive::analyzeWhile(const WhileStmt &loop, Flow &flow) {
  // Without reporting, a loop entered in a state seen before ends the same way.
  if (!reporting_) {
    for (const LoopResult &result : loopCache_[&loop]) {
      if (sameStates(result.entry, flow)) {
        flow = result.exit;
        return;
      }
    }
  }
  const Flow entry = flow;

  // Find the state at the top of the loop: "before the loop" merged with
  // "end of the body", repeated until it stops changing.
  Flow head = entry;
  bool wasReporting = reporting_;
  reporting_ = false;
  for (int round = 0; round < kMaxLoopRounds; ++round) {
    Flow body = head;
    evaluate(loop.condition(), body);
    breakFlows_.emplace_back();
    analyzeBody(loop.body(), body);
    breakFlows_.pop_back();
    Flow next = merge(entry, body);
    if (sameStates(next, head))
      break;
    head = std::move(next);
  }
  reporting_ = wasReporting;

  // Final pass from the fixed point, reporting errors.
  Flow body = head;
  evaluate(loop.condition(), body);
  Flow exit = body; // The condition was false.
  breakFlows_.emplace_back();
  analyzeBody(loop.body(), body);
  for (const Flow &atBreak : breakFlows_.back())
    exit = merge(exit, atBreak);
  breakFlows_.pop_back();
  if (!reporting_)
    loopCache_[&loop].push_back({entry, exit});
  flow = std::move(exit);
}

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------
std::optional<std::uint32_t>
Destructive::trackedSymbol(const Expr &expr) const {
  if (expr.kind() != NodeKind::NameExpr || !isConsumable(expr))
    return std::nullopt;
  auto id = sema_.symbolFor(static_cast<const NameExpr &>(expr));
  if (!id)
    return std::nullopt;
  auto kind = sema_.symbol(*id).kind;
  if (kind != sema::Symbol::Kind::Variable &&
      kind != sema::Symbol::Kind::Parameter)
    return std::nullopt;
  return id->value;
}

void Destructive::evaluate(const Expr &expr, Flow &flow) {
  switch (expr.kind()) {
  case NodeKind::NameExpr:
    use(static_cast<const NameExpr &>(expr), flow);
    break;
  case NodeKind::InterpolatedString:
    for (const StringPart &part :
         static_cast<const InterpolatedString &>(expr).parts())
      if (part.value)
        evaluate(*part.value, flow);
    break;
  case NodeKind::UnaryExpr:
    evaluate(static_cast<const UnaryExpr &>(expr).operand(), flow);
    break;
  case NodeKind::BinaryExpr: {
    // Left-nested chains (a @ b @ c @ ...) are walked with a loop rather than
    // recursion, so their length doesn't matter (see sema.cpp inferBinary).
    // The order is the same: each left operand is finished before its right.
    std::vector<const BinaryExpr *> spine{
        &static_cast<const BinaryExpr &>(expr)};
    while (spine.back()->lhs().kind() == NodeKind::BinaryExpr)
      spine.push_back(static_cast<const BinaryExpr *>(&spine.back()->lhs()));
    evaluate(spine.back()->lhs(), flow);
    for (auto it = spine.rbegin(); it != spine.rend(); ++it) {
      const BinaryExpr &binary = **it;
      if (binary.op() == BinaryOp::MatMul)
        consume(binary.lhs(), binary, flow);
      evaluate(binary.rhs(), flow);
      if (binary.op() == BinaryOp::MatMul)
        consume(binary.rhs(), binary, flow);
    }
    break;
  }
  case NodeKind::CallExpr: {
    const auto &call = static_cast<const CallExpr &>(expr);
    bool isCopy = false;
    if (call.callee().kind() == NodeKind::NameExpr)
      if (auto id = sema_.symbolFor(
              static_cast<const NameExpr &>(call.callee())))
        isCopy = sema_.symbol(*id).kind == sema::Symbol::Kind::Builtin;
    for (const auto &arg : call.args()) {
      evaluate(*arg, flow);
      if (!isCopy) // copy(m) reads m; every other call takes ownership.
        consume(*arg, call, flow);
    }
    break;
  }
  case NodeKind::CastExpr:
    evaluate(static_cast<const CastExpr &>(expr).value(), flow);
    break;
  default:
    break; // Literals.
  }
}

void Destructive::use(const NameExpr &name, Flow &flow) {
  auto id = trackedSymbol(name);
  if (!id)
    return;
  auto it = flow.vars.find(*id);
  if (it == flow.vars.end() || it->second.state == State::Live)
    return;
  if (!reporting_)
    return;
  // One error per variable: after the first, later uses of the same variable
  // in this function aren't reported again.
  if (reportedVars_.insert(*id).second && reported_.insert(&name).second) {
    std::string quoted = quoteForMessage(name.name());
    const Node *consumer = it->second.consumedBy;
    if (it->second.state == State::Consumed)
      diags_.error(name.range(), "use of consumed matrix " + quoted);
    else
      diags_.error(name.range(),
                   "matrix " + quoted +
                       " may have been consumed (on another branch, or in "
                       "an earlier loop iteration)");
    if (consumer)
      diags_.note(consumer->range(),
                  quoted + " is consumed here; to keep using it, use copy(" +
                      name.name() + ") there");
  }
  // Treat it as usable again from here on, so one mistake gives one error.
  flow.vars.erase(it);
}

void Destructive::consume(const Expr &operand, const Node &consumer,
                          Flow &flow) {
  if (!isConsumable(operand))
    return;
  consumed_.insert(&operand);
  if (auto id = trackedSymbol(operand))
    flow.vars[*id] = VarState{State::Consumed, &consumer};
}

} // namespace MemoLang
