// ---------------------------------------------------------------------------
// CodegenFunction.cpp: lowers statements and expressions (CodegenFunction.h)
// ---------------------------------------------------------------------------
#include "Codegen/CodegenFunction.h"

#include <cassert>

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/MDBuilder.h>
#include <llvm/IR/Value.h>

#include "AST/AST.h"
#include "Analysis/destructive.h"
#include "Codegen/CallUtils.h"
#include "Codegen/CodegenModule.h"
#include "Semantics/sema.h"
#include "Types/Typecontext.h"

namespace MemoLang {
namespace {

bool isComparison(BinaryOp op) {
  switch (op) {
  case BinaryOp::Eq:
  case BinaryOp::Ne:
  case BinaryOp::Lt:
  case BinaryOp::Le:
  case BinaryOp::Gt:
  case BinaryOp::Ge:
    return true;
  default:
    return false;
  }
}

bool isKind(const Type *type, Type::Kind kind) {
  return type && type->kind() == kind;
}

// Integer math runs in i64. Narrower unsigned types zero-extend into it and
// stay non-negative, so only u64 needs the unsigned instructions.
bool isU64(const Type *type) {
  const auto *intType = dynCast<IntType>(type);
  return intType && intType->bits() == 64 && !intType->isSigned();
}

// matrix<f32> -> 1, matrix<float> -> 0 (the runtime's element type flag).
std::uint32_t f32Flag(const Type *matrixType) {
  if (const auto *m = dynCast<MatrixType>(matrixType))
    if (const auto *f = dynCast<FloatType>(m->element()))
      return f->bits() == 32 ? 1 : 0;
  return 0;
}

} // namespace

CodegenFunction::CodegenFunction(CodegenModule &cgm, llvm::Function *function,
                                 const FunctionDef *definition)
    : cgm_(cgm), types_(cgm.getTypes()), function_(function),
      definition_(definition), builder_(cgm.getLLVMContext()) {
  entry_ = llvm::BasicBlock::Create(cgm.getLLVMContext(), "entry", function);
  builder_.SetInsertPoint(entry_);
  scopes_.emplace_back(cgm_); // The function's own scope.
}

CodegenFunction::~CodegenFunction() = default;

bool CodegenFunction::isMatrix(const Type *type) {
  return isKind(type, Type::Kind::Matrix);
}

bool CodegenFunction::reachable() const {
  const llvm::BasicBlock *block = builder_.GetInsertBlock();
  return block && !block->hasTerminator();
}

const Type *CodegenFunction::returnType() const {
  if (definition_)
    if (const auto *signature =
            dynCast<FunctionType>(definition_->resolvedType()))
      return signature->returnType();
  return types_.voidType();
}

// ---------------------------------------------------------------------------
// Variables
// ---------------------------------------------------------------------------
llvm::AllocaInst *CodegenFunction::createEntryAlloca(llvm::Type *type,
                                                     const std::string &name) {
  // All slots live in the entry block, so mem2reg can promote them.
  llvm::IRBuilder<> entryBuilder(entry_, entry_->begin());
  return entryBuilder.CreateAlloca(type, nullptr, name);
}

void CodegenFunction::declareLocal(std::uint32_t symbol, const Type *type,
                                   const std::string &name,
                                   llvm::Value *initial) {
  llvm::Type *lowered = cgm_.lowerType(type);
  if (!lowered || lowered->isVoidTy()) {
    cgm_.error(SourceRange{}, "code generation doesn't support variables of "
                              "type " + Detail::displayOrUnknown(type));
    return;
  }
  llvm::AllocaInst *slot = createEntryAlloca(lowered, name);
  emitLifetimeStart(builder_, slot);
  builder_.CreateStore(initial, slot);
  scopes_.back().track(slot, isMatrix(type));
  locals_[symbol] = Slot{slot, type};
}

CodegenFunction::Slot CodegenFunction::slotFor(std::uint32_t symbol,
                                               const Node &where) {
  if (auto it = locals_.find(symbol); it != locals_.end())
    return it->second;
  const Type *type = cgm_.getSema().symbol(SymbolId{symbol}).type;
  if (llvm::GlobalVariable *global = cgm_.globalFor(symbol))
    return Slot{global, type};
  cgm_.error(where.range(), "internal error: variable has no storage");
  llvm::Type *lowered = cgm_.lowerType(type);
  if (!lowered || lowered->isVoidTy())
    lowered = builder_.getInt64Ty();
  return Slot{createEntryAlloca(lowered, "invalid"), type};
}

CodegenFunction::Slot CodegenFunction::emitLValue(const Expr &target) {
  if (target.kind() == NodeKind::NameExpr) {
    const auto &name = static_cast<const NameExpr &>(target);
    if (auto id = cgm_.getSema().symbolFor(name))
      return slotFor(id->value, name);
    cgm_.error(name.range(), "internal error: '" + name.name() +
                                 "' was never resolved");
  } else if (target.kind() == NodeKind::UnaryExpr &&
             static_cast<const UnaryExpr &>(target).op() == UnaryOp::Deref) {
    // `*p`: the address is p's value. Sema allows nothing else here.
    llvm::Value *address =
        emitExpr(static_cast<const UnaryExpr &>(target).operand());
    panicIf(builder_.CreateIsNull(address),
            "dereferencing a pointer that was never given a value");
    return Slot{address, target.resolvedType()};
  } else {
    cgm_.error(target.range(), "internal error: not an assignable expression");
  }
  llvm::Type *lowered = cgm_.lowerType(target.resolvedType());
  if (!lowered || lowered->isVoidTy())
    lowered = builder_.getInt64Ty();
  return Slot{createEntryAlloca(lowered, "invalid"), target.resolvedType()};
}

// ---------------------------------------------------------------------------
// Runtime checks and calls
// ---------------------------------------------------------------------------
void CodegenFunction::panicIf(llvm::Value *condition,
                              const std::string &message) {
  if (auto *constant = llvm::dyn_cast<llvm::ConstantInt>(condition))
    if (constant->isZero())
      return;
  llvm::LLVMContext &context = cgm_.getLLVMContext();
  auto *fail = llvm::BasicBlock::Create(context, "panic", function_);
  auto *ok = llvm::BasicBlock::Create(context, "ok", function_);
  llvm::MDBuilder weights(context);
  builder_.CreateCondBr(condition, fail, ok,
                        weights.createUnlikelyBranchWeights());
  builder_.SetInsertPoint(fail);
  builder_.CreateCall(cgm_.runtime(RuntimeFn::Panic),
                      {cgm_.stringConstant(message)});
  builder_.CreateUnreachable();
  builder_.SetInsertPoint(ok);
}

void CodegenFunction::release(llvm::Value *matrix) {
  builder_.CreateCall(cgm_.runtime(RuntimeFn::Release), {matrix});
}

void CodegenFunction::retain(llvm::Value *matrix) {
  builder_.CreateCall(cgm_.runtime(RuntimeFn::Retain), {matrix});
}

void CodegenFunction::emitCleanupsDownTo(std::size_t depth) {
  for (std::size_t i = scopes_.size(); i-- > depth;)
    scopes_[i].emitCleanups(builder_);
}

// ---------------------------------------------------------------------------
// Conversions (the interpreter's convertTo)
// ---------------------------------------------------------------------------
llvm::Value *CodegenFunction::toI64(llvm::Value *value, const Type *from) {
  if (const auto *intType = dynCast<IntType>(from))
    return builder_.CreateIntCast(value, builder_.getInt64Ty(),
                                  intType->isSigned());
  return builder_.CreateZExt(value, builder_.getInt64Ty()); // bool
}

llvm::Value *CodegenFunction::toF64(llvm::Value *value, const Type *from) {
  llvm::Type *f64 = builder_.getDoubleTy();
  switch (from->kind()) {
  case Type::Kind::Int:
    return isU64(from) ? builder_.CreateUIToFP(value, f64)
                       : builder_.CreateSIToFP(toI64(value, from), f64);
  case Type::Kind::Float:
    return builder_.CreateFPExt(value, f64);
  default:
    return builder_.CreateUIToFP(value, f64); // bool: 0.0 or 1.0
  }
}

llvm::Value *CodegenFunction::convert(llvm::Value *value, const Type *from,
                                      const Type *to) {
  if (!from || !to || from == to || !value)
    return value;
  bool fromScalar = from->isNumeric() || isKind(from, Type::Kind::Bool);
  if (!fromScalar)
    return value; // Matrices (shapes may differ), strings.

  if (const auto *intType = dynCast<IntType>(to)) {
    llvm::Value *wide = nullptr;
    if (from->kind() == Type::Kind::Float) {
      llvm::Value *d = toF64(value, from);
      // In range iff -2^63 <= d < 2^63, or -1 < d < 2^64 for u64 (it
      // truncates toward zero, so -0.5 gives 0); NaN and infinities fail.
      bool toU64 = isU64(to);
      auto constant = [&](double c) {
        return llvm::ConstantFP::get(d->getType(), c);
      };
      llvm::Value *inRange =
          toU64 ? builder_.CreateAnd(
                      builder_.CreateFCmpOLT(d, constant(1.8446744073709552e19)),
                      builder_.CreateFCmpOGT(d, constant(-1.0)))
                : builder_.CreateAnd(
                      builder_.CreateFCmpOLT(d, constant(9.2233720368547758e18)),
                      builder_.CreateFCmpOGE(d, constant(-9.2233720368547758e18)));
      panicIf(builder_.CreateNot(inRange),
              "float value out of range for an integer");
      wide = toU64 ? builder_.CreateFPToUI(d, builder_.getInt64Ty())
                   : builder_.CreateFPToSI(d, builder_.getInt64Ty());
    } else {
      wide = toI64(value, from);
    }
    return builder_.CreateTrunc(wide, builder_.getIntNTy(intType->bits()));
  }
  if (const auto *floatType = dynCast<FloatType>(to)) {
    llvm::Value *d = toF64(value, from);
    return floatType->bits() == 32
               ? builder_.CreateFPTrunc(d, builder_.getFloatTy())
               : d;
  }
  if (isKind(to, Type::Kind::Bool)) {
    if (isKind(from, Type::Kind::Bool))
      return value;
    if (from->kind() == Type::Kind::Int)
      return builder_.CreateICmpNE(value,
                                   llvm::ConstantInt::get(value->getType(), 0));
    return builder_.CreateFCmpUNE(
        value, llvm::ConstantFP::get(value->getType(), 0.0));
  }
  return value;
}

llvm::Value *CodegenFunction::defaultValue(const Type *type) {
  if (!type)
    return nullptr;
  switch (type->kind()) {
  case Type::Kind::Str:
    return cgm_.stringConstant("");
  case Type::Kind::Int:
  case Type::Kind::Float:
  case Type::Kind::Bool:
  case Type::Kind::Matrix:  // A matrix without a value: null.
  case Type::Kind::Pointer: // A pointer to nothing: null.
    return llvm::Constant::getNullValue(cgm_.lowerType(type));
  default:
    return nullptr;
  }
}

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------
void CodegenFunction::emitPrologue() {
  if (!definition_)
    return;
  const auto *signature = dynCast<FunctionType>(definition_->resolvedType());
  const auto &ids = cgm_.getSema().parameterSymbols(*definition_);
  auto params = definition_->params();
  for (std::size_t i = 0; i < params.size() && i < ids.size(); ++i)
    declareLocal(ids[i].value, signature->params()[i], params[i].name,
                 function_->getArg(static_cast<unsigned>(i)));
}

void CodegenFunction::emitBody(
    std::span<const std::unique_ptr<Statement>> body) {
  for (const auto &stmt : body) {
    if (!reachable())
      return; // After `return` or `break`: the rest can't run.
    emitStatement(*stmt);
  }
}

void CodegenFunction::emitScopedBody(
    std::span<const std::unique_ptr<Statement>> body) {
  scopes_.emplace_back(cgm_);
  emitBody(body);
  if (reachable())
    scopes_.back().emitCleanups(builder_);
  scopes_.pop_back();
}

void CodegenFunction::finish() {
  if (reachable()) {
    emitCleanupsDownTo(0);
    const Type *returns = returnType();
    if (isKind(returns, Type::Kind::Void))
    //  Silently defaults to void, to be changed
      builder_.CreateRetVoid();
    else
      builder_.CreateRet(defaultValue(returns));
  }
  scopes_.clear();
}

void CodegenFunction::emitStatement(const Statement &stmt) {
  switch (stmt.kind()) {
  case NodeKind::ExprStmt: {
    const Expr &expr = static_cast<const ExprStmt &>(stmt).expr();
    llvm::Value *value = emitExpr(expr);
    if (isMatrix(expr.resolvedType()) && value)
      release(value); // An unused matrix result is freed right away.
    break;
  }
  case NodeKind::DeclareVar:
    emitDeclareVar(static_cast<const DeclareVar &>(stmt));
    break;
  case NodeKind::AssignStmt:
    emitAssign(static_cast<const AssignStmt &>(stmt));
    break;
  case NodeKind::ReturnStmt:
    emitReturn(static_cast<const ReturnStmt &>(stmt));
    break;
  case NodeKind::IfStmt:
    emitIf(static_cast<const IfStmt &>(stmt));
    break;
  case NodeKind::WhileStmt:
    emitWhile(static_cast<const WhileStmt &>(stmt));
    break;
  case NodeKind::BreakStmt:
    emitBreak(static_cast<const BreakStmt &>(stmt));
    break;
  case NodeKind::FunctionDef:
    cgm_.error(stmt.range(), "code generation doesn't support functions "
                             "defined inside other code");
    break;
  default:
    break;
  }
}

void CodegenFunction::emitDeclareVar(const DeclareVar &decl) {
  auto id = cgm_.getSema().declaredSymbol(decl);
  if (!id) {
    cgm_.error(decl.range(), "internal error: '" + decl.name() +
                                 "' was never declared");
    return;
  }
  const Type *type = decl.resolvedType();
  llvm::Value *value =
      decl.init() ? convert(emitExpr(*decl.init()),
                            decl.init()->resolvedType(), type)
                  : defaultValue(type);
  if (!value)
    return;
  if (llvm::GlobalVariable *global = cgm_.globalFor(id->value)) {
    builder_.CreateStore(value, global); // A module-level variable.
    return;
  }
  declareLocal(id->value, type, decl.name(), value);
}

void CodegenFunction::emitAssign(const AssignStmt &assign) {
  // `x = v` / `*p = v` evaluate v first; `x op= v` / `*p op= v` find and
  // read the target first, as the interpreter does.
  const Expr &valueExpr = assign.value();
  Slot slot;
  llvm::Value *value = nullptr;
  if (assign.op() == AssignOp::Assign) {
    llvm::Value *raw = emitExpr(valueExpr);
    slot = emitLValue(assign.target());
    value = convert(raw, valueExpr.resolvedType(), slot.type);
  } else {
    slot = emitLValue(assign.target());
    // `m op= v` reads m without consuming it, as the interpreter does.
    llvm::Value *current =
        builder_.CreateLoad(cgm_.lowerType(slot.type), slot.address, "current");
    if (isMatrix(slot.type))
      retain(current);
    llvm::Value *rhs = emitExpr(valueExpr);
    BinaryOp op = BinaryOp::Add;
    switch (assign.op()) {
    case AssignOp::Sub: op = BinaryOp::Sub; break;
    case AssignOp::Mul: op = BinaryOp::Mul; break;
    case AssignOp::Div: op = BinaryOp::Div; break;
    case AssignOp::Mod: op = BinaryOp::Mod; break;
    default: break;
    }
    value = emitBinaryOp(op, current, slot.type, rhs, valueExpr.resolvedType(),
                         slot.type, assign);
  }
  if (!value)
    return;
  if (isMatrix(slot.type)) {
    // The new value replaces the old one, which is released (also through
    // a pointer: the variable pointed to owned the old matrix).
    llvm::Value *old =
        builder_.CreateLoad(cgm_.lowerType(slot.type), slot.address, "old");
    builder_.CreateStore(value, slot.address);
    release(old);
  } else {
    builder_.CreateStore(value, slot.address);
  }
}
// The text of one embedded value in an interpolated string, made by the
// runtime's *_to_str functions, which format exactly as the interpreter does
// (runtime::formatInt/formatFloat/formatBool). Semantic analysis allows only
// int, float, bool and str values, and a spec only on floats.
llvm::Value *CodegenFunction::emitToString(const Expr &value,
                                           std::string_view spec) {
  const Type *type = value.resolvedType();
  llvm::Value *v = emitExpr(value);
  if (!v)
    return cgm_.stringConstant("");
  switch (type->kind()) {
  case Type::Kind::Str:
    return v;
  case Type::Kind::Int:
    return builder_.CreateCall(cgm_.runtime(RuntimeFn::IntToStr),
                               {toI64(v, type),
                                builder_.getInt32(isU64(type) ? 1 : 0)});
  case Type::Kind::Float:
    return builder_.CreateCall(
        cgm_.runtime(RuntimeFn::FloatToStr),
        {toF64(v, type), builder_.getInt32(static_cast<std::uint32_t>(
                             floatPrecision(spec).value_or(-1)))});
  case Type::Kind::Bool:
    return builder_.CreateCall(cgm_.runtime(RuntimeFn::BoolToStr),
                               {builder_.CreateZExt(v, builder_.getInt32Ty())});
  default:
    cgm_.error(value.range(), "internal error: can't interpolate a value of "
                              "type " +
                                  Detail::displayOrUnknown(type));
    return cgm_.stringConstant("");
  }
}

void CodegenFunction::emitReturn(const ReturnStmt &ret) {
  const Type *returns = returnType();
  llvm::Value *value = nullptr;
  if (ret.value()) {
    value = emitExpr(*ret.value());
    if (definition_ && !isKind(returns, Type::Kind::Void))
      value = convert(value, ret.value()->resolvedType(), returns);
    else if (isMatrix(ret.value()->resolvedType()) && value)
      release(value); // Nothing receives it.
  }
  // The returned value was evaluated first: a returned variable has already
  // handed over its matrix, so the cleanups don't free it.
  emitCleanupsDownTo(0);
  if (!definition_ || isKind(returns, Type::Kind::Void))
    builder_.CreateRetVoid();
  else
    builder_.CreateRet(value ? value : defaultValue(returns));
}

void CodegenFunction::emitIf(const IfStmt &ifStmt) {
  llvm::LLVMContext &context = cgm_.getLLVMContext();
  llvm::Value *condition = emitExpr(ifStmt.condition());
  auto *thenBlock = llvm::BasicBlock::Create(context, "if.then", function_);
  auto *elseBlock = llvm::BasicBlock::Create(context, "if.else", function_);
  auto *merge = llvm::BasicBlock::Create(context, "if.end", function_);
  builder_.CreateCondBr(condition, thenBlock, elseBlock);

  builder_.SetInsertPoint(thenBlock);
  emitScopedBody(ifStmt.thenBody());
  if (reachable())
    builder_.CreateBr(merge);

  builder_.SetInsertPoint(elseBlock);
  emitScopedBody(ifStmt.elseBody());
  if (reachable())
    builder_.CreateBr(merge);

  builder_.SetInsertPoint(merge);
  if (llvm::pred_empty(merge))
    builder_.CreateUnreachable(); // Both branches returned or broke out.
}

void CodegenFunction::emitWhile(const WhileStmt &loop) {
  llvm::LLVMContext &context = cgm_.getLLVMContext();
  auto *condBlock = llvm::BasicBlock::Create(context, "while.cond", function_);
  auto *bodyBlock = llvm::BasicBlock::Create(context, "while.body", function_);
  auto *exit = llvm::BasicBlock::Create(context, "while.end", function_);
  builder_.CreateBr(condBlock);

  builder_.SetInsertPoint(condBlock);
  builder_.CreateCondBr(emitExpr(loop.condition()), bodyBlock, exit);

  builder_.SetInsertPoint(bodyBlock);
  loops_.push_back(Loop{exit, scopes_.size()});
  emitScopedBody(loop.body()); // A fresh scope each iteration.
  loops_.pop_back();
  if (reachable())
    builder_.CreateBr(condBlock);

  builder_.SetInsertPoint(exit);
  if (llvm::pred_empty(exit))
    builder_.CreateUnreachable(); // `while true` without `break`.
}
// "a{x}b" is concatenated left to right: "" + "a" + x + "b".
llvm::Value *
CodegenFunction::emitInterpolatedString(const InterpolatedString &s) {
  llvm::Value *acc = cgm_.stringConstant("");
  for (const StringPart &part : s.parts()) {
    if (!part.literal.empty()) {
      llvm::Value *text = cgm_.stringConstant(part.literal);
      acc = builder_.CreateCall(cgm_.runtime(RuntimeFn::StrConcat), {acc, text});
    }
    if (part.value) {
      llvm::Value *piece = emitToString(*part.value, part.spec);
      acc = builder_.CreateCall(cgm_.runtime(RuntimeFn::StrConcat), {acc, piece});
    }
  }
  return acc;
}

void CodegenFunction::emitBreak(const BreakStmt &stmt) {
  if (loops_.empty()) {
    cgm_.error(stmt.range(), "'break' outside a loop");
    return;
  }
  emitCleanupsDownTo(loops_.back().scopeDepth);
  builder_.CreateBr(loops_.back().exit);
}

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------
llvm::Value *CodegenFunction::emitExpr(const Expr &expr) {
  switch (expr.kind()) {
  case NodeKind::NameExpr:
    return emitName(static_cast<const NameExpr &>(expr));
  case NodeKind::IntegerLiteral: {
    auto value = static_cast<const IntegerLiteral &>(expr).value();
    return convert(builder_.getInt64(static_cast<std::uint64_t>(value)),
                   types_.intType(), expr.resolvedType());
  }
  case NodeKind::FloatLiteral: {
    const auto &literal = static_cast<const FloatLiteral &>(expr);
    double value = literal.isF32()
                       ? static_cast<double>(static_cast<float>(literal.value()))
                       : literal.value();
    return llvm::ConstantFP::get(cgm_.lowerType(expr.resolvedType()), value);
  }
  case NodeKind::StringLiteral:
    return cgm_.stringConstant(static_cast<const StringLiteral &>(expr).value());
  case NodeKind::BooleanLiteral:
    return builder_.getInt1(static_cast<const BooleanLiteral &>(expr).value());
  case NodeKind::UnaryExpr:
    return emitUnary(static_cast<const UnaryExpr &>(expr));
  case NodeKind::BinaryExpr:
    return emitBinary(static_cast<const BinaryExpr &>(expr));
  case NodeKind::CallExpr:
    return emitCall(static_cast<const CallExpr &>(expr));
  case NodeKind::CastExpr: {
    const auto &cast = static_cast<const CastExpr &>(expr);
    return convert(emitExpr(cast.value()), cast.value().resolvedType(),
                   cast.target().resolvedType());
  }
  case NodeKind::SizeOfExpr: {
    // The same numbers as sema and the interpreter (sizeOfType), not the
    // target's DataLayout, so every way of running a program agrees.
    const auto &size = static_cast<const SizeOfExpr &>(expr);
    std::int64_t bytes = sizeOfType(size.target().resolvedType()).value_or(0);
    return convert(builder_.getInt64(static_cast<std::uint64_t>(bytes)),
                   types_.intType(), expr.resolvedType());
  }
  case NodeKind::InterpolatedString: {
    return emitInterpolatedString(static_cast<const InterpolatedString &>(expr));
  }
  default:
    cgm_.error(expr.range(), "code generation doesn't support this type! ");
    return defaultValue(types_.strType());
  }
}

llvm::Value *CodegenFunction::emitName(const NameExpr &name) {
  auto id = cgm_.getSema().symbolFor(name);
  if (!id) {
    cgm_.error(name.range(), "internal error: '" + name.name() +
                                 "' was never resolved");
    return defaultValue(name.resolvedType());
  }
  Slot slot = slotFor(id->value, name);
  llvm::Value *value =
      builder_.CreateLoad(cgm_.lowerType(slot.type), slot.address, name.name());
  if (!isMatrix(slot.type))
    return value;

  panicIf(builder_.CreateIsNull(value),
          "'" + name.name() + "' has no value (it was never given one)");
  if (cgm_.getDestructive().isConsumed(name)) {
    // Consumed here: the variable hands its reference over.
    builder_.CreateStore(llvm::Constant::getNullValue(value->getType()),
                         slot.address);
  } else {
    retain(value); // A shared read: the user gets a reference of its own.
  }
  return value;
}

llvm::Value *CodegenFunction::emitUnary(const UnaryExpr &unary) {
  UnaryOp op = unary.op();
  const Type *resultType = unary.resolvedType();

  if (op == UnaryOp::PreInc || op == UnaryOp::PostInc ||
      op == UnaryOp::PreDec || op == UnaryOp::PostDec) {
    Slot slot = emitLValue(unary.operand()); // `x++` or `(*p)++`.
    llvm::Value *old =
        builder_.CreateLoad(cgm_.lowerType(slot.type), slot.address, "old");
    bool up = op == UnaryOp::PreInc || op == UnaryOp::PostInc;
    llvm::Value *updated = nullptr;
    if (slot.type->kind() == Type::Kind::Int) {
      llvm::Value *raw = up ? builder_.CreateAdd(toI64(old, slot.type),
                                                 builder_.getInt64(1))
                            : builder_.CreateSub(toI64(old, slot.type),
                                                 builder_.getInt64(1));
      updated = convert(raw, types_.intType(), slot.type);
    } else {
      llvm::Value *one = llvm::ConstantFP::get(builder_.getDoubleTy(), 1.0);
      llvm::Value *raw = up ? builder_.CreateFAdd(toF64(old, slot.type), one)
                            : builder_.CreateFSub(toF64(old, slot.type), one);
      updated = convert(raw, types_.floatType(), slot.type);
    }
    builder_.CreateStore(updated, slot.address);
    return (op == UnaryOp::PreInc || op == UnaryOp::PreDec) ? updated : old;
  }
  // `&x`: the variable's slot (an alloca or a global), not its value. A
  // local whose address is taken stays in memory: mem2reg leaves it alone.
  if (op == UnaryOp::AddressOf)
    return emitLValue(unary.operand()).address;

  const Type *operandType = unary.operand().resolvedType();
  llvm::Value *operand = emitExpr(unary.operand());
  switch (op) {
  case UnaryOp::Deref: {
    panicIf(builder_.CreateIsNull(operand),
            "dereferencing a pointer that was never given a value");
    llvm::Value *value =
        builder_.CreateLoad(cgm_.lowerType(resultType), operand, "deref");
    if (!isMatrix(resultType))
      return value;
    // Never consumed: the variable pointed to keeps its matrix, and the
    // reader gets a reference of its own, as for a shared read of a name.
    panicIf(builder_.CreateIsNull(value),
            "the variable this pointer points to has no value");
    retain(value);
    return value;
  }
  case UnaryOp::Neg:
    if (isMatrix(operandType)) {
      llvm::Value *result = builder_.CreateCall(
          cgm_.runtime(RuntimeFn::Scale),
          {operand, llvm::ConstantFP::get(builder_.getDoubleTy(), -1.0)});
      release(operand);
      return result;
    }
    if (operandType->kind() == Type::Kind::Int)
      return convert(builder_.CreateSub(builder_.getInt64(0),
                                        toI64(operand, operandType)),
                     types_.intType(), resultType);
    return builder_.CreateFNeg(operand);
  case UnaryOp::Inv:
    return convert(builder_.CreateNot(toI64(operand, operandType)),
                   types_.intType(), resultType);
  case UnaryOp::Not:
    return builder_.CreateNot(operand);
  default:
    return operand;
  }
}

llvm::Value *CodegenFunction::emitBinary(const BinaryExpr &binary) {
  // Long left-nested chains (a @ b @ c ...) are walked with a loop, as in
  // semantic analysis and the interpreter, so their length can't overflow
  // the stack.
  std::vector<const BinaryExpr *> spine{&binary};
  while (spine.back()->lhs().kind() == NodeKind::BinaryExpr)
    spine.push_back(static_cast<const BinaryExpr *>(&spine.back()->lhs()));

  const Expr &first = spine.back()->lhs();
  llvm::Value *acc = emitExpr(first);
  const Type *accType = first.resolvedType();
  for (auto it = spine.rbegin(); it != spine.rend(); ++it) {
    const BinaryExpr &node = **it;
    if (node.op() == BinaryOp::And || node.op() == BinaryOp::Or) {
      acc = emitLogical(node, acc);
    } else {
      llvm::Value *rhs = emitExpr(node.rhs());
      acc = emitBinaryOp(node.op(), acc, accType, rhs, node.rhs().resolvedType(),
                         node.resolvedType(), node);
    }
    accType = node.resolvedType();
  }
  return acc;
}

llvm::Value *CodegenFunction::emitLogical(const BinaryExpr &binary,
                                          llvm::Value *lhs) {
  // `and` / `or` skip the right side when the left decides the result.
  llvm::LLVMContext &context = cgm_.getLLVMContext();
  bool isAnd = binary.op() == BinaryOp::And;
  llvm::BasicBlock *lhsBlock = builder_.GetInsertBlock();
  auto *rhsBlock = llvm::BasicBlock::Create(
      context, isAnd ? "and.rhs" : "or.rhs", function_);
  auto *merge = llvm::BasicBlock::Create(context, isAnd ? "and.end" : "or.end",
                                         function_);
  if (isAnd)
    builder_.CreateCondBr(lhs, rhsBlock, merge);
  else
    builder_.CreateCondBr(lhs, merge, rhsBlock);

  builder_.SetInsertPoint(rhsBlock);
  llvm::Value *rhs = emitExpr(binary.rhs());
  llvm::BasicBlock *rhsEnd = builder_.GetInsertBlock();
  builder_.CreateBr(merge);

  builder_.SetInsertPoint(merge);
  llvm::PHINode *phi = builder_.CreatePHI(builder_.getInt1Ty(), 2,
                                          isAnd ? "and" : "or");
  phi->addIncoming(builder_.getInt1(!isAnd), lhsBlock);
  phi->addIncoming(rhs, rhsEnd);
  return phi;
}

llvm::Value *CodegenFunction::emitBinaryOp(BinaryOp op, llvm::Value *lhs,
                                           const Type *lhsType,
                                           llvm::Value *rhs,
                                           const Type *rhsType,
                                           const Type *result,
                                           const Node &where) {
  if (isMatrix(lhsType) || isMatrix(rhsType))
    return emitMatrixOp(op, lhs, lhsType, rhs, rhsType, where);

  if (isKind(lhsType, Type::Kind::Str)) {
    if (op == BinaryOp::Add)
      return builder_.CreateCall(cgm_.runtime(RuntimeFn::StrConcat), {lhs, rhs});
    llvm::Value *order =
        builder_.CreateCall(cgm_.runtime(RuntimeFn::StrCompare), {lhs, rhs});
    llvm::Value *zero = builder_.getInt32(0);
    switch (op) {
    case BinaryOp::Eq: return builder_.CreateICmpEQ(order, zero);
    case BinaryOp::Ne: return builder_.CreateICmpNE(order, zero);
    case BinaryOp::Lt: return builder_.CreateICmpSLT(order, zero);
    case BinaryOp::Le: return builder_.CreateICmpSLE(order, zero);
    case BinaryOp::Gt: return builder_.CreateICmpSGT(order, zero);
    case BinaryOp::Ge: return builder_.CreateICmpSGE(order, zero);
    default: break;
    }
  } else if (isKind(lhsType, Type::Kind::Pointer) &&
             isKind(rhsType, Type::Kind::Pointer)) {
    // Same variable (or both null). Sema allows nothing but == and !=.
    if (op == BinaryOp::Eq)
      return builder_.CreateICmpEQ(lhs, rhs);
    if (op == BinaryOp::Ne)
      return builder_.CreateICmpNE(lhs, rhs);
  } else if (isKind(lhsType, Type::Kind::Bool) &&
             isKind(rhsType, Type::Kind::Bool)) {
    if (op == BinaryOp::Eq)
      return builder_.CreateICmpEQ(lhs, rhs);
    if (op == BinaryOp::Ne)
      return builder_.CreateICmpNE(lhs, rhs);
  } else if (isKind(lhsType, Type::Kind::Int) &&
             isKind(rhsType, Type::Kind::Int)) {
    // Integer math in i64, then converted to the result type.
    // u64 against any other int is u64 math (sema's commonNumericType).
    bool isUnsigned = isU64(lhsType) || isU64(rhsType);
    llvm::Value *raw = emitIntOp(op, toI64(lhs, lhsType), toI64(rhs, rhsType),
                                 where, isUnsigned);
    if (!raw)
      return defaultValue(result);
    const Type *rawType =
        isUnsigned ? types_.intType(64, /*isSigned=*/false) : types_.intType();
    if (isComparison(op))
      rawType = types_.boolType();
    else if (op == BinaryOp::Div)
      rawType = types_.floatType();
    return convert(raw, rawType, result);
  } else if (lhsType && rhsType && lhsType->isNumeric() &&
             rhsType->isNumeric()) {
    // Float math in double, then converted (f32 rounds).
    llvm::Value *raw =
        emitFloatOp(op, toF64(lhs, lhsType), toF64(rhs, rhsType), where);
    if (!raw)
      return defaultValue(result);
    return convert(raw, isComparison(op) ? types_.boolType()
                                         : types_.floatType(),
                   result);
  }
  cgm_.error(where.range(), "code generation doesn't support this operation "
                            "on " + Detail::displayOrUnknown(lhsType) + " and " +
                                Detail::displayOrUnknown(rhsType));
  return defaultValue(result);
}

llvm::Value *CodegenFunction::emitIntOp(BinaryOp op, llvm::Value *a,
                                        llvm::Value *b, const Node &where,
                                        bool isUnsigned) {
  llvm::Type *i64 = builder_.getInt64Ty();
  llvm::Value *zero = builder_.getInt64(0);
  llvm::Value *minusOne = llvm::ConstantInt::getSigned(i64, -1);
  if (isUnsigned) {
    // Add, Sub, Mul, the bitwise ops, Shl, Eq and Ne are the same bits
    // either way; the rest have unsigned forms. `//` and `%` need no
    // rounding fix: nothing is negative.
    switch (op) {
    case BinaryOp::Div:
      panicIf(builder_.CreateICmpEQ(b, zero), "division by zero");
      return builder_.CreateFDiv(builder_.CreateUIToFP(a, builder_.getDoubleTy()),
                                 builder_.CreateUIToFP(b, builder_.getDoubleTy()));
    case BinaryOp::FloorDiv:
      panicIf(builder_.CreateICmpEQ(b, zero), "integer division by zero");
      return builder_.CreateUDiv(a, b);
    case BinaryOp::Mod:
      panicIf(builder_.CreateICmpEQ(b, zero), "integer modulo by zero");
      return builder_.CreateURem(a, b);
    case BinaryOp::Pow:
      return builder_.CreateCall(cgm_.runtime(RuntimeFn::UPow), {a, b});
    case BinaryOp::Lt: return builder_.CreateICmpULT(a, b);
    case BinaryOp::Le: return builder_.CreateICmpULE(a, b);
    case BinaryOp::Gt: return builder_.CreateICmpUGT(a, b);
    case BinaryOp::Ge: return builder_.CreateICmpUGE(a, b);
    default: break; // Shr is handled below.
    }
  }
  switch (op) {
  // Wrap-around arithmetic: LLVM's add/sub/mul without nsw.
  case BinaryOp::Add: return builder_.CreateAdd(a, b);
  case BinaryOp::Sub: return builder_.CreateSub(a, b);
  case BinaryOp::Mul: return builder_.CreateMul(a, b);
  case BinaryOp::Div: // `/` is true division.
    panicIf(builder_.CreateICmpEQ(b, zero), "division by zero");
    return builder_.CreateFDiv(builder_.CreateSIToFP(a, builder_.getDoubleTy()),
                               builder_.CreateSIToFP(b, builder_.getDoubleTy()));
  case BinaryOp::FloorDiv: {
    // Rounds toward minus infinity. INT64_MIN // -1 wraps to INT64_MIN; the
    // divisor is swapped for 1 there, since sdiv would overflow.
    panicIf(builder_.CreateICmpEQ(b, zero), "integer division by zero");
    llvm::Value *overflow = builder_.CreateAnd(
        builder_.CreateICmpEQ(
            a, llvm::ConstantInt::get(i64, llvm::APInt::getSignedMinValue(64))),
        builder_.CreateICmpEQ(b, minusOne));
    llvm::Value *divisor =
        builder_.CreateSelect(overflow, builder_.getInt64(1), b);
    llvm::Value *quotient = builder_.CreateSDiv(a, divisor);
    llvm::Value *remainder = builder_.CreateSRem(a, divisor);
    llvm::Value *adjust = builder_.CreateAnd(
        builder_.CreateICmpNE(remainder, zero),
        builder_.CreateICmpNE(builder_.CreateICmpSLT(a, zero),
                              builder_.CreateICmpSLT(b, zero)));
    return builder_.CreateSub(quotient, builder_.CreateZExt(adjust, i64));
  }
  case BinaryOp::Mod: {
    // Takes the divisor's sign, so a == (a // b) * b + a % b.
    panicIf(builder_.CreateICmpEQ(b, zero), "integer modulo by zero");
    llvm::Value *divisor = builder_.CreateSelect(
        builder_.CreateICmpEQ(b, minusOne), builder_.getInt64(1), b);
    llvm::Value *remainder = builder_.CreateSRem(a, divisor);
    llvm::Value *adjust = builder_.CreateAnd(
        builder_.CreateICmpNE(remainder, zero),
        builder_.CreateICmpNE(builder_.CreateICmpSLT(remainder, zero),
                              builder_.CreateICmpSLT(b, zero)));
    return builder_.CreateAdd(remainder,
                              builder_.CreateSelect(adjust, b, zero));
  }
  case BinaryOp::Pow:
    return builder_.CreateCall(cgm_.runtime(RuntimeFn::IPow), {a, b});
  case BinaryOp::BitAnd: return builder_.CreateAnd(a, b);
  case BinaryOp::BitOr: return builder_.CreateOr(a, b);
  case BinaryOp::BitXor: return builder_.CreateXor(a, b);
  case BinaryOp::Shl:
  case BinaryOp::Shr: {
    // Shift amounts outside 0..63 are an error (an unsigned compare catches
    // negative ones too).
    llvm::LLVMContext &context = cgm_.getLLVMContext();
    auto *fail = llvm::BasicBlock::Create(context, "shift.bad", function_);
    auto *ok = llvm::BasicBlock::Create(context, "shift.ok", function_);
    llvm::MDBuilder weights(context);
    builder_.CreateCondBr(builder_.CreateICmpUGT(b, builder_.getInt64(63)),
                          fail, ok, weights.createUnlikelyBranchWeights());
    builder_.SetInsertPoint(fail);
    builder_.CreateCall(cgm_.runtime(RuntimeFn::ShiftError), {b});
    builder_.CreateUnreachable();
    builder_.SetInsertPoint(ok);
    if (op == BinaryOp::Shl)
      return builder_.CreateShl(a, b);
    return isUnsigned ? builder_.CreateLShr(a, b) : builder_.CreateAShr(a, b);
  }
  case BinaryOp::Eq: return builder_.CreateICmpEQ(a, b);
  case BinaryOp::Ne: return builder_.CreateICmpNE(a, b);
  case BinaryOp::Lt: return builder_.CreateICmpSLT(a, b);
  case BinaryOp::Le: return builder_.CreateICmpSLE(a, b);
  case BinaryOp::Gt: return builder_.CreateICmpSGT(a, b);
  case BinaryOp::Ge: return builder_.CreateICmpSGE(a, b);
  default:
    cgm_.error(where.range(), "code generation doesn't support this integer "
                              "operation");
    return nullptr;
  }
}

llvm::Value *CodegenFunction::emitFloatOp(BinaryOp op, llvm::Value *a,
                                          llvm::Value *b, const Node &where) {
  switch (op) {
  case BinaryOp::Add: return builder_.CreateFAdd(a, b);
  case BinaryOp::Sub: return builder_.CreateFSub(a, b);
  case BinaryOp::Mul: return builder_.CreateFMul(a, b);
  case BinaryOp::Div: return builder_.CreateFDiv(a, b);
  case BinaryOp::FloorDiv:
    return builder_.CreateUnaryIntrinsic(llvm::Intrinsic::floor,
                                         builder_.CreateFDiv(a, b));
  case BinaryOp::Mod: {
    llvm::Value *floored = builder_.CreateUnaryIntrinsic(
        llvm::Intrinsic::floor, builder_.CreateFDiv(a, b));
    return builder_.CreateFSub(a, builder_.CreateFMul(floored, b));
  }
  case BinaryOp::Pow:
    return builder_.CreateBinaryIntrinsic(llvm::Intrinsic::pow, a, b);
  // Like C++: comparisons with NaN are false, except `!=`.
  case BinaryOp::Eq: return builder_.CreateFCmpOEQ(a, b);
  case BinaryOp::Ne: return builder_.CreateFCmpUNE(a, b);
  case BinaryOp::Lt: return builder_.CreateFCmpOLT(a, b);
  case BinaryOp::Le: return builder_.CreateFCmpOLE(a, b);
  case BinaryOp::Gt: return builder_.CreateFCmpOGT(a, b);
  case BinaryOp::Ge: return builder_.CreateFCmpOGE(a, b);
  default:
    cgm_.error(where.range(), "code generation doesn't support this float "
                              "operation");
    return nullptr;
  }
}

llvm::Value *CodegenFunction::emitMatrixOp(BinaryOp op, llvm::Value *lhs,
                                           const Type *lhsType,
                                           llvm::Value *rhs,
                                           const Type *rhsType,
                                           const Node &where) {
  bool lhsMatrix = isMatrix(lhsType);
  bool rhsMatrix = isMatrix(rhsType);
  if ((op == BinaryOp::Eq || op == BinaryOp::Ne) && lhsMatrix && rhsMatrix) {
    // A bool, not a matrix, so it returns here. The releases balance the
    // reference each (never consumed) operand read took.
    llvm::Value *same = builder_.CreateCall(
        cgm_.runtime(RuntimeFn::MatrixEqual), {lhs, rhs});
    release(lhs);
    release(rhs);
    llvm::Value *zero = builder_.getInt32(0);
    return op == BinaryOp::Eq ? builder_.CreateICmpNE(same, zero)
                              : builder_.CreateICmpEQ(same, zero);
  }
  llvm::Value *result = nullptr;
  if (op == BinaryOp::MatMul && lhsMatrix && rhsMatrix) {
    result = builder_.CreateCall(cgm_.runtime(RuntimeFn::Matmul), {lhs, rhs});
  } else if ((op == BinaryOp::Add || op == BinaryOp::Sub) && lhsMatrix &&
             rhsMatrix) {
    result = builder_.CreateCall(
        cgm_.runtime(RuntimeFn::Add),
        {lhs, rhs, builder_.getInt32(op == BinaryOp::Sub ? 1 : 0)});
  } else if (op == BinaryOp::Mul && lhsMatrix && !rhsMatrix) {
    result = builder_.CreateCall(cgm_.runtime(RuntimeFn::Scale),
                                 {lhs, toF64(rhs, rhsType)});
  } else if (op == BinaryOp::Mul && rhsMatrix && !lhsMatrix) {
    result = builder_.CreateCall(cgm_.runtime(RuntimeFn::Scale),
                                 {rhs, toF64(lhs, lhsType)});
  } else if (op == BinaryOp::Div && lhsMatrix && !rhsMatrix) {
    llvm::Value *divisor = toF64(rhs, rhsType);
    panicIf(builder_.CreateFCmpOEQ(
                divisor, llvm::ConstantFP::get(builder_.getDoubleTy(), 0.0)),
            "matrix divided by zero");
    result = builder_.CreateCall(
        cgm_.runtime(RuntimeFn::Scale),
        {lhs, builder_.CreateFDiv(
                  llvm::ConstantFP::get(builder_.getDoubleTy(), 1.0), divisor)});
  } else {
    cgm_.error(where.range(), "code generation doesn't support this matrix "
                              "operation");
    return llvm::Constant::getNullValue(builder_.getPtrTy());
  }
  // The operation is done with its operands: consumed ones are freed now.
  if (lhsMatrix)
    release(lhs);
  if (rhsMatrix)
    release(rhs);
  return result;
}

llvm::Value *CodegenFunction::emitCall(const CallExpr &call) {
  if (call.callee().kind() != NodeKind::NameExpr) {
    cgm_.error(call.callee().range(), "only named functions can be called");
    return defaultValue(call.resolvedType());
  }
  const auto &callee = static_cast<const NameExpr &>(call.callee());
  auto id = cgm_.getSema().symbolFor(callee);
  if (!id) {
    cgm_.error(callee.range(), "internal error: '" + callee.name() +
                                   "' was never resolved");
    return defaultValue(call.resolvedType());
  }
  const sema::Symbol &symbol = cgm_.getSema().symbol(*id);

  if (symbol.kind == sema::Symbol::Kind::Builtin) { // copy(x)
    const Expr &arg = *call.args()[0];
    llvm::Value *value = emitExpr(arg);
    if (!isMatrix(arg.resolvedType()))
      return value;
    llvm::Value *copy =
        builder_.CreateCall(cgm_.runtime(RuntimeFn::Copy), {value});
    release(value);
    return copy;
  }

  const FunctionDef &function = *symbol.function;
  const auto *signature = dynCast<FunctionType>(function.resolvedType());
  if (!signature) {
    cgm_.error(call.range(), "internal error: '" + function.name() +
                                 "' has no type");
    return defaultValue(call.resolvedType());
  }
  // Arguments run in source order; each goes to its parameter's position.
  assert(shouldEvaluateArgsLeftToRight(getCallArgEvaluationOrder(call)));
  std::vector<std::size_t> paramForArg = mapCallArguments(call, function);
  std::vector<llvm::Value *> args(function.params().size(), nullptr);
  for (std::size_t i = 0; i < call.args().size(); ++i) {
    const Expr &arg = *call.args()[i];
    std::size_t index = paramForArg[i];
    llvm::Value *value = emitExpr(arg);
    if (index < args.size())
      args[index] = convert(value, arg.resolvedType(), signature->params()[index]);
  }

  if (CodegenModule::isRuntimeExtern(function))
    return emitRuntimeExtern(function, args, call);

  llvm::Function *lowered = cgm_.functionFor(function);
  if (!lowered)
    return defaultValue(call.resolvedType()); // Already reported.
  return builder_.CreateCall(lowered, args);
}

llvm::Value *CodegenFunction::emitRuntimeExtern(const FunctionDef &function,
                                                std::vector<llvm::Value *> &args,
                                                const CallExpr &call) {
  const auto *signature = dynCast<FunctionType>(function.resolvedType());
  const Type *returns = signature->returnType();
  auto intArg = [&](std::size_t i) {
    return toI64(args[i], signature->params()[i]);
  };
  llvm::Value *element = builder_.getInt32(f32Flag(returns));
  const std::string &name = function.name();
  if (name == "random_matrix")
    return builder_.CreateCall(cgm_.runtime(RuntimeFn::Random),
                               {intArg(0), intArg(1), element});
  if (name == "zeros_matrix")
    return builder_.CreateCall(cgm_.runtime(RuntimeFn::Zeros),
                               {intArg(0), intArg(1), element});
  if (name == "identity_matrix")
    return builder_.CreateCall(cgm_.runtime(RuntimeFn::Identity),
                               {intArg(0), element});
  // matrix_sum: takes (and so frees) its argument.
  llvm::Value *sum =
      builder_.CreateCall(cgm_.runtime(RuntimeFn::Sum), {args[0]});
  release(args[0]);
  (void)call;
  return convert(sum, types_.floatType(), returns);
}

} // namespace MemoLang
