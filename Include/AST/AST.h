#ifndef AST_H_
#define AST_H_

#include <cassert>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <span>

//  Private data members are named [name]_ 
//  Nodes are move-only. Use clone() for deep copies.
//  * Preconditions use assert().

namespace MemoLang {

// Forward declarations
class Type; // Semantic type

class Visitor;
class TypeExpr;
class IntegerLiteral;
class FloatLiteral;
class StringLiteral;
class InterpolatedString;
class BooleanLiteral;
class UnaryExpr;
class BinaryExpr;
class CallExpr;
class CastExpr;
class ExprStmt;
class DeclareVar;
class FunctionDef;

// ---------------------------------------------------------------------------
// Enums
// ---------------------------------------------------------------------------
// TODO (not implemented yet): LambdaExpr, MacroDef.
enum class NodeKind {
    // Expressions
    IntegerLiteral,
    FloatLiteral,
    StringLiteral,
    InterpolatedString,
    BooleanLiteral,
    UnaryExpr,
    BinaryExpr,
    CallExpr,
    CastExpr,
    FirstExpr = IntegerLiteral,
    LastExpr = CastExpr,

    // Statements
    ExprStmt,
    DeclareVar,
    FunctionDef,
    FirstStmt = ExprStmt,
    LastStmt = FunctionDef,

    // Type expressions
    TypeExpr,
};

enum class State { Live, Consumed, MaybeConsumed };  // There is no "maybe live".

enum class BinaryOp {
    Add, Sub, Mul, Div, FloorDiv, Mod, Pow,
    Eq, Ne, Lt, Le, Gt, Ge,
    BitAnd, BitOr, BitXor, Shl, Shr,
    And, Or,
    Is, IsNot,
    In, NotIn,
};

enum class UnaryOp {
    PreInc, PostInc,
    PreDec, PostDec,
    Neg,
    Inv,
};

enum class AssignOp { Assign, Add, Sub, Mul, Div };


// Flags for DeclareVar (instead of two easily-swapped bools).
enum class DeclFlags : std::uint8_t {
    None = 0,
    Static = 1 << 0,
    Const = 1 << 1,
};
[[nodiscard]] constexpr DeclFlags operator|(DeclFlags a, DeclFlags b) noexcept {
    return static_cast<DeclFlags>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}
[[nodiscard]] constexpr DeclFlags operator&(DeclFlags a, DeclFlags b) noexcept {
    return static_cast<DeclFlags>(static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(b));
}

// ---------------------------------------------------------------------------
// Operator -> dunder method names
// Wow nice i can copy // ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
struct BinaryDunderNames {
    std::string_view forward;    // e.g. "__add__"  (empty: no dunder for this op)
    std::string_view reflected;  // e.g. "__radd__"
};
[[nodiscard]] constexpr BinaryDunderNames binaryDunderNames(BinaryOp op) noexcept {
    switch (op) {
    case BinaryOp::Add:      return {"__add__", "__radd__"};
    case BinaryOp::Sub:      return {"__sub__", "__rsub__"};
    case BinaryOp::Mul:      return {"__mul__", "__rmul__"};
    case BinaryOp::Div:      return {"__truediv__", "__rtruediv__"};
    case BinaryOp::FloorDiv: return {"__floordiv__", "__rfloordiv__"};
    case BinaryOp::Mod:      return {"__mod__", "__rmod__"};
    case BinaryOp::Pow:      return {"__pow__", "__rpow__"};
    case BinaryOp::Eq:       return {"__eq__", "__eq__"};
    case BinaryOp::Ne:       return {"__ne__", "__ne__"};
    case BinaryOp::Lt:       return {"__lt__", "__gt__"};
    case BinaryOp::Le:       return {"__le__", "__ge__"};
    case BinaryOp::Gt:       return {"__gt__", "__lt__"};
    case BinaryOp::Ge:       return {"__ge__", "__le__"};
    case BinaryOp::BitAnd:   return {"__and__", "__rand__"};
    case BinaryOp::BitOr:    return {"__or__", "__ror__"};
    case BinaryOp::BitXor:   return {"__xor__", "__rxor__"};
    case BinaryOp::Shl:      return {"__lshift__", "__rlshift__"};
    case BinaryOp::Shr:      return {"__rshift__", "__rrshift__"};
    case BinaryOp::And:
    case BinaryOp::Or:
    case BinaryOp::Is:
    case BinaryOp::IsNot:
    case BinaryOp::In:
    case BinaryOp::NotIn:
        break;
    }
    return {};
}

// ---------------------------------------------------------------------------
// Visitor: one visit() per concrete node. Codegen, type checking, printing,
// etc. are all Visitors implemented outside this header.
// ---------------------------------------------------------------------------
class Visitor {
public:
    virtual ~Visitor() = default;

    virtual void visit(const TypeExpr&) = 0;
    virtual void visit(const IntegerLiteral&) = 0;
    virtual void visit(const FloatLiteral&) = 0;
    virtual void visit(const StringLiteral&) = 0;
    virtual void visit(const InterpolatedString&) = 0;
    virtual void visit(const BooleanLiteral&) = 0;
    virtual void visit(const UnaryExpr&) = 0;
    virtual void visit(const BinaryExpr&) = 0;
    virtual void visit(const CallExpr&) = 0;
    virtual void visit(const CastExpr&) = 0;
    virtual void visit(const ExprStmt&) = 0;
    virtual void visit(const DeclareVar&) = 0;
    virtual void visit(const FunctionDef&) = 0;
    enum class State { // State is along with visitors, then revisited in analysis for consume flags
        Live,
        Consumed,
        MaybeConsumed,
    };
};

// ---------------------------------------------------------------------------
// Node
// ---------------------------------------------------------------------------
struct SourceLoc {
    std::uint32_t line = 0;  // 1-based; 0 = unknown
    std::uint32_t column = 0;
};

class Node {
public:
    virtual ~Node() = default; // Base virtual
    Node(const Node&) = delete;  // AST nodes are move-only; use clone().
    Node& operator=(const Node&) = delete;

    [[nodiscard]] NodeKind kind() const noexcept { return kind_; }

    [[nodiscard]] SourceLoc loc() const noexcept { return loc_; }
    void setLoc(SourceLoc loc) noexcept { loc_ = loc; }

    // The resolved type is analysis output, not part of the tree's structure,
    // so passes may set it through a const tree.
    [[nodiscard]] const Type* resolvedType() const noexcept { return resolvedType_; }
    void setResolvedType(const Type* t) const noexcept { resolvedType_ = t; }

    virtual void accept(Visitor& v) const = 0;

protected:
    explicit Node(NodeKind k) noexcept : kind_(k) {}
    Node(Node&&) noexcept = default;
    Node& operator=(Node&&) noexcept = default;

private:
    NodeKind kind_;
    SourceLoc loc_{};
    mutable const Type* resolvedType_ = nullptr;
};
class TypeExpr final : public Node {
public:
    explicit TypeExpr(std::string name, std::vector<std::unique_ptr<TypeExpr>> args = {})
        : Node(NodeKind::TypeExpr), name_(std::move(name)), args_(std::move(args)) {
        for (const auto& a : args_) assert(a && "TypeExpr argument must not be null");
    }

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    std::span<const std::unique_ptr<TypeExpr>> args() const noexcept { return args_; }

    [[nodiscard]] std::unique_ptr<TypeExpr> clone() const {
        std::vector<std::unique_ptr<TypeExpr>> copy;
        copy.reserve(args_.size());
        for (const auto& a : args_) copy.push_back(a->clone());
        auto result = std::make_unique<TypeExpr>(name_, std::move(copy));
        result->setLoc(loc());
        return result;
    }

    void accept(Visitor& v) const override { v.visit(*this); }

private:
    std::string name_;
    std::vector<std::unique_ptr<TypeExpr>> args_;
};

// ---------------------------------------------------------------------------
// Expr
// ---------------------------------------------------------------------------
class Expr : public Node {
public:
    [[nodiscard]] virtual std::unique_ptr<Expr> clone() const = 0;

protected:
    explicit Expr(NodeKind k) noexcept : Node(k) {}
};

// A piece of an interpolated string: literal text and/or an embedded expression.
struct StringPart {
    std::string literal;
    std::unique_ptr<Expr> value;  // Nullable: null means a pure literal part.
    std::string spec;             // Format spec after ':'. Empty means plain.

    [[nodiscard]] StringPart clone() const {
        StringPart copy;
        copy.literal = literal;
        copy.value = value ? value->clone() : nullptr;
        copy.spec = spec;
        return copy;
    }
};

// ---------------------------------------------------------------------------
// Literals
// ---------------------------------------------------------------------------
class IntegerLiteral final : public Expr {
public:
    // Range validation (int64 overflow, byte range) belongs in the parser;
    // the literal `-9223372036854775808` must be handled there.
    explicit IntegerLiteral(std::int64_t value, bool isByte = false)
        : Expr(NodeKind::IntegerLiteral), value_(value), isByte_(isByte) {}

    [[nodiscard]] std::int64_t value() const noexcept { return value_; }
    [[nodiscard]] bool isByte() const noexcept { return isByte_; }

    [[nodiscard]] std::unique_ptr<Expr> clone() const override {
        auto c = std::make_unique<IntegerLiteral>(value_, isByte_);
        c->setLoc(loc());
        return c;
    }
    void accept(Visitor& v) const override { v.visit(*this); }

private:
    std::int64_t value_;
    bool isByte_;
};

class FloatLiteral final : public Expr {
public:
    explicit FloatLiteral(double value, bool isF32 = false)
        : Expr(NodeKind::FloatLiteral), value_(value), isF32_(isF32) {}

    [[nodiscard]] double value() const noexcept { return value_; }
    [[nodiscard]] bool isF32() const noexcept { return isF32_; }  // Value is rounded to float on codegen.

    [[nodiscard]] std::unique_ptr<Expr> clone() const override {
        auto c = std::make_unique<FloatLiteral>(value_, isF32_);
        c->setLoc(loc());
        return c;
    }
    void accept(Visitor& v) const override { v.visit(*this); }

private:
    double value_;
    bool isF32_;
};

class StringLiteral final : public Expr {
public:
    explicit StringLiteral(std::string value, bool isRegex = false)
        : Expr(NodeKind::StringLiteral), value_(std::move(value)), isRegex_(isRegex) {}

    [[nodiscard]] const std::string& value() const noexcept { return value_; }
    [[nodiscard]] bool isRegex() const noexcept { return isRegex_; }  // Pattern validity is checked later.

    [[nodiscard]] std::unique_ptr<Expr> clone() const override {
        auto c = std::make_unique<StringLiteral>(value_, isRegex_);
        c->setLoc(loc());
        return c;
    }
    void accept(Visitor& v) const override { v.visit(*this); }

private:
    std::string value_;
    bool isRegex_;
};

class InterpolatedString final : public Expr {
public:
    explicit InterpolatedString(std::vector<StringPart> parts)
        : Expr(NodeKind::InterpolatedString), parts_(std::move(parts)) {}

    [[nodiscard]] std::span<const StringPart> parts() const noexcept { return parts_; }

    [[nodiscard]] std::unique_ptr<Expr> clone() const override {
        std::vector<StringPart> copy;
        copy.reserve(parts_.size());
        for (const auto& p : parts_) copy.push_back(p.clone());
        auto c = std::make_unique<InterpolatedString>(std::move(copy));
        c->setLoc(loc());
        return c;
    }
    void accept(Visitor& v) const override { v.visit(*this); }

private:
    std::vector<StringPart> parts_;
};

class BooleanLiteral final : public Expr {
public:
    explicit BooleanLiteral(bool value) : Expr(NodeKind::BooleanLiteral), value_(value) {}

    [[nodiscard]] bool value() const noexcept { return value_; }

    [[nodiscard]] std::unique_ptr<Expr> clone() const override {
        auto c = std::make_unique<BooleanLiteral>(value_);
        c->setLoc(loc());
        return c;
    }
    void accept(Visitor& v) const override { v.visit(*this); }

private:
    bool value_;
};

// ---------------------------------------------------------------------------
// Operators
// ---------------------------------------------------------------------------
class UnaryExpr final : public Expr {
public:
    UnaryExpr(UnaryOp op, std::unique_ptr<Expr> operand)
        : Expr(NodeKind::UnaryExpr), op_(op), operand_(std::move(operand)) {
        assert(operand_ && "UnaryExpr operand must not be null");
    }

    [[nodiscard]] UnaryOp op() const noexcept { return op_; }
    [[nodiscard]] const Expr& operand() const noexcept { return *operand_; }
    [[nodiscard]] Expr& operand() noexcept { return *operand_; }

    [[nodiscard]] std::unique_ptr<Expr> clone() const override {
        auto c = std::make_unique<UnaryExpr>(op_, operand_->clone());
        c->setLoc(loc());
        return c;
    }
    void accept(Visitor& v) const override { v.visit(*this); }

private:
    UnaryOp op_;
    std::unique_ptr<Expr> operand_;
};

class BinaryExpr final : public Expr {
public:
    BinaryExpr(BinaryOp op, std::unique_ptr<Expr> lhs, std::unique_ptr<Expr> rhs)
        : Expr(NodeKind::BinaryExpr), op_(op), lhs_(std::move(lhs)), rhs_(std::move(rhs)) {
        assert(lhs_ && rhs_ && "BinaryExpr operands must not be null");
    }

    [[nodiscard]] BinaryOp op() const noexcept { return op_; }
    [[nodiscard]] const Expr& lhs() const noexcept { return *lhs_; }
    [[nodiscard]] Expr& lhs() noexcept { return *lhs_; }
    [[nodiscard]] const Expr& rhs() const noexcept { return *rhs_; }
    [[nodiscard]] Expr& rhs() noexcept { return *rhs_; }

    [[nodiscard]] std::unique_ptr<Expr> clone() const override {
        auto c = std::make_unique<BinaryExpr>(op_, lhs_->clone(), rhs_->clone());
        c->setLoc(loc());
        return c;
    }
    void accept(Visitor& v) const override { v.visit(*this); }

private:
    BinaryOp op_;
    std::unique_ptr<Expr> lhs_;
    std::unique_ptr<Expr> rhs_;
};

class CastExpr final : public Expr {
public:
    CastExpr(std::unique_ptr<Expr> value, std::unique_ptr<TypeExpr> target)
        : Expr(NodeKind::CastExpr), value_(std::move(value)), target_(std::move(target)) {
        assert(value_ && target_ && "CastExpr value and target must not be null");
    }

    [[nodiscard]] const Expr& value() const noexcept { return *value_; }
    [[nodiscard]] Expr& value() noexcept { return *value_; }
    [[nodiscard]] const TypeExpr& target() const noexcept { return *target_; }
    [[nodiscard]] TypeExpr& target() noexcept { return *target_; }

    [[nodiscard]] std::unique_ptr<Expr> clone() const override {
        auto c = std::make_unique<CastExpr>(value_->clone(), target_->clone());
        c->setLoc(loc());
        return c;
    }
    void accept(Visitor& v) const override { v.visit(*this); }

private:
    std::unique_ptr<Expr> value_;
    std::unique_ptr<TypeExpr> target_;
};

// ---------------------------------------------------------------------------
// CallExpr
// ---------------------------------------------------------------------------
class CallExpr final : public Expr {
public:
    CallExpr(std::unique_ptr<Expr> callee,
             std::vector<std::unique_ptr<Expr>> args,
             std::vector<std::unique_ptr<TypeExpr>> typeArgs = {},
             std::vector<std::string> paramNames = {})
        : Expr(NodeKind::CallExpr),
          callee_(std::move(callee)),
          args_(std::move(args)),
          typeArgs_(std::move(typeArgs)),
          paramNames_(std::move(paramNames)) {
        assert(callee_ && "CallExpr callee must not be null");
        for (const auto& a : args_) assert(a && "CallExpr argument must not be null");
        for (const auto& t : typeArgs_) assert(t && "CallExpr type argument must not be null");
        assert((paramNames_.empty() || paramNames_.size() == args_.size()) &&
               "paramNames must be empty or match args one-to-one");
    }

    [[nodiscard]] const Expr& callee() const noexcept { return *callee_; }
    [[nodiscard]] Expr& callee() noexcept { return *callee_; }
    [[nodiscard]] std::span<const std::unique_ptr<Expr>> args() const noexcept { return args_; }
    [[nodiscard]] std::span<const std::unique_ptr<TypeExpr>> typeArgs() const noexcept { return typeArgs_; }
    // Named arguments: empty, or one (possibly empty) name per argument.
    [[nodiscard]] std::span<const std::string> paramNames() const noexcept { return paramNames_; }

    void setParamNames(std::vector<std::string> names) {
        assert((names.empty() || names.size() == args_.size()) && "names must match args one-to-one");
        paramNames_ = std::move(names);
    }

    [[nodiscard]] std::unique_ptr<Expr> clone() const override {
        std::vector<std::unique_ptr<Expr>> a;
        a.reserve(args_.size());
        for (const auto& x : args_) a.push_back(x->clone());
        std::vector<std::unique_ptr<TypeExpr>> t;
        t.reserve(typeArgs_.size());
        for (const auto& x : typeArgs_) t.push_back(x->clone());
        auto c = std::make_unique<CallExpr>(callee_->clone(), std::move(a), std::move(t), paramNames_);
        c->setLoc(loc());
        return c;
    }
    void accept(Visitor& v) const override { v.visit(*this); }

private:
    std::unique_ptr<Expr> callee_;
    std::vector<std::unique_ptr<Expr>> args_;
    std::vector<std::unique_ptr<TypeExpr>> typeArgs_;
    std::vector<std::string> paramNames_;
};

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------
class Statement : public Node {
protected:
    explicit Statement(NodeKind k) noexcept : Node(k) {}
};

// An expression used as a statement, e.g. a bare call `foo();`.
class ExprStmt final : public Statement {
public:
    explicit ExprStmt(std::unique_ptr<Expr> expr)
        : Statement(NodeKind::ExprStmt), expr_(std::move(expr)) {
        assert(expr_ && "ExprStmt expression must not be null");
    }

    [[nodiscard]] const Expr& expr() const noexcept { return *expr_; }
    [[nodiscard]] Expr& expr() noexcept { return *expr_; }

    void accept(Visitor& v) const override { v.visit(*this); }

private:
    std::unique_ptr<Expr> expr_;
};

class DeclareVar final : public Statement {
public:
    // `type` is nullable (inferred from init); `init` is nullable (declaration
    // only). At least one of them must be present.
    DeclareVar(std::string name,
               std::unique_ptr<TypeExpr> type,
               std::unique_ptr<Expr> init,
               DeclFlags flags = DeclFlags::None)
        : Statement(NodeKind::DeclareVar),
          name_(std::move(name)),
          type_(std::move(type)),
          init_(std::move(init)),
          flags_(flags) {
        assert(!name_.empty() && "variable name must not be empty");
        assert((type_ || init_) && "declaration needs a type or an initializer");
    }

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    void setName(std::string name) { name_ = std::move(name); }
    [[nodiscard]] const TypeExpr* type() const noexcept { return type_.get(); }      // May be null.
    [[nodiscard]] const Expr* init() const noexcept { return init_.get(); }          // May be null.
    [[nodiscard]] bool isStatic() const noexcept { return (flags_ & DeclFlags::Static) != DeclFlags::None; }
    [[nodiscard]] bool isConst() const noexcept { return (flags_ & DeclFlags::Const) != DeclFlags::None; }

    void accept(Visitor& v) const override { v.visit(*this); }

private:
    std::string name_;
    std::unique_ptr<TypeExpr> type_;
    std::unique_ptr<Expr> init_;
    DeclFlags flags_;
};

class FunctionDef final : public Statement {
public:
    struct Param {
        std::string name;
        std::unique_ptr<TypeExpr> type;  // Non-null.
    };

    // Non-empty externName means the function is extern and must have no body.
    FunctionDef(std::string name,
                std::vector<Param> params,
                std::unique_ptr<TypeExpr> returnType,
                std::vector<std::unique_ptr<Statement>> body,
                std::string externName = {})
        : Statement(NodeKind::FunctionDef),
          name_(std::move(name)),
          params_(std::move(params)),
          returnType_(std::move(returnType)),
          body_(std::move(body)),
          externName_(std::move(externName)) {
        assert(!name_.empty() && "function name must not be empty");
        assert(returnType_ && "return type must not be null");
        for (const auto& p : params_) assert(p.type && "parameter type must not be null");
        for (const auto& s : body_) assert(s && "body statement must not be null");
        assert((externName_.empty() || body_.empty()) && "extern function must not have a body");
    }

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] std::span<const Param> params() const noexcept { return params_; }
    [[nodiscard]] const TypeExpr& returnType() const noexcept { return *returnType_; }
    [[nodiscard]] TypeExpr& returnType() noexcept { return *returnType_; }
    [[nodiscard]] std::span<const std::unique_ptr<Statement>> body() const noexcept { return body_; }
    [[nodiscard]] const std::string& externName() const noexcept { return externName_; }
    [[nodiscard]] bool isExtern() const noexcept { return !externName_.empty(); }

    void accept(Visitor& v) const override { v.visit(*this); }

private:
    std::string name_;
    std::vector<Param> params_;
    std::unique_ptr<TypeExpr> returnType_;
    std::vector<std::unique_ptr<Statement>> body_;
    std::string externName_;
};

}  // namespace Memolang

#endif  // AST_H_