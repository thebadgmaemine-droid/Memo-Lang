#ifndef DESTRUCTIVE_H_
#define DESTRUCTIVE_H_

#include <string>
#include <unordered_map>

#include "AST/AST.h"

namespace MemoLang {

class Destructive final : public Visitor {
public:
  using State = Visitor::State;
  // state maps to a hash table
  using StateMap = std::unordered_map<std::string, State>;
// templ
  explicit Destructive(const Module &module) { initialize(module); }
// Initializing: clears the state hash then gets state based on visit
  void initialize(const Module &module) {
    states_.clear();
    for (const auto &statement : module.body)
      statement->accept(*this);
  }

  [[nodiscard]] const StateMap &states() const noexcept { return states_; }

  void visit(const TypeExpr &) override {}
  void visit(const NameExpr &) override {}
  void visit(const IntegerLiteral &) override {}
  void visit(const FloatLiteral &) override {}
  void visit(const StringLiteral &) override {}
  void visit(const BooleanLiteral &) override {}
  // For corresponding tokens

  void visit(const InterpolatedString &expr) override {
    for (const auto &part : expr.parts())
      if (part.value)
        part.value->accept(*this);
  }

  void visit(const UnaryExpr &expr) override { expr.operand().accept(*this); }

  void visit(const BinaryExpr &expr) override {
    expr.lhs().accept(*this);
    expr.rhs().accept(*this);
  }

  void visit(const CallExpr &expr) override {
    expr.callee().accept(*this);
    for (const auto &arg : expr.args())
      arg->accept(*this);
  }

  void visit(const CastExpr &expr) override { expr.value().accept(*this); }

  void visit(const ExprStmt &stmt) override { stmt.expr().accept(*this); }

  void visit(const DeclareVar &stmt) override {
    states_.insert_or_assign(stmt.name(), State::Live);
    if (const Expr *init = stmt.init())
      init->accept(*this);
  }

  void visit(const FunctionDef &stmt) override {
    for (const auto &param : stmt.params())
      states_.insert_or_assign(param.name, State::Live);
    for (const auto &bodyStatement : stmt.body())
      bodyStatement->accept(*this);
  }

  void visit(const AssignStmt &stmt) override {
    stmt.target().accept(*this);
    stmt.value().accept(*this);
  }

  void visit(const ReturnStmt &stmt) override {
    if (const Expr *value = stmt.value())
      value->accept(*this);
  }

  void visit(const IfStmt &stmt) override {
    stmt.condition().accept(*this);
    for (const auto &bodyStatement : stmt.thenBody())
      bodyStatement->accept(*this);
    for (const auto &bodyStatement : stmt.elseBody())
      bodyStatement->accept(*this);
  }

  void visit(const WhileStmt &stmt) override {
    stmt.condition().accept(*this);
    for (const auto &bodyStatement : stmt.body())
      bodyStatement->accept(*this);
  }

  void visit(const BreakStmt &) override {}

private:
  StateMap states_;
};

} // namespace MemoLang

#endif // DESTRUCTIVE_H_
