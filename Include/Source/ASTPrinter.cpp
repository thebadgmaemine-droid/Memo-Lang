// ---------------------------------------------------------------------------
// ASTPrinter.cpp: prints the AST as an indented tree (`MemoLang --dump-ast`)
//
// The printer is a Visitor (see AST.h): each node's accept() calls back the
// matching visit() here, and each tree level is indented two more spaces. The
// tests compare this output with the .ast files in tests/ast/.
//
// History: added 2026-10-14.
// ---------------------------------------------------------------------------
#include "AST/ASTPrinter.h"

#include <ostream>
#include <string>
#include <string_view>

namespace MemoLang {
namespace {

std::string_view binaryOpSpelling(BinaryOp op) {
    switch (op) {
    case BinaryOp::Add:      return "+";
    case BinaryOp::Sub:      return "-";
    case BinaryOp::Mul:      return "*";
    case BinaryOp::Div:      return "/";
    case BinaryOp::FloorDiv: return "//";
    case BinaryOp::Mod:      return "%";
    case BinaryOp::Pow:      return "**";
    case BinaryOp::MatMul:   return "@";
    case BinaryOp::Eq:       return "==";
    case BinaryOp::Ne:       return "!=";
    case BinaryOp::Lt:       return "<";
    case BinaryOp::Le:       return "<=";
    case BinaryOp::Gt:       return ">";
    case BinaryOp::Ge:       return ">=";
    case BinaryOp::BitAnd:   return "&";
    case BinaryOp::BitOr:    return "|";
    case BinaryOp::BitXor:   return "^";
    case BinaryOp::Shl:      return "<<";
    case BinaryOp::Shr:      return ">>";
    case BinaryOp::And:      return "and";
    case BinaryOp::Or:       return "or";
    case BinaryOp::Is:       return "is";
    case BinaryOp::IsNot:    return "is not";
    case BinaryOp::In:       return "in";
    case BinaryOp::NotIn:    return "not in";
    }
    return "?";
}

std::string_view unaryOpSpelling(UnaryOp op) {
    switch (op) {
    case UnaryOp::PreInc:  return "++ (prefix)";
    case UnaryOp::PostInc: return "++ (postfix)";
    case UnaryOp::PreDec:  return "-- (prefix)";
    case UnaryOp::PostDec: return "-- (postfix)";
    case UnaryOp::Neg:     return "-";
    case UnaryOp::Inv:     return "~";
    case UnaryOp::Not:     return "not";
    }
    return "?";
}

std::string_view assignOpSpelling(AssignOp op) {
    switch (op) {
    case AssignOp::Assign: return "=";
    case AssignOp::Add:    return "+=";
    case AssignOp::Sub:    return "-=";
    case AssignOp::Mul:    return "*=";
    case AssignOp::Div:    return "/=";
    case AssignOp::Mod:    return "%=";
    }
    return "?";
}

std::string typeToString(const TypeExpr& type) {
    std::string text = type.name();
    if (!type.args().empty()) {
        text += '<';
        bool first = true;
        for (const auto& arg : type.args()) {
            if (!first) text += ", ";
            text += typeToString(*arg);
            first = false;
        }
        text += '>';
    }
    return text;
}

std::string escape(std::string_view text) {
    std::string out;
    for (char c : text) {
        switch (c) {
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        case '\r': out += "\\r"; break;
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        default:   out += c;
        }
    }
    return out;
}

class Printer final : public Visitor {
public:
    explicit Printer(std::ostream& out) : out_(out) {}

    void printBody(std::span<const std::unique_ptr<Statement>> body) {
        ++depth_;
        for (const auto& stmt : body) stmt->accept(*this);
        --depth_;
    }

    void visit(const TypeExpr& n) override { indent() << "Type " << typeToString(n) << '\n'; }
    void visit(const NameExpr& n) override { indent() << "Name " << n.name() << '\n'; }
    void visit(const IntegerLiteral& n) override { indent() << "Integer " << n.value() << '\n'; }
    void visit(const FloatLiteral& n) override {
        indent() << "Float " << n.value() << (n.isF32() ? " (f32)" : "") << '\n';
    }
    void visit(const StringLiteral& n) override { indent() << "String \"" << escape(n.value()) << "\"\n"; }
    void visit(const InterpolatedString& n) override {
        indent() << "InterpolatedString (" << n.parts().size() << " parts)\n";
    }
    void visit(const BooleanLiteral& n) override { indent() << "Boolean " << (n.value() ? "true" : "false") << '\n'; }
    void visit(const UnaryExpr& n) override {
        indent() << "Unary " << unaryOpSpelling(n.op()) << '\n';
        child(n.operand());
    }
    void visit(const BinaryExpr& n) override {
        indent() << "Binary " << binaryOpSpelling(n.op()) << '\n';
        child(n.lhs());
        child(n.rhs());
    }
    void visit(const CastExpr& n) override {
        indent() << "Cast to " << typeToString(n.target()) << '\n';
        child(n.value());
    }
    void visit(const CallExpr& n) override {
        indent() << "Call\n";
        child(n.callee());
        auto names = n.paramNames();
        for (std::size_t i = 0; i < n.args().size(); ++i) {
            if (!names.empty() && !names[i].empty()) {
                ++depth_;
                indent() << names[i] << " =\n";
                child(*n.args()[i]);
                --depth_;
            } else {
                child(*n.args()[i]);
            }
        }
    }
    void visit(const ExprStmt& n) override {
        indent() << "ExprStmt\n";
        child(n.expr());
    }
    void visit(const DeclareVar& n) override {
        indent() << "DeclareVar " << n.name();
        if (n.type()) out_ << " : " << typeToString(*n.type());
        if (n.isStatic()) out_ << " static";
        if (n.isConst()) out_ << " const";
        out_ << '\n';
        if (n.init()) child(*n.init());
    }
    void visit(const FunctionDef& n) override {
        indent() << (n.isExtern() ? "ExternFunction " : "FunctionDef ") << n.name() << '(';
        bool first = true;
        for (const auto& param : n.params()) {
            if (!first) out_ << ", ";
            out_ << typeToString(*param.type) << ' ' << param.name;
            first = false;
        }
        out_ << ") -> " << typeToString(n.returnType()) << '\n';
        printBody(n.body());
    }
    void visit(const AssignStmt& n) override {
        indent() << "Assign " << assignOpSpelling(n.op()) << '\n';
        child(n.target());
        child(n.value());
    }
    void visit(const ReturnStmt& n) override {
        indent() << "Return\n";
        if (n.value()) child(*n.value());
    }
    void visit(const IfStmt& n) override {
        indent() << "If\n";
        child(n.condition());
        ++depth_;
        indent() << "Then\n";
        printBody(n.thenBody());
        if (!n.elseBody().empty()) {
            indent() << "Else\n";
            printBody(n.elseBody());
        }
        --depth_;
    }
    void visit(const WhileStmt& n) override {
        indent() << "While\n";
        child(n.condition());
        ++depth_;
        indent() << "Body\n";
        printBody(n.body());
        --depth_;
    }
    void visit(const BreakStmt&) override { indent() << "Break\n"; }

private:
    std::ostream& indent() {
        for (int i = 0; i < depth_; ++i) out_ << "  ";
        return out_;
    }
    void child(const Node& node) {
        ++depth_;
        node.accept(*this);
        --depth_;
    }

    std::ostream& out_;
    int depth_ = 0;
};

}  // namespace

void printAST(const Module& module, std::ostream& out) {
    out << "Module " << module.name << '\n';
    Printer printer(out);
    printer.printBody(module.body);
}

}  // namespace MemoLang
