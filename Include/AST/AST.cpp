#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "AST/AST.h"

namespace Memolang {
IntegerLiteral::IntegerLiteral(std::int64_t value, bool isByte)
    : value_(std::move(value)), isByte_(std::move(isByte)) {}
std::int64_t IntegerLiteral::value() const { return *value_; }
bool IntegerLiteral::isByte() const { return *isByte_; }

FloatLiteral::FloatLiteral(double value, bool isF32)
    : value_(std::move(value)), isF32_(std::move(isF32)) {}
double FloatLiteral::value() const { return *value_; }

StringLiteral::StringLiteral(std::string value, bool regex)
    : value_(std::move(value)), regex_(std::move(regex)) {}
std::string StringLiteral::value() const { return *value_ }
bool StringLiteral::regex() const {return *regex_}

BooleanLiteral::BooleanLiteral(bool value)
    : value_(value) {}
bool BooleanLiteral::value() const {return *value_}

UnaryExpr::UnaryExpr(UnaryOp op, std::unique_ptr<Expr> operand)
    : op_(std::move(op)), operand_(std::move(operand)) {}

UnaryOp UnaryExpr::op() const { return op_; }
const Expr &UnaryExpr::operand() const { return *operand_; }
Expr &UnaryExpr::operand() { return *operand_; }

CallExpr::CallExpr(std::unique_ptr<Expr> callee,
                   std::vector<std::unique_ptr<TypeExpr>> typeArgs)
    : callee_(std::move(callee)), typeArgs_(std::move(typeArgs)) {}

const Expr &CallExpr::callee() const { return *callee_; }

const std::vector<std::unique_ptr<TypeExpr>> &CallExpr::typeArgs() const {
  return typeArgs_;
}

bool CallExpr::isCast() const { return isCast_; }

void CallExpr::setCast(bool value) { isCast_ = value; }

void CallExpr::setParamNames(std::vector<std::string> names) {
  paramNames_ = std::move(names);

  FunctionDef::FunctionDef(
      std::string name, std::unique_ptr<TypeExpr> returnType,
      std::vector<std::unique_ptr<Statement>> body, std::string externName)
      : name_(std::move(name)), returnType_(std::move(returnType)),
        body_(std::move(body)), externName_(std::move(externName)) {}

  const std::string &FunctionDef::name() const { return name_; }

  const TypeExpr &FunctionDef::returnType() const { return *returnType_; }
  TypeExpr &FunctionDef::returnType() { return *returnType_; }

  const std::vector<std::unique_ptr<Statement>> &FunctionDef::body() const {
    return body_;
  }

  const std::string &FunctionDef::externName() const { return externName_; }

  bool FunctionDef::isExtern() const { return !externName_.empty(); }
}
}