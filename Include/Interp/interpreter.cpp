// ---------------------------------------------------------------------------
// interpreter.cpp: runs checked MemoLang programs (see interpreter.h)
//
// Values are ints (int64), floats (double), bools, strings and matrices.
// Matrices are reference-counted handles (runtime::MatrixRef): a buffer is
// freed when the last reference goes away. So the two modes differ only in
// when a variable lets go of its matrix:
//   - Destructive: at the moment the variable is consumed. Destructive
//     analysis marks each consumed use of a name (Destructive::isConsumed),
//     and evaluating such a name moves the matrix out of the variable.
//   - Baseline: when its scope ends, or when it is given a new value.
// Temporaries die as soon as the operation using them returns, in both modes.
//
// Values are converted to declared types the way semantic analysis allows
// (int to float, wrap-around for i32 and byte, f32 rounding).
//
// History: added 2026-11-12.
// ---------------------------------------------------------------------------
#include "Interp/interpreter.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <utility>

#include "Analysis/destructive.h"
#include "Semantics/sema.h"
#include "Types/Typecontext.h"
#include "Support/Compiler.h"

namespace MemoLang {
namespace {

using runtime::ElementType;
using runtime::MatrixRef;
using runtime::RuntimeError;
using Value = Interpreter::Value;

// Deep recursion in the program would overflow the interpreter's own stack.
constexpr int kMaxCallDepth = 2000;

[[noreturn]] void fail(std::string message) {
  throw RuntimeError{std::move(message)};
}

bool isInt(const Value &v) { return std::holds_alternative<std::int64_t>(v); }
bool isFloat(const Value &v) { return std::holds_alternative<double>(v); }
bool isMatrix(const Value &v) { return std::holds_alternative<MatrixRef>(v); }

// Ints are stored as int64; a u64 keeps its bits there, so code that reads
// one as a number has to know its type.
bool isU64(const Type *type) {
  const auto *intType = dynCast<IntType>(type);
  return intType && intType->bits() == 64 && !intType->isSigned();
}

double asDouble(const Value &v, const Type *type = nullptr) {
  if (isInt(v) && isU64(type))
    return static_cast<double>(
        static_cast<std::uint64_t>(std::get<std::int64_t>(v)));
  if (isInt(v))
    return static_cast<double>(std::get<std::int64_t>(v));
  if (isFloat(v))
    return std::get<double>(v);
  if (std::holds_alternative<bool>(v))
    return std::get<bool>(v) ? 1.0 : 0.0;
  fail("expected a number");
}

std::int64_t asInt(const Value &v) {
  if (isInt(v))
    return std::get<std::int64_t>(v);
  if (std::holds_alternative<bool>(v))
    return std::get<bool>(v) ? 1 : 0;
  fail("expected an integer");
}

bool asBool(const Value &v) {
  if (std::holds_alternative<bool>(v))
    return std::get<bool>(v);
  fail("expected a bool");
}

const MatrixRef &asMatrix(const Value &v MEMOLANG_LIFETIMEBOUND) {
  if (!isMatrix(v) || !std::get<MatrixRef>(v))
    fail("expected a matrix");
  return std::get<MatrixRef>(v);
}

std::int64_t wrapInt(std::int64_t value, unsigned bits, bool isSigned) {
  if (bits >= 64)
    return value;
  std::uint64_t mask = (std::uint64_t{1} << bits) - 1;
  std::uint64_t u = static_cast<std::uint64_t>(value) & mask;
  if (isSigned && ((u >> (bits - 1)) & 1))
    u |= ~mask;
  return static_cast<std::int64_t>(u);
}

// Integer arithmetic wraps around on overflow instead of being undefined.
std::int64_t wrapAdd(std::int64_t a, std::int64_t b) {
  return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) +
                                   static_cast<std::uint64_t>(b));
}
std::int64_t wrapSub(std::int64_t a, std::int64_t b) {
  return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) -
                                   static_cast<std::uint64_t>(b));
}
std::int64_t wrapMul(std::int64_t a, std::int64_t b) {
  return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) *
                                   static_cast<std::uint64_t>(b));
}

// `//` and `%` round toward minus infinity, so they agree: a == (a // b) * b + a % b.
std::int64_t floorDiv(std::int64_t a, std::int64_t b) {
  if (b == 0)
    fail("integer division by zero");
  if (a == std::numeric_limits<std::int64_t>::min() && b == -1)
    return a; // Wraps around.
  std::int64_t q = a / b;
  if ((a % b != 0) && ((a < 0) != (b < 0)))
    --q;
  return q;
}
std::int64_t floorMod(std::int64_t a, std::int64_t b) {
  if (b == 0)
    fail("integer modulo by zero");
  if (b == -1)
    return 0;
  std::int64_t r = a % b;
  if (r != 0 && ((r < 0) != (b < 0)))
    r += b;
  return r;
}
// u64 `//`, `%` and `**`: plain unsigned math, every exponent is valid.
std::int64_t unsignedOp(BinaryOp op, std::int64_t a, std::int64_t b) {
  auto ua = static_cast<std::uint64_t>(a);
  auto ub = static_cast<std::uint64_t>(b);
  std::uint64_t r = 1;
  switch (op) {
  case BinaryOp::FloorDiv:
    if (ub == 0)
      fail("integer division by zero");
    r = ua / ub;
    break;
  case BinaryOp::Mod:
    if (ub == 0)
      fail("integer modulo by zero");
    r = ua % ub;
    break;
  default: // Pow
    for (; ub > 0; ub >>= 1, ua *= ua)
      if (ub & 1)
        r *= ua;
    break;
  }
  return static_cast<std::int64_t>(r);
}

std::int64_t intPow(std::int64_t base, std::int64_t exponent) {
  if (exponent < 0)
    fail("negative exponent for an integer power; use a float");
  std::int64_t result = 1;
  while (exponent > 0) {
    if (exponent & 1)
      result = wrapMul(result, base);
    base = wrapMul(base, base);
    exponent >>= 1;
  }
  return result;
}

ElementType elementTypeOf(const Type *matrixType) {
  if (const auto *m = dynCast<MatrixType>(matrixType))
    if (const auto *f = dynCast<FloatType>(m->element()))
      if (f->bits() == 32)
        return ElementType::F32;
  return ElementType::F64;
}

const Type *returnTypeOf(const FunctionDef &function) {
  const auto *type = dynCast<FunctionType>(function.resolvedType());
  return type ? type->returnType() : nullptr;
}

Value defaultValue(const Type *type) {
  if (!type)
    return std::monostate{};
  switch (type->kind()) {
  case Type::Kind::Int: return std::int64_t{0};
  case Type::Kind::Float: return 0.0;
  case Type::Kind::Bool: return false;
  case Type::Kind::Str: return std::string();
  case Type::Kind::Pointer: return Interpreter::Pointer{}; // Points nowhere.
  default: return std::monostate{}; // A matrix without a value.
  }
}

std::string toText(const Value &v, const Type *type) {
  if (isInt(v) && isU64(type))
    return std::to_string(static_cast<std::uint64_t>(std::get<std::int64_t>(v)));
  if (isInt(v))
    return std::to_string(std::get<std::int64_t>(v));
  if (isFloat(v)) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.10g", std::get<double>(v));
    return buffer;
  }
  if (std::holds_alternative<bool>(v))
    return std::get<bool>(v) ? "true" : "false";
  if (std::holds_alternative<std::string>(v))
    return "\"" + std::get<std::string>(v) + "\"";
  if (isMatrix(v) && std::get<MatrixRef>(v)) {
    const auto &m = std::get<MatrixRef>(v);
    return "matrix " + std::to_string(m->rows()) + "x" +
           std::to_string(m->cols());
  }
  return "nothing";
}

} // namespace

Interpreter::Interpreter(const Module &module,
                         const sema::SemanticAnalyzer &sema,
                         const Destructive &destructive,
                         runtime::Runtime &runtime, Mode mode)
    : module_(module), sema_(sema), destructive_(destructive),
      runtime_(runtime), mode_(mode) {}

Value Interpreter::convertTo(Value value, const Type *type, const Type *from) {
  if (!type || std::holds_alternative<std::monostate>(value))
    return value;
  if (const auto *intType = dynCast<IntType>(type)) {
    std::int64_t v = 0;
    if (isFloat(value) && isU64(type)) {
      double d = std::get<double>(value);
      if (!(d > -1.0 && d < 1.8446744073709552e19)) // NaN fails too.
        fail("float value out of range for an integer");
      return static_cast<std::int64_t>(static_cast<std::uint64_t>(d));
    }
    if (isFloat(value)) {
      double d = std::get<double>(value);
      if (!std::isfinite(d) || d >= 9.2233720368547758e18 ||
          d < -9.2233720368547758e18)
        fail("float value out of range for an integer");
      v = static_cast<std::int64_t>(d); // Truncates toward zero.
    } else {
      v = asInt(value);
    }
    return wrapInt(v, intType->bits(), intType->isSigned());
  }
  if (const auto *floatType = dynCast<FloatType>(type)) {
    double d = asDouble(value, from);
    if (floatType->bits() == 32)
      d = static_cast<double>(static_cast<float>(d));
    return d;
  }
  if (type->kind() == Type::Kind::Bool) {
    if (std::holds_alternative<bool>(value))
      return value;
    return asDouble(value) != 0.0;
  }
  return value;
}

// ---------------------------------------------------------------------------
// Variables
// ---------------------------------------------------------------------------
std::uint32_t Interpreter::symbolOf(const NameExpr &name) const {
  auto id = sema_.symbolFor(name);
  if (!id)
    fail("internal error: '" + name.name() + "' was never resolved");
  return id->value;
}

std::shared_ptr<Interpreter::Cell>
Interpreter::lookupCell(std::uint32_t symbol) {
  if (!frames_.empty()) {
    auto &scopes = frames_.back().scopes;
    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
      auto found = it->find(symbol);
      if (found != it->end())
        return found->second;
    }
  }
  auto found = globals_.find(symbol);
  return found == globals_.end() ? nullptr : found->second;
}

Value *Interpreter::lookup(std::uint32_t symbol) {
  // The scope keeps owning the cell, so the address stays valid.
  Cell *cell = lookupCell(symbol).get();
  return cell ? &cell->value : nullptr;
}

void Interpreter::declare(std::uint32_t symbol, Value value) {
  // A fresh cell each time: a pointer to the variable's previous life (an
  // earlier loop iteration or call) doesn't see the new one.
  auto cell = std::make_shared<Cell>(Cell{std::move(value)});
  if (frames_.empty() || frames_.back().scopes.empty())
    globals_[symbol] = std::move(cell);
  else
    frames_.back().scopes.back()[symbol] = std::move(cell);
}

std::shared_ptr<Interpreter::Cell> Interpreter::follow(const Value &pointer) {
  const auto *p = std::get_if<Pointer>(&pointer);
  if (!p)
    fail("expected a pointer");
  if (!p->hasTarget)
    fail("dereferencing a pointer that was never given a value");
  auto cell = p->cell.lock();
  if (!cell)
    fail("dereferencing a dangling pointer: the variable it points to no "
         "longer exists");
  return cell;
}

std::shared_ptr<Interpreter::Cell> Interpreter::cellFor(const Expr &target,
                                                        const Type *&type) {
  if (target.kind() == NodeKind::NameExpr) {
    const auto &name = static_cast<const NameExpr &>(target);
    std::uint32_t id = symbolOf(name);
    auto cell = lookupCell(id);
    if (!cell)
      fail("internal error: '" + name.name() + "' has no storage");
    type = sema_.symbol(SymbolId{id}).type;
    return cell;
  }
  // `*p`: sema allows only a variable or a dereference here.
  const auto &deref = static_cast<const UnaryExpr &>(target);
  type = target.resolvedType();
  return follow(eval(deref.operand()));
}

// ---------------------------------------------------------------------------
// Running
// ---------------------------------------------------------------------------
Interpreter::Result Interpreter::run() {
  Result result;
  callDepth_ = 0;
  try {
    frames_.clear();
    globals_.clear();
    frames_.push_back(Frame{}); // Module-level code; its names are globals.
    const FunctionDef *mainFunction = nullptr;
    for (const auto &stmt : module_.body) {
      if (stmt->kind() == NodeKind::FunctionDef) {
        const auto &function = static_cast<const FunctionDef &>(*stmt);
        if (function.name() == "main")
          mainFunction = &function;
        continue;
      }
      exec(*stmt);
    }
    if (!mainFunction)
      fail("there is no main() function to run");
    if (!mainFunction->params().empty())
      fail("main() must not take parameters");
    result.value = toText(callFunction(*mainFunction, {}),
                          returnTypeOf(*mainFunction));
    result.ok = true;
  } catch (const RuntimeError &error) {
    result.error = error.message;
  }
  // Release everything, so the statistics show what was never freed.
  frames_.clear();
  globals_.clear();
  returnValue_ = std::monostate{};
  return result;
}

Value Interpreter::callFunction(const FunctionDef &function,
                                std::vector<Value> args) {
  if (function.isExtern())
    return callExtern(function, args);
  if (callDepth_ >= kMaxCallDepth)
    fail("too many nested calls (more than " + std::to_string(kMaxCallDepth) +
         "); is there endless recursion?");
  ++callDepth_;
  frames_.push_back(Frame{&function, {}});
  frames_.back().scopes.emplace_back();
  const auto &params = sema_.parameterSymbols(function);
  for (std::size_t i = 0; i < params.size() && i < args.size(); ++i)
    declare(params[i].value, std::move(args[i]));
  args.clear();

  Flow flow = execStatements(function.body());
  Value result =
      flow == Flow::Return ? std::move(returnValue_) : Value{std::monostate{}};
  returnValue_ = std::monostate{};
  frames_.pop_back(); // Locals are released here.
  --callDepth_;
  return result;
}

Value Interpreter::callExtern(const FunctionDef &function,
                              std::vector<Value> &args) {
  const std::string &name = function.name();
  const Type *returns = returnTypeOf(function);
  auto intArg = [&](std::size_t i) { return asInt(args.at(i)); };
  if (name == "random_matrix" && args.size() == 2)
    return runtime_.random(intArg(0), intArg(1), elementTypeOf(returns));
  if (name == "zeros_matrix" && args.size() == 2)
    return runtime_.zeros(intArg(0), intArg(1), elementTypeOf(returns));
  if (name == "identity_matrix" && args.size() == 1)
    return runtime_.identity(intArg(0), elementTypeOf(returns));
  if (name == "matrix_sum" && args.size() == 1)
    return convertTo(runtime_.sum(*asMatrix(args[0])), returns);
  fail("extern '" + name + "' has no implementation in the interpreter");
}

Interpreter::Flow
Interpreter::execBody(std::span<const std::unique_ptr<Statement>> body) {
  frames_.back().scopes.emplace_back();
  Flow flow = Flow::Normal;
  try {
    flow = execStatements(body);
  } catch (...) {
    frames_.back().scopes.pop_back();
    throw;
  }
  frames_.back().scopes.pop_back(); // Block locals are released here.
  return flow;
}

Interpreter::Flow
Interpreter::execStatements(std::span<const std::unique_ptr<Statement>> body) {
  for (const auto &stmt : body) {
    Flow flow = exec(*stmt);
    if (flow != Flow::Normal)
      return flow;
  }
  return Flow::Normal;
}

Interpreter::Flow Interpreter::exec(const Statement &stmt) {
  switch (stmt.kind()) {
  case NodeKind::ExprStmt:
    eval(static_cast<const ExprStmt &>(stmt).expr());
    return Flow::Normal;
  case NodeKind::DeclareVar: {
    const auto &decl = static_cast<const DeclareVar &>(stmt);
    auto id = sema_.declaredSymbol(decl);
    if (!id)
      fail("internal error: '" + decl.name() + "' was never declared");
    Value value = decl.init() ? eval(*decl.init())
                              : defaultValue(decl.resolvedType());
    declare(id->value,
            convertTo(std::move(value), decl.resolvedType(),
                      decl.init() ? decl.init()->resolvedType() : nullptr));
    return Flow::Normal;
  }
  case NodeKind::AssignStmt: {
    const auto &assign = static_cast<const AssignStmt &>(stmt);
    // `x = v` and `*p = v` evaluate v first; `x op= v` and `*p op= v` find
    // the target and read it first (codegen does the same).
    const Type *type = nullptr;
    std::shared_ptr<Cell> cell;
    Value value;
    const Type *from = nullptr; // A compound result needs no source type.
    if (assign.op() == AssignOp::Assign) {
      value = eval(assign.value());
      from = assign.value().resolvedType();
      cell = cellFor(assign.target(), type);
    } else {
      cell = cellFor(assign.target(), type);
      Value current = cell->value;
      Value rhs = eval(assign.value());
      BinaryOp op = BinaryOp::Add;
      switch (assign.op()) {
      case AssignOp::Sub: op = BinaryOp::Sub; break;
      case AssignOp::Mul: op = BinaryOp::Mul; break;
      case AssignOp::Div: op = BinaryOp::Div; break;
      case AssignOp::Mod: op = BinaryOp::Mod; break;
      default: break;
      }
      value = applyBinary(op, current, rhs, type, assign.value().resolvedType());
    }
    // Replacing the old value releases its matrix, in both modes.
    cell->value = convertTo(std::move(value), type, from);
    return Flow::Normal;
  }
  case NodeKind::ReturnStmt: {
    const auto &ret = static_cast<const ReturnStmt &>(stmt);
    const FunctionDef *function = frames_.back().function;
    returnValue_ = ret.value() ? convertTo(eval(*ret.value()),
                                           function ? returnTypeOf(*function)
                                                    : nullptr,
                                           ret.value()->resolvedType())
                               : Value{std::monostate{}};
    return Flow::Return;
  }
  case NodeKind::IfStmt: {
    const auto &ifStmt = static_cast<const IfStmt &>(stmt);
    return asBool(eval(ifStmt.condition())) ? execBody(ifStmt.thenBody())
                                            : execBody(ifStmt.elseBody());
  }
  case NodeKind::WhileStmt: {
    const auto &loop = static_cast<const WhileStmt &>(stmt);
    while (asBool(eval(loop.condition()))) {
      Flow flow = execBody(loop.body()); // A fresh scope each iteration.
      if (flow == Flow::Break)
        break;
      if (flow == Flow::Return)
        return flow;
    }
    return Flow::Normal;
  }
  case NodeKind::BreakStmt:
    return Flow::Break;
  default:
    return Flow::Normal; // Function definitions run only when called.
  }
}

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------
Value Interpreter::eval(const Expr &expr) {
  switch (expr.kind()) {
  case NodeKind::NameExpr: {
    const auto &name = static_cast<const NameExpr &>(expr);
    Value *slot = lookup(symbolOf(name));
    if (!slot)
      fail("internal error: '" + name.name() + "' has no storage");
    if (std::holds_alternative<std::monostate>(*slot))
      fail("'" + name.name() + "' has no value (it was never given one)");
    if (mode_ == Mode::Destructive && destructive_.isConsumed(name)) {
      // Consumed here: the variable lets go of its matrix.
      Value value = std::move(*slot);
      *slot = std::monostate{};
      return value;
    }
    return *slot;
  }
  case NodeKind::IntegerLiteral:
    return static_cast<const IntegerLiteral &>(expr).value();
  case NodeKind::FloatLiteral: {
    const auto &literal = static_cast<const FloatLiteral &>(expr);
    return literal.isF32()
               ? static_cast<double>(static_cast<float>(literal.value()))
               : literal.value();
  }
  case NodeKind::StringLiteral:
    return static_cast<const StringLiteral &>(expr).value();
  case NodeKind::BooleanLiteral:
    return static_cast<const BooleanLiteral &>(expr).value();
  case NodeKind::UnaryExpr:
    return evalUnary(static_cast<const UnaryExpr &>(expr));
  case NodeKind::BinaryExpr:
    return evalBinary(static_cast<const BinaryExpr &>(expr));
  case NodeKind::CallExpr:
    return evalCall(static_cast<const CallExpr &>(expr));
  case NodeKind::CastExpr:
    return evalCast(static_cast<const CastExpr &>(expr));
  case NodeKind::SizeOfExpr:
    return sizeOfType(static_cast<const SizeOfExpr &>(expr)
                          .target()
                          .resolvedType())
        .value_or(0); // Sema rejected every type without a size.
  default:
    return std::string(); // Interpolated strings have no syntax yet.
  }
}

Value Interpreter::evalBinary(const BinaryExpr &binary) {
  // Long left-nested chains (a @ b @ c ...) are walked with a loop, as in
  // semantic analysis, so their length can't overflow the stack.
  std::vector<const BinaryExpr *> spine{&binary};
  while (spine.back()->lhs().kind() == NodeKind::BinaryExpr)
    spine.push_back(static_cast<const BinaryExpr *>(&spine.back()->lhs()));
  Value acc = eval(spine.back()->lhs());
  for (auto it = spine.rbegin(); it != spine.rend(); ++it) {
    const BinaryExpr &node = **it;
    if (node.op() == BinaryOp::And) {
      acc = asBool(acc) && asBool(eval(node.rhs()));
    } else if (node.op() == BinaryOp::Or) {
      acc = asBool(acc) || asBool(eval(node.rhs()));
    } else {
      Value rhs = eval(node.rhs());
      acc = convertTo(applyBinary(node.op(), acc, rhs,
                                  node.lhs().resolvedType(),
                                  node.rhs().resolvedType()),
                      node.resolvedType());
    }
  }
  return acc;
}

Value Interpreter::applyBinary(BinaryOp op, const Value &lhs,
                               const Value &rhs, const Type *lhsType,
                               const Type *rhsType) {
  // Matrices.
  if (isMatrix(lhs) || isMatrix(rhs)) {
    if (op == BinaryOp::MatMul)
      return runtime_.matmul(*asMatrix(lhs), *asMatrix(rhs));
    if ((op == BinaryOp::Add || op == BinaryOp::Sub) && isMatrix(lhs) &&
        isMatrix(rhs))
      return runtime_.add(*asMatrix(lhs), *asMatrix(rhs),
                          op == BinaryOp::Sub);
    if (op == BinaryOp::Mul && isMatrix(lhs))
      return runtime_.scale(*asMatrix(lhs), asDouble(rhs, rhsType));
    if (op == BinaryOp::Mul && isMatrix(rhs))
      return runtime_.scale(*asMatrix(rhs), asDouble(lhs, lhsType));
    if (op == BinaryOp::Div && isMatrix(lhs)) {
      double d = asDouble(rhs, rhsType);
      if (d == 0.0)
        fail("matrix divided by zero");
      return runtime_.scale(*asMatrix(lhs), 1.0 / d);
    }
    fail("this operation isn't supported on matrices at run time");
  }
  // Strings.
  if (std::holds_alternative<std::string>(lhs)) {
    const auto &l = std::get<std::string>(lhs);
    const auto &r = std::get<std::string>(rhs);
    switch (op) {
    case BinaryOp::Add: return l + r;
    case BinaryOp::Eq: return l == r;
    case BinaryOp::Ne: return l != r;
    case BinaryOp::Lt: return l < r;
    case BinaryOp::Le: return l <= r;
    case BinaryOp::Gt: return l > r;
    case BinaryOp::Ge: return l >= r;
    default: fail("this operation isn't supported on strings");
    }
  }
  // Pointers are equal when they point to the same variable (or both nowhere).
  if (const auto *l = std::get_if<Pointer>(&lhs)) {
    const auto &r = std::get<Pointer>(rhs);
    bool same = l->hasTarget == r.hasTarget && !l->cell.owner_before(r.cell) &&
                !r.cell.owner_before(l->cell);
    if (op == BinaryOp::Eq)
      return same;
    if (op == BinaryOp::Ne)
      return !same;
    fail("this operation isn't supported on pointers");
  }
  // Bools compare by value.
  if (std::holds_alternative<bool>(lhs) && std::holds_alternative<bool>(rhs)) {
    if (op == BinaryOp::Eq)
      return std::get<bool>(lhs) == std::get<bool>(rhs);
    if (op == BinaryOp::Ne)
      return std::get<bool>(lhs) != std::get<bool>(rhs);
  }
  // Numbers: integer math when both are integers, otherwise float math.
  if (isInt(lhs) && isInt(rhs)) {
    std::int64_t a = std::get<std::int64_t>(lhs);
    std::int64_t b = std::get<std::int64_t>(rhs);
    // u64 against any other int is u64 math (sema's commonNumericType).
    if (isU64(lhsType) || isU64(rhsType)) {
      auto ua = static_cast<std::uint64_t>(a);
      auto ub = static_cast<std::uint64_t>(b);
      switch (op) {
      case BinaryOp::Div:
        if (ub == 0)
          fail("division by zero");
        return static_cast<double>(ua) / static_cast<double>(ub);
      case BinaryOp::FloorDiv:
      case BinaryOp::Mod:
      case BinaryOp::Pow:
        return unsignedOp(op, a, b);
      case BinaryOp::Shr:
        if (b < 0 || b > 63)
          fail("shift amount " + std::to_string(b) + " is out of range");
        return static_cast<std::int64_t>(ua >> b);
      case BinaryOp::Lt: return ua < ub;
      case BinaryOp::Le: return ua <= ub;
      case BinaryOp::Gt: return ua > ub;
      case BinaryOp::Ge: return ua >= ub;
      default: break; // The rest are the same bits as int64.
      }
    }
    switch (op) {
    case BinaryOp::Add: return wrapAdd(a, b);
    case BinaryOp::Sub: return wrapSub(a, b);
    case BinaryOp::Mul: return wrapMul(a, b);
    case BinaryOp::Div:
      if (b == 0)
        fail("division by zero");
      return static_cast<double>(a) / static_cast<double>(b);
    case BinaryOp::FloorDiv: return floorDiv(a, b);
    case BinaryOp::Mod: return floorMod(a, b);
    case BinaryOp::Pow: return intPow(a, b);
    case BinaryOp::BitAnd: return a & b;
    case BinaryOp::BitOr: return a | b;
    case BinaryOp::BitXor: return a ^ b;
    case BinaryOp::Shl:
    case BinaryOp::Shr:
      if (b < 0 || b > 63)
        fail("shift amount " + std::to_string(b) + " is out of range");
      return op == BinaryOp::Shl
                 ? static_cast<std::int64_t>(static_cast<std::uint64_t>(a) << b)
                 : a >> b;
    case BinaryOp::Eq: return a == b;
    case BinaryOp::Ne: return a != b;
    case BinaryOp::Lt: return a < b;
    case BinaryOp::Le: return a <= b;
    case BinaryOp::Gt: return a > b;
    case BinaryOp::Ge: return a >= b;
    default: break;
    }
  } else {
    double a = asDouble(lhs, lhsType);
    double b = asDouble(rhs, rhsType);
    switch (op) {
    case BinaryOp::Add: return a + b;
    case BinaryOp::Sub: return a - b;
    case BinaryOp::Mul: return a * b;
    case BinaryOp::Div: return a / b;
    case BinaryOp::FloorDiv: return std::floor(a / b);
    case BinaryOp::Mod: return a - std::floor(a / b) * b;
    case BinaryOp::Pow: return std::pow(a, b);
// MemoLang's `==` and `!=` on floats are exact comparisons, as in C, so
// -Wfloat-equal (usually right about C++ code) is wrong here.
#if defined(__GNUC__) // GCC and Clang
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
#endif
    case BinaryOp::Eq: return a == b;
    case BinaryOp::Ne: return a != b;
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    case BinaryOp::Lt: return a < b;
    case BinaryOp::Le: return a <= b;
    case BinaryOp::Gt: return a > b;
    case BinaryOp::Ge: return a >= b;
    default: break;
    }
  }
  fail("this operation isn't supported at run time");
}

Value Interpreter::evalUnary(const UnaryExpr &unary) {
  UnaryOp op = unary.op();
  if (op == UnaryOp::PreInc || op == UnaryOp::PostInc ||
      op == UnaryOp::PreDec || op == UnaryOp::PostDec) {
    // `x++` or `(*p)++`.
    const Type *type = nullptr;
    std::shared_ptr<Cell> cell = cellFor(unary.operand(), type);
    Value old = cell->value;
    bool up = op == UnaryOp::PreInc || op == UnaryOp::PostInc;
    Value updated = isInt(old) ? Value{up ? wrapAdd(std::get<std::int64_t>(old), 1)
                                          : wrapSub(std::get<std::int64_t>(old), 1)}
                               : Value{asDouble(old) + (up ? 1.0 : -1.0)};
    cell->value = convertTo(updated, type);
    return (op == UnaryOp::PreInc || op == UnaryOp::PreDec) ? cell->value : old;
  }
  if (op == UnaryOp::AddressOf) {
    // `&x` or `&*p`: the cell itself, not its value.
    const Type *type = nullptr;
    return Pointer{cellFor(unary.operand(), type), /*hasTarget=*/true};
  }
  Value operand = eval(unary.operand());
  switch (op) {
  case UnaryOp::Deref: {
    // A read through the pointer never moves the value out, even a matrix.
    Value value = follow(operand)->value;
    if (std::holds_alternative<std::monostate>(value))
      fail("the variable this pointer points to has no value");
    return value;
  }
  case UnaryOp::Neg:
    if (isMatrix(operand))
      return runtime_.scale(*asMatrix(operand), -1.0);
    if (isInt(operand))
      return convertTo(wrapSub(0, std::get<std::int64_t>(operand)),
                       unary.resolvedType());
    return -asDouble(operand);
  case UnaryOp::Inv:
    return convertTo(~asInt(operand), unary.resolvedType());
  case UnaryOp::Not:
    return !asBool(operand);
  default:
    break;
  }
  fail("this operation isn't supported at run time");
}

Value Interpreter::evalCall(const CallExpr &call) {
  const auto &callee = static_cast<const NameExpr &>(call.callee());
  const sema::Symbol &symbol = sema_.symbol(SymbolId{symbolOf(callee)});

  if (symbol.kind == sema::Symbol::Kind::Builtin) { // copy(x)
    Value value = eval(*call.args()[0]);
    // Without destructive reads a program never needs to copy, so the
    // baseline just shares the matrix: copy() costs nothing there.
    if (mode_ == Mode::Baseline || !isMatrix(value))
      return value;
    return runtime_.copy(*asMatrix(value));
  }

  const FunctionDef &function = *symbol.function;
  auto params = function.params();
  auto names = call.paramNames();
  const auto *type = dynCast<FunctionType>(function.resolvedType());
  std::vector<Value> args(params.size());
  // Evaluate in source order; place each by position or by name.
  for (std::size_t i = 0; i < call.args().size(); ++i) {
    std::size_t index = i;
    if (!names.empty() && !names[i].empty())
      for (std::size_t p = 0; p < params.size(); ++p)
        if (params[p].name == names[i])
          index = p;
    Value value = eval(*call.args()[i]);
    args[index] = convertTo(std::move(value),
                            type ? type->params()[index] : nullptr,
                            call.args()[i]->resolvedType());
  }
  return callFunction(function, std::move(args));
}

Value Interpreter::evalCast(const CastExpr &cast) {
  return convertTo(eval(cast.value()), cast.target().resolvedType(),
                   cast.value().resolvedType());
}

RunReport runProgram(const Module &module, const sema::SemanticAnalyzer &sema,
                     const Destructive &destructive, Interpreter::Mode mode) {
  RunReport report;
  runtime::Runtime runtime; // Declared first, so it outlives the interpreter.
  {
    Interpreter interpreter(module, sema, destructive, runtime, mode);
    auto start = std::chrono::steady_clock::now();
    report.result = interpreter.run();
    report.totalMs = std::chrono::duration<double, std::milli>(
                         std::chrono::steady_clock::now() - start)
                         .count();
  }
  report.stats = runtime.stats();
  return report;
}

} // namespace MemoLang
