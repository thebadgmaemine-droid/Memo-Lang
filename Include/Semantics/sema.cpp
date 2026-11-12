// ---------------------------------------------------------------------------
// sema.cpp: stage 3 of the compiler pipeline
//
//   source -> Lexer -> Parser -> AST -> [Semantic analysis] -> destructive
//   analysis -> (codegen: not written yet)
//
// Resolves every name to the symbol it refers to and works out the type of
// every expression, reporting what doesn't fit: undeclared names, wrong
// argument types, assigning to a constant, `break` outside a loop, `@` on
// things that aren't matrices, and so on.
//
// How it works:
//   - Pass 1 records every function's signature, so a function can be called
//     before its definition.
//   - Pass 2 walks the file in order. Scopes come from Semantics/Scope.h: one
//     module-level scope, plus a stack for the current function's blocks.
//   - A `def` without `-> type` gets its return type from its `return`
//     statements. If a call needs that type before the function has been
//     analyzed, the function is analyzed then and there.
//   - An expression whose type couldn't be worked out gets the Error type, and
//     nothing reports errors about Error operands, so one mistake gives one
//     error.
//
// Type rules in short: integers convert to any integer or float type;
// `/` on integers gives a float (`//` keeps integers); `a @ b` needs two
// matrices with the same element type; a matrix may be multiplied or divided
// by a number; conditions must be `bool`.
//
// Built-in: copy(x) returns a copy of x. Destructive analysis
// (Analysis/destructive.cpp) treats it as the way to keep a matrix that an
// `@` or a call would otherwise consume.
//
// History: added 2026-11-12 (sema.h had only declarations).
// ---------------------------------------------------------------------------
#include "Semantics/sema.h"

#include <algorithm>
#include <cassert>
#include <sstream>
#include <utility>

#include "Diag/diagengine.h"
#include "Types/Typecontext.h"

namespace MemoLang::sema {
namespace {

std::string typeName(const Type *type) {
  return "'" + Detail::displayOrUnknown(type) + "'";
}

std::string_view binaryOpSpelling(BinaryOp op) {
  switch (op) {
  case BinaryOp::Add: return "+";
  case BinaryOp::Sub: return "-";
  case BinaryOp::Mul: return "*";
  case BinaryOp::Div: return "/";
  case BinaryOp::FloorDiv: return "//";
  case BinaryOp::Mod: return "%";
  case BinaryOp::Pow: return "**";
  case BinaryOp::MatMul: return "@";
  case BinaryOp::Eq: return "==";
  case BinaryOp::Ne: return "!=";
  case BinaryOp::Lt: return "<";
  case BinaryOp::Le: return "<=";
  case BinaryOp::Gt: return ">";
  case BinaryOp::Ge: return ">=";
  case BinaryOp::BitAnd: return "&";
  case BinaryOp::BitOr: return "|";
  case BinaryOp::BitXor: return "^";
  case BinaryOp::Shl: return "<<";
  case BinaryOp::Shr: return ">>";
  case BinaryOp::And: return "and";
  case BinaryOp::Or: return "or";
  case BinaryOp::Is: return "is";
  case BinaryOp::IsNot: return "is not";
  case BinaryOp::In: return "in";
  case BinaryOp::NotIn: return "not in";
  }
  return "?";
}

std::string_view unaryOpSpelling(UnaryOp op) {
  switch (op) {
  case UnaryOp::PreInc:
  case UnaryOp::PostInc: return "++";
  case UnaryOp::PreDec:
  case UnaryOp::PostDec: return "--";
  case UnaryOp::Neg: return "-";
  case UnaryOp::Inv: return "~";
  case UnaryOp::Not: return "not";
  }
  return "?";
}

BinaryOp compoundToBinary(AssignOp op) {
  switch (op) {
  case AssignOp::Add: return BinaryOp::Add;
  case AssignOp::Sub: return BinaryOp::Sub;
  case AssignOp::Mul: return BinaryOp::Mul;
  case AssignOp::Div: return BinaryOp::Div;
  case AssignOp::Mod: return BinaryOp::Mod;
  case AssignOp::Assign: break;
  }
  return BinaryOp::Add;
}

// Two optional matrix dimensions agree unless both are known and differ.
bool dimensionsAgree(std::optional<std::int64_t> a,
                     std::optional<std::int64_t> b) {
  return !a || !b || *a == *b;
}
std::optional<std::int64_t> mergeDimension(std::optional<std::int64_t> a,
                                           std::optional<std::int64_t> b) {
  return a ? a : b;
}

} // namespace

// ---------------------------------------------------------------------------
// Per-function state
// ---------------------------------------------------------------------------
struct SemanticAnalyzer::FunctionInfo {
  enum class State { NotStarted, InProgress, Done };
  SymbolId symbol;
  std::vector<SymbolId> params;
  std::vector<const Type *> paramTypes;
  const Type *returnType = nullptr; // Auto until inferred.
  State state = State::NotStarted;
};

// The function whose body is being analyzed (function == nullptr for code
// outside any function).
struct SemanticAnalyzer::FunctionContext {
  const FunctionDef *function = nullptr;
  FunctionInfo *info = nullptr;
  const Type *declaredReturn = nullptr; // Auto when inferring.
  const Type *inferredReturn = nullptr; // From the first `return`.
  const ReturnStmt *firstReturn = nullptr;
  int loopDepth = 0;
};

// Opens a block scope for its lifetime.
class SemanticAnalyzer::ScopeGuard {
public:
  explicit ScopeGuard(SemanticAnalyzer &analyzer) : analyzer_(analyzer) {
    Scope *parent = analyzer.localScopes_.empty()
                        ? &analyzer.global_
                        : analyzer.localScopes_.back().get();
    analyzer.localScopes_.push_back(std::make_unique<Scope>(parent));
  }
  ~ScopeGuard() { analyzer_.localScopes_.pop_back(); }
  ScopeGuard(const ScopeGuard &) = delete;
  ScopeGuard &operator=(const ScopeGuard &) = delete;

private:
  SemanticAnalyzer &analyzer_;
};

SemanticAnalyzer::SemanticAnalyzer(TypeContext &types, DiagnosticEngine &diags)
    : types_(types), diagnostics_(diags),
      topLevel_(std::make_unique<FunctionContext>()), current_(topLevel_.get()) {
  // Built-in functions.
  Symbol copy;
  copy.name = "copy";
  copy.kind = Symbol::Kind::Builtin;
  copy.isFunction = true;
  SymbolId copyId = addSymbol(copy);
  global_.declare("copy", copyId);
}

SemanticAnalyzer::~SemanticAnalyzer() = default;

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------
void SemanticAnalyzer::error(const Node &where, std::string message) {
  diagnostics_.error(where.range(), std::move(message));
}

void SemanticAnalyzer::note(SourceRange where, std::string message) {
  diagnostics_.note(where, std::move(message));
}

// ---------------------------------------------------------------------------
// Symbols and scopes
// ---------------------------------------------------------------------------
SymbolId SemanticAnalyzer::addSymbol(Symbol symbol) {
  symbols_.push_back(std::move(symbol));
  return SymbolId{static_cast<std::uint32_t>(symbols_.size() - 1)};
}

bool SemanticAnalyzer::declare(const std::string &name, SymbolId id,
                               const Node &where) {
  Scope &scope = localScopes_.empty() ? global_ : *localScopes_.back();
  if (scope.declare(name, id))
    return true;
  const Symbol &previous = symbols_[scope.lookupLocal(name)->value];
  if (previous.kind == Symbol::Kind::Builtin) {
    error(where, quoteForMessage(name) +
                     " is a built-in function and can't be redefined");
    return false;
  }
  error(where, "redefinition of " + quoteForMessage(name));
  note(previous.location,
       "the previous declaration of " + quoteForMessage(name) + " is here");
  return false;
}

std::optional<SymbolId> SemanticAnalyzer::lookup(std::string_view name) const {
  const Scope &scope = localScopes_.empty() ? global_ : *localScopes_.back();
  return scope.lookup(name);
}

const Symbol &SemanticAnalyzer::symbol(SymbolId id) const {
  assert(id.value < symbols_.size() && "unknown SymbolId");
  return symbols_[id.value];
}

std::optional<SymbolId>
SemanticAnalyzer::symbolFor(const NameExpr &name) const {
  auto it = resolutions_.find(&name);
  if (it == resolutions_.end())
    return std::nullopt;
  return it->second;
}

std::optional<SymbolId>
SemanticAnalyzer::declaredSymbol(const DeclareVar &decl) const {
  auto it = declarations_.find(&decl);
  if (it == declarations_.end())
    return std::nullopt;
  return it->second;
}

const std::vector<SymbolId> &
SemanticAnalyzer::parameterSymbols(const FunctionDef &function) const {
  static const std::vector<SymbolId> none;
  auto it = functions_.find(&function);
  return it == functions_.end() ? none : it->second->params;
}

std::string SemanticAnalyzer::dumpSymbolTable() const {
  std::ostringstream out;
  for (const Symbol &s : symbols_) {
    const char *kind = "variable";
    switch (s.kind) {
    case Symbol::Kind::Variable: kind = s.isLocal ? "local" : "global"; break;
    case Symbol::Kind::Parameter: kind = "parameter"; break;
    case Symbol::Kind::Function: kind = "function"; break;
    case Symbol::Kind::Builtin: kind = "built-in"; break;
    }
    out << s.name << " : "
        << (s.kind == Symbol::Kind::Builtin ? "built-in"
                                            : Detail::displayOrUnknown(s.type))
        << "  (" << kind << ", depth " << s.scopeDepth
        << (s.isConst ? ", const" : "") << ")\n";
  }
  return out.str();
}

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------
const Type *SemanticAnalyzer::resolveType(const TypeExpr &node) {
  const std::string &name = node.name();
  const Type *type = types_.errorType();
  if (name == "matrix") {
    if (node.args().size() != 1) {
      error(node, "'matrix' needs one element type, e.g. matrix<float>");
    } else {
      const TypeExpr &elementNode = *node.args()[0];
      const Type *element = resolveType(elementNode);
      if (!element->isError() && !element->isNumeric())
        error(elementNode,
              "matrix elements must be numbers, not " + typeName(element));
      else if (!element->isError())
        type = types_.matrixType(element);
    }
  } else if (const Type *primitive = types_.primitive(name)) {
    if (!node.args().empty())
      error(node,
            "type " + quoteForMessage(name) + " doesn't take type arguments");
    type = primitive;
  } else {
    error(node, "unknown type " + quoteForMessage(name));
  }
  node.setResolvedType(type);
  return type;
}

bool SemanticAnalyzer::checkAssignment(const Type &target,
                                       const Type &value) const {
  if (&target == &value || target.isError() || value.isError())
    return true;
  if (target.kind() == Type::Kind::Int && value.kind() == Type::Kind::Int)
    return true;
  if (target.kind() == Type::Kind::Float && value.isNumeric())
    return true;
  const auto *targetMatrix = dynCast<MatrixType>(&target);
  const auto *valueMatrix = dynCast<MatrixType>(&value);
  if (targetMatrix && valueMatrix)
    return targetMatrix->element() == valueMatrix->element() &&
           dimensionsAgree(targetMatrix->rows(), valueMatrix->rows()) &&
           dimensionsAgree(targetMatrix->cols(), valueMatrix->cols());
  return false;
}

const Type *SemanticAnalyzer::commonNumericType(const Type &lhs,
                                                const Type &rhs) {
  const auto *lf = dynCast<FloatType>(&lhs);
  const auto *rf = dynCast<FloatType>(&rhs);
  if (lf || rf)
    return types_.floatType(std::max(lf ? lf->bits() : 0u, rf ? rf->bits() : 0u));
  const auto &li = static_cast<const IntType &>(lhs);
  const auto &ri = static_cast<const IntType &>(rhs);
  if (li.bits() != ri.bits())
    return li.bits() > ri.bits() ? &li : &ri;
  return types_.intType(li.bits(), li.isSigned() || ri.isSigned());
}

const Type *SemanticAnalyzer::checkArithmetic(BinaryOp op, const Type &lhs,
                                              const Type &rhs) {
  if (lhs.isError() || rhs.isError())
    return types_.errorType();
  if (lhs.isNumeric() && rhs.isNumeric()) {
    const Type *common = commonNumericType(lhs, rhs);
    // `/` is true division; `//` keeps integers.
    if (op == BinaryOp::Div && common->kind() == Type::Kind::Int)
      return types_.floatType(64);
    return common;
  }
  if (op == BinaryOp::Add && lhs.kind() == Type::Kind::Str &&
      rhs.kind() == Type::Kind::Str)
    return types_.strType();

  const auto *lm = dynCast<MatrixType>(&lhs);
  const auto *rm = dynCast<MatrixType>(&rhs);
  if (lm && rm && (op == BinaryOp::Add || op == BinaryOp::Sub)) {
    if (lm->element() != rm->element() ||
        !dimensionsAgree(lm->rows(), rm->rows()) ||
        !dimensionsAgree(lm->cols(), rm->cols()))
      return nullptr;
    return types_.matrixType(lm->element(), mergeDimension(lm->rows(), rm->rows()),
                             mergeDimension(lm->cols(), rm->cols()));
  }
  // Scaling: matrix * number, number * matrix, matrix / number.
  if (lm && rhs.isNumeric() && (op == BinaryOp::Mul || op == BinaryOp::Div))
    return lm;
  if (rm && lhs.isNumeric() && op == BinaryOp::Mul)
    return rm;
  return nullptr;
}

bool SemanticAnalyzer::checkComparison(BinaryOp op, const Type &lhs,
                                       const Type &rhs) const {
  if (lhs.isError() || rhs.isError())
    return true;
  bool bothNumeric = lhs.isNumeric() && rhs.isNumeric();
  if (op == BinaryOp::Eq || op == BinaryOp::Ne)
    return bothNumeric || &lhs == &rhs;
  return bothNumeric || (lhs.kind() == Type::Kind::Str &&
                         rhs.kind() == Type::Kind::Str);
}

// ---------------------------------------------------------------------------
// Module and functions
// ---------------------------------------------------------------------------
bool SemanticAnalyzer::analyzeModule(const Module &module) {
  errorCountAtStart_ = diagnostics_.errorCount();
  // Pass 1: every function's signature, so calls may come before definitions.
  for (const auto &stmt : module.body)
    if (stmt->kind() == NodeKind::FunctionDef)
      declareFunctionSignature(static_cast<const FunctionDef &>(*stmt));
  // Pass 2: everything, in order.
  for (const auto &stmt : module.body)
    analyzeStatement(*stmt);
  return diagnostics_.errorCount() == errorCountAtStart_;
}

bool SemanticAnalyzer::analyzeFunction(const FunctionDef &function) {
  std::size_t before = diagnostics_.errorCount();
  declareFunctionSignature(function);
  analyzeFunctionBody(function);
  return diagnostics_.errorCount() == before;
}

void SemanticAnalyzer::declareFunctionSignature(const FunctionDef &function) {
  if (functions_.count(&function) != 0)
    return;
  auto info = std::make_unique<FunctionInfo>();
  for (const auto &param : function.params())
    info->paramTypes.push_back(resolveType(*param.type));
  info->returnType = resolveType(function.returnType());

  Symbol symbol;
  symbol.name = function.name();
  symbol.kind = Symbol::Kind::Function;
  symbol.isFunction = true;
  symbol.location = function.range();
  symbol.function = &function;
  symbol.type = types_.functionType(info->paramTypes, info->returnType);
  function.setResolvedType(symbol.type);
  info->symbol = addSymbol(std::move(symbol));
  declare(function.name(), info->symbol, function);
  functions_.emplace(&function, std::move(info));
}

void SemanticAnalyzer::analyzeFunctionBody(const FunctionDef &function) {
  FunctionInfo &info = *functions_.at(&function);
  if (info.state != FunctionInfo::State::NotStarted)
    return;
  info.state = FunctionInfo::State::InProgress;

  // The body sees only module-level names. This may run in the middle of
  // another function (when a call needs this one's inferred return type), so
  // set the caller's scopes and context aside.
  auto callerScopes = std::move(localScopes_);
  localScopes_.clear();
  FunctionContext context;
  context.function = &function;
  context.info = &info;
  context.declaredReturn = info.returnType;
  FunctionContext *caller = current_;
  current_ = &context;
  {
    ScopeGuard scope(*this);
    auto params = function.params();
    for (std::size_t i = 0; i < params.size(); ++i) {
      Symbol param;
      param.name = params[i].name;
      param.type = info.paramTypes[i];
      param.kind = Symbol::Kind::Parameter;
      param.isParameter = true;
      param.isLocal = true;
      param.scopeDepth = 1;
      param.location = params[i].type->range();
      SymbolId id = addSymbol(std::move(param));
      declare(params[i].name, id, *params[i].type);
      info.params.push_back(id);
    }
    if (!function.isExtern())
      analyzeBody(function.body());
  }
  current_ = caller;
  localScopes_ = std::move(callerScopes);

  if (info.returnType->kind() == Type::Kind::Auto) {
    info.returnType = context.inferredReturn ? context.inferredReturn
                                             : types_.voidType();
    Symbol &symbol = symbols_[info.symbol.value];
    symbol.type = types_.functionType(info.paramTypes, info.returnType);
    function.setResolvedType(symbol.type);
  }
  info.state = FunctionInfo::State::Done;
}

const Type *SemanticAnalyzer::functionReturnType(const FunctionDef &function,
                                                 const Node &useSite) {
  FunctionInfo &info = *functions_.at(&function);
  if (info.returnType->kind() != Type::Kind::Auto)
    return info.returnType;
  if (info.state == FunctionInfo::State::NotStarted)
    analyzeFunctionBody(function);
  if (info.returnType->kind() != Type::Kind::Auto)
    return info.returnType;
  error(useSite, "can't infer the return type of " +
                     quoteForMessage(function.name()) +
                     " because it is used inside itself; declare it with "
                     "'-> type'");
  return types_.errorType();
}

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------
void SemanticAnalyzer::analyzeBody(
    std::span<const std::unique_ptr<Statement>> body) {
  for (const auto &stmt : body)
    analyzeStatement(*stmt);
}

bool SemanticAnalyzer::analyzeStatement(const Statement &stmt) {
  std::size_t before = diagnostics_.errorCount();
  switch (stmt.kind()) {
  case NodeKind::ExprStmt:
    inferExpressionType(static_cast<const ExprStmt &>(stmt).expr());
    break;
  case NodeKind::DeclareVar:
    analyzeDeclareVar(static_cast<const DeclareVar &>(stmt));
    break;
  case NodeKind::FunctionDef: {
    const auto &function = static_cast<const FunctionDef &>(stmt);
    if (current_->function != nullptr || !localScopes_.empty()) {
      error(function, "nested functions are not supported yet; define " +
                          quoteForMessage(function.name()) +
                          " at the top level");
      break;
    }
    declareFunctionSignature(function);
    analyzeFunctionBody(function);
    break;
  }
  case NodeKind::AssignStmt:
    analyzeAssign(static_cast<const AssignStmt &>(stmt));
    break;
  case NodeKind::ReturnStmt:
    analyzeReturn(static_cast<const ReturnStmt &>(stmt));
    break;
  case NodeKind::IfStmt: {
    const auto &ifStmt = static_cast<const IfStmt &>(stmt);
    checkCondition(ifStmt.condition(), "'if'");
    {
      ScopeGuard scope(*this);
      analyzeBody(ifStmt.thenBody());
    }
    {
      ScopeGuard scope(*this);
      analyzeBody(ifStmt.elseBody());
    }
    break;
  }
  case NodeKind::WhileStmt: {
    const auto &whileStmt = static_cast<const WhileStmt &>(stmt);
    checkCondition(whileStmt.condition(), "'while'");
    ++current_->loopDepth;
    {
      ScopeGuard scope(*this);
      analyzeBody(whileStmt.body());
    }
    --current_->loopDepth;
    break;
  }
  case NodeKind::BreakStmt:
    if (current_->loopDepth == 0)
      error(stmt, "'break' outside a loop");
    break;
  default:
    break;
  }
  return diagnostics_.errorCount() == before;
}

void SemanticAnalyzer::checkCondition(const Expr &condition,
                                      std::string_view what) {
  const Type *type = inferExpressionType(condition);
  if (!type->isError() && type != types_.boolType())
    error(condition, std::string(what) + " condition must be 'bool', found " +
                         typeName(type));
}

void SemanticAnalyzer::analyzeDeclareVar(const DeclareVar &decl) {
  const Type *declared = decl.type() ? resolveType(*decl.type()) : nullptr;
  if (declared && declared->kind() == Type::Kind::Void) {
    error(*decl.type(), "a variable can't have type 'void'");
    declared = types_.errorType();
  }
  const Type *value = decl.init() ? inferExpressionType(*decl.init()) : nullptr;
  if (value && value->kind() == Type::Kind::Void) {
    error(*decl.init(), "this expression has no value (its type is 'void')");
    value = types_.errorType();
  }
  if (declared && value && !checkAssignment(*declared, *value))
    error(*decl.init(), "cannot initialize " + quoteForMessage(decl.name()) +
                            " of type " + typeName(declared) +
                            " with a value of type " + typeName(value));

  Symbol symbol;
  symbol.name = decl.name();
  symbol.type = declared ? declared : (value ? value : types_.errorType());
  symbol.kind = Symbol::Kind::Variable;
  symbol.isLocal = !localScopes_.empty();
  symbol.isConst = decl.isConst();
  symbol.scopeDepth = static_cast<std::uint32_t>(localScopes_.size());
  symbol.location = decl.range();
  decl.setResolvedType(symbol.type);
  SymbolId id = addSymbol(std::move(symbol));
  declare(decl.name(), id, decl);
  declarations_[&decl] = id;
}

void SemanticAnalyzer::analyzeAssign(const AssignStmt &assign) {
  const Type *value = inferExpressionType(assign.value());
  const Expr &target = assign.target();
  if (target.kind() != NodeKind::NameExpr) {
    error(target, "the left side of an assignment must be a variable name");
    return;
  }
  const auto &name = static_cast<const NameExpr &>(target);
  auto id = lookup(name.name());
  if (!id) {
    error(name, "use of undeclared name " + quoteForMessage(name.name()));
    name.setResolvedType(types_.errorType());
    return;
  }
  resolutions_[&name] = *id;
  const Symbol &symbol = symbols_[id->value];
  name.setResolvedType(symbol.type);
  if (symbol.isFunction) {
    error(name, "cannot assign to function " + quoteForMessage(name.name()));
    return;
  }
  if (symbol.isConst) {
    error(name, "cannot assign to constant " + quoteForMessage(name.name()));
    note(symbol.location,
         quoteForMessage(name.name()) + " is declared 'const' here");
    return;
  }
  const Type *result = value;
  if (assign.op() != AssignOp::Assign) {
    BinaryOp op = compoundToBinary(assign.op());
    result = checkArithmetic(op, *symbol.type, *value);
    if (!result) {
      error(assign, "invalid operands to '" +
                        std::string(binaryOpSpelling(op)) + "=': " +
                        typeName(symbol.type) + " and " + typeName(value));
      return;
    }
  }
  if (!checkAssignment(*symbol.type, *result))
    error(assign.value(), "cannot assign a value of type " + typeName(result) +
                              " to " + quoteForMessage(name.name()) +
                              " of type " + typeName(symbol.type));
}

void SemanticAnalyzer::analyzeReturn(const ReturnStmt &ret) {
  const Type *value =
      ret.value() ? inferExpressionType(*ret.value()) : nullptr;
  FunctionContext &context = *current_;
  if (context.function == nullptr) {
    error(ret, "'return' outside a function");
    return;
  }
  std::string function = quoteForMessage(context.function->name());
  const Type *declared = context.declaredReturn;

  if (declared->kind() == Type::Kind::Auto) {
    const Type *type = value ? value : types_.voidType();
    if (!context.inferredReturn) {
      context.inferredReturn = type;
      context.firstReturn = &ret;
      return;
    }
    if (type == context.inferredReturn || type->isError() ||
        context.inferredReturn->isError())
      return;
    // A later `return` may also give a type the first one converts from
    // (int where the first gave float), but `return;` and `return x;` don't mix.
    bool bothHaveValues = type->kind() != Type::Kind::Void &&
                          context.inferredReturn->kind() != Type::Kind::Void;
    if (bothHaveValues && checkAssignment(*context.inferredReturn, *type))
      return;
    error(ret, "inconsistent return types in " + function + ": " +
                   typeName(context.inferredReturn) + " and " + typeName(type));
    note(context.firstReturn->range(),
         "the first 'return' gives " + typeName(context.inferredReturn));
    return;
  }
  if (declared->kind() == Type::Kind::Void) {
    if (value)
      error(*ret.value(),
            function + " returns 'void', so its 'return' can't have a value");
    return;
  }
  if (!value) {
    error(ret, function + " must return a value of type " + typeName(declared));
    return;
  }
  if (!checkAssignment(*declared, *value))
    error(*ret.value(), function + " returns " + typeName(declared) +
                            ", but this value is " + typeName(value));
}

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------
bool SemanticAnalyzer::analyzeExpression(const Expr &expr) {
  return !inferExpressionType(expr)->isError();
}

const Type *SemanticAnalyzer::inferExpressionType(const Expr &expr) {
  const Type *type = types_.errorType();
  switch (expr.kind()) {
  case NodeKind::NameExpr:
    type = inferName(static_cast<const NameExpr &>(expr));
    break;
  case NodeKind::IntegerLiteral:
    type = static_cast<const IntegerLiteral &>(expr).isByte()
               ? static_cast<const Type *>(types_.byteType())
               : types_.intType();
    break;
  case NodeKind::FloatLiteral:
    type = types_.floatType(
        static_cast<const FloatLiteral &>(expr).isF32() ? 32 : 64);
    break;
  case NodeKind::StringLiteral:
    type = types_.strType();
    break;
  case NodeKind::InterpolatedString:
    for (const StringPart &part :
         static_cast<const InterpolatedString &>(expr).parts())
      if (part.value)
        inferExpressionType(*part.value);
    type = types_.strType();
    break;
  case NodeKind::BooleanLiteral:
    type = types_.boolType();
    break;
  case NodeKind::UnaryExpr:
    type = inferUnary(static_cast<const UnaryExpr &>(expr));
    break;
  case NodeKind::BinaryExpr:
    type = inferBinary(static_cast<const BinaryExpr &>(expr));
    break;
  case NodeKind::CallExpr:
    type = inferCall(static_cast<const CallExpr &>(expr));
    break;
  case NodeKind::CastExpr:
    type = inferCast(static_cast<const CastExpr &>(expr));
    break;
  default:
    break;
  }
  expr.setResolvedType(type);
  return type;
}

const Type *SemanticAnalyzer::inferName(const NameExpr &name) {
  auto id = lookup(name.name());
  if (!id) {
    error(name, "use of undeclared name " + quoteForMessage(name.name()));
    return types_.errorType();
  }
  resolutions_[&name] = *id;
  const Symbol &symbol = symbols_[id->value];
  if (symbol.kind == Symbol::Kind::Builtin) {
    error(name, quoteForMessage(name.name()) +
                    " is a built-in function; call it, e.g. copy(m)");
    return types_.errorType();
  }
  if (symbol.kind == Symbol::Kind::Function) {
    error(name, "function " + quoteForMessage(name.name()) +
                    " can't be used as a value; call it");
    return types_.errorType();
  }
  return symbol.type;
}

const Type *SemanticAnalyzer::inferUnary(const UnaryExpr &unary) {
  const Type *operand = inferExpressionType(unary.operand());
  if (operand->isError())
    return operand;
  switch (unary.op()) {
  case UnaryOp::Neg:
    if (operand->isNumeric() || operand->kind() == Type::Kind::Matrix)
      return operand;
    break;
  case UnaryOp::Inv:
    if (operand->kind() == Type::Kind::Int)
      return operand;
    break;
  case UnaryOp::Not:
    if (operand == types_.boolType())
      return operand;
    break;
  case UnaryOp::PreInc:
  case UnaryOp::PostInc:
  case UnaryOp::PreDec:
  case UnaryOp::PostDec: {
    if (!operand->isNumeric())
      break;
    std::string op(unaryOpSpelling(unary.op()));
    if (unary.operand().kind() != NodeKind::NameExpr) {
      error(unary, "'" + op + "' needs a variable");
      return types_.errorType();
    }
    const auto &name = static_cast<const NameExpr &>(unary.operand());
    if (auto id = symbolFor(name); id && symbols_[id->value].isConst) {
      error(unary, "cannot modify constant " + quoteForMessage(name.name()));
      note(symbols_[id->value].location,
           quoteForMessage(name.name()) + " is declared 'const' here");
      return types_.errorType();
    }
    return operand;
  }
  }
  error(unary, "invalid operand to '" + std::string(unaryOpSpelling(unary.op())) +
                   "': " + typeName(operand));
  return types_.errorType();
}

const Type *SemanticAnalyzer::inferBinary(const BinaryExpr &binary) {
  // `a + b + c + ...` nests to the left, as deep as the chain is long. Walk
  // down that left spine with a loop and combine on the way back up, so a
  // 10000-term chain doesn't need 10000 nested calls (it overflowed the stack).
  // Everything else nests at most Parser::kMaxNestingDepth deep.
  std::vector<const BinaryExpr *> spine{&binary};
  while (spine.back()->lhs().kind() == NodeKind::BinaryExpr)
    spine.push_back(static_cast<const BinaryExpr *>(&spine.back()->lhs()));
  const Type *lhs = inferExpressionType(spine.back()->lhs());
  for (auto it = spine.rbegin(); it != spine.rend(); ++it) {
    const Type *rhs = inferExpressionType((*it)->rhs());
    lhs = combineBinary(**it, lhs, rhs);
    (*it)->setResolvedType(lhs);
  }
  return lhs;
}

const Type *SemanticAnalyzer::combineBinary(const BinaryExpr &binary,
                                            const Type *lhs, const Type *rhs) {
  if (lhs->isError() || rhs->isError())
    return types_.errorType();
  BinaryOp op = binary.op();
  std::string spelling(binaryOpSpelling(op));

  switch (op) {
  case BinaryOp::MatMul:
    return inferMatMul(binary, lhs, rhs);
  case BinaryOp::Add:
  case BinaryOp::Sub:
  case BinaryOp::Mul:
  case BinaryOp::Div:
  case BinaryOp::FloorDiv:
  case BinaryOp::Mod:
  case BinaryOp::Pow:
    if (const Type *result = checkArithmetic(op, *lhs, *rhs))
      return result;
    if (op == BinaryOp::Mul && lhs->kind() == Type::Kind::Matrix &&
        rhs->kind() == Type::Kind::Matrix)
      error(binary, "'*' isn't defined for two matrices; use '@' for matrix "
                    "multiplication");
    else
      error(binary, "invalid operands to '" + spelling + "': " +
                        typeName(lhs) + " and " + typeName(rhs));
    return types_.errorType();
  case BinaryOp::Eq:
  case BinaryOp::Ne:
  case BinaryOp::Lt:
  case BinaryOp::Le:
  case BinaryOp::Gt:
  case BinaryOp::Ge:
    if (checkComparison(op, *lhs, *rhs))
      return types_.boolType();
    error(binary, "cannot compare " + typeName(lhs) + " and " + typeName(rhs) +
                      " with '" + spelling + "'");
    return types_.errorType();
  case BinaryOp::And:
  case BinaryOp::Or:
    if (lhs == types_.boolType() && rhs == types_.boolType())
      return types_.boolType();
    error(binary, "'" + spelling + "' needs 'bool' operands, found " +
                      typeName(lhs) + " and " + typeName(rhs));
    return types_.errorType();
  case BinaryOp::BitAnd:
  case BinaryOp::BitOr:
  case BinaryOp::BitXor:
  case BinaryOp::Shl:
  case BinaryOp::Shr:
    if (lhs->kind() == Type::Kind::Int && rhs->kind() == Type::Kind::Int)
      return commonNumericType(*lhs, *rhs);
    error(binary, "'" + spelling + "' needs integer operands, found " +
                      typeName(lhs) + " and " + typeName(rhs));
    return types_.errorType();
  case BinaryOp::Is:
  case BinaryOp::IsNot:
  case BinaryOp::In:
  case BinaryOp::NotIn:
    error(binary, "operator '" + spelling + "' is not supported yet");
    return types_.errorType();
  }
  return types_.errorType();
}

const Type *SemanticAnalyzer::inferMatMul(const BinaryExpr &binary,
                                          const Type *lhs, const Type *rhs) {
  const auto *lm = dynCast<MatrixType>(lhs);
  const auto *rm = dynCast<MatrixType>(rhs);
  if (!lm || !rm) {
    error(binary, "'@' needs two matrices, found " + typeName(lhs) + " and " +
                      typeName(rhs));
    return types_.errorType();
  }
  if (lm->element() != rm->element()) {
    error(binary, "can't multiply matrices with different element types: " +
                      typeName(lhs) + " @ " + typeName(rhs));
    return types_.errorType();
  }
  if (!dimensionsAgree(lm->cols(), rm->rows())) {
    error(binary, "matrix shapes don't match for '@': " + typeName(lhs) +
                      " has " + std::to_string(*lm->cols()) + " columns but " +
                      typeName(rhs) + " has " + std::to_string(*rm->rows()) +
                      " rows");
    return types_.errorType();
  }
  return types_.matrixType(lm->element(), lm->rows(), rm->cols());
}

const Type *SemanticAnalyzer::inferCall(const CallExpr &call) {
  // Arguments are always checked, so mistakes inside them are reported.
  std::vector<const Type *> argTypes;
  for (const auto &arg : call.args())
    argTypes.push_back(inferExpressionType(*arg));
  if (!call.typeArgs().empty())
    error(call, "type arguments are not supported yet");

  const Expr &callee = call.callee();
  if (callee.kind() != NodeKind::NameExpr) {
    error(callee, "only named functions can be called");
    return types_.errorType();
  }
  const auto &name = static_cast<const NameExpr &>(callee);
  std::string quotedName = quoteForMessage(name.name());
  auto id = lookup(name.name());
  if (!id) {
    error(name, "call to undeclared function " + quotedName);
    name.setResolvedType(types_.errorType());
    return types_.errorType();
  }
  resolutions_[&name] = *id;
  const Symbol &symbol = symbols_[id->value];

  if (symbol.kind == Symbol::Kind::Builtin) { // copy(x)
    if (argTypes.size() != 1 || !call.paramNames().empty()) {
      error(call, "copy() takes exactly one argument");
      return types_.errorType();
    }
    return argTypes[0];
  }
  if (!symbol.isFunction) {
    error(name, quotedName + " is not a function; its type is " +
                    typeName(symbol.type));
    return types_.errorType();
  }
  name.setResolvedType(symbol.type);

  const FunctionDef &function = *symbol.function;
  const FunctionInfo &info = *functions_.at(&function);
  auto params = function.params();
  auto args = call.args();
  auto names = call.paramNames();

  // Match arguments to parameters: positional first, then by name.
  std::vector<int> argForParam(params.size(), -1);
  bool tooMany = false;
  for (std::size_t i = 0; i < args.size(); ++i) {
    std::size_t index = i;
    if (!names.empty() && !names[i].empty()) {
      auto it = std::find_if(params.begin(), params.end(),
                             [&](const FunctionDef::Param &p) {
                               return p.name == names[i];
                             });
      if (it == params.end()) {
        error(*args[i], quotedName + " has no parameter named " +
                            quoteForMessage(names[i]));
        continue;
      }
      index = static_cast<std::size_t>(it - params.begin());
    } else if (index >= params.size()) {
      if (!tooMany)
        error(*args[i], "too many arguments to " + quotedName + ": it takes " +
                            std::to_string(params.size()));
      tooMany = true;
      continue;
    }
    if (argForParam[index] != -1) {
      error(*args[i], "parameter " + quoteForMessage(params[index].name) +
                          " of " + quotedName + " is given twice");
      continue;
    }
    argForParam[index] = static_cast<int>(i);
  }
  for (std::size_t p = 0; p < params.size(); ++p) {
    if (argForParam[p] == -1) {
      error(call, "missing argument for parameter " +
                      quoteForMessage(params[p].name) + " of " + quotedName);
      continue;
    }
    const Type *argType = argTypes[static_cast<std::size_t>(argForParam[p])];
    if (!checkAssignment(*info.paramTypes[p], *argType))
      error(*args[static_cast<std::size_t>(argForParam[p])],
            "argument for " + quoteForMessage(params[p].name) + " of " +
                quotedName + " must be " + typeName(info.paramTypes[p]) +
                ", found " + typeName(argType));
  }
  return functionReturnType(function, call);
}

const Type *SemanticAnalyzer::inferCast(const CastExpr &cast) {
  const Type *value = inferExpressionType(cast.value());
  const Type *target = resolveType(cast.target());
  if (value->isError() || target->isError() || value == target)
    return target;
  bool valueScalar = value->isNumeric() || value == types_.boolType();
  bool targetScalar = target->isNumeric() || target == types_.boolType();
  if (!(valueScalar && targetScalar))
    error(cast, "cannot cast " + typeName(value) + " to " + typeName(target));
  return target;
}

} // namespace MemoLang::sema
