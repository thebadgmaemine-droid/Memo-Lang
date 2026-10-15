// ---------------------------------------------------------------------------
// parser.cpp: stage 2 of the compiler pipeline
//
//   source text -> Lexer -> tokens -> [Parser] -> AST
//
// The parser reads tokens from the lexer and builds the abstract syntax tree
// (the node classes in AST.h). It is a recursive-descent parser: each grammar
// rule (statement, if, block, expression, ...) is one function, and the
// functions call each other the way the rules nest. The full grammar is
// written out in docs/STATUS.md.
//
// Binary operators use precedence climbing, the same idea as ParseBinOpRHS in
// the old Kaleidoscope parser. parseExpression(minPrecedence) reads one
// operand, then keeps taking operators whose precedence is at least
// minPrecedence. The right-hand side is parsed with (precedence + 1), so for
// `a + b * c`:
//   - read `a`; `+` (level 9) >= 1, so parse its right side with minimum 10
//       - read `b`; `*` (level 10) >= 10, so it binds here: b * c
//   - result: a + (b * c)
// For `a - b - c`, the second `-` (9) is below 10, so the inner call stops and
// the outer loop takes it: (a - b) - c. That is what left-associative means.
//
// Error recovery: when a statement is broken, the parser reports one error,
// then synchronize() skips to a likely statement boundary (`;`, `}`, or a
// keyword such as `return`) and carries on. One run finds every mistake
// without a cascade of follow-on errors; tests/errors/syntax.memo checks this.
//
// Nesting limit: each nested construct uses stack space. NestingGuard counts
// the depth; past Parser::kMaxNestingDepth (256) the parser reports one error
// and stops, instead of crashing with a stack overflow.
//
// History: written 2026-10-14, replacing the Kaleidoscope-style parser.h
// (git commit dae18ee). 2026-10-15: nesting limit and fatal stop (found by the
// stress tests); long text shortened in error messages.
// ---------------------------------------------------------------------------
#include "Parser/parser.h"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <utility>

#include "Diag/diagengine.h"
#include "Lex/lexer.h"

namespace MemoLang {
namespace {

// Binary operator precedence, lowest first. `not` sits between `and` and the
// comparisons, as in Python: `not a == b` means `not (a == b)`.
constexpr int kPrecOr = 1;
constexpr int kPrecAnd = 2;
constexpr int kPrecNot = 3;
constexpr int kPrecCompare = 4;
constexpr int kPrecBitOr = 5;
constexpr int kPrecBitXor = 6;
constexpr int kPrecBitAnd = 7;
constexpr int kPrecShift = 8;
constexpr int kPrecAdditive = 9;
constexpr int kPrecMultiplicative = 10;  // * / // % @

// Raises the parser's nesting depth for the lifetime of one recursive call.
class NestingGuard {
public:
    explicit NestingGuard(int& depth) noexcept : depth_(depth) { ++depth_; }
    ~NestingGuard() { --depth_; }
    NestingGuard(const NestingGuard&) = delete;
    NestingGuard& operator=(const NestingGuard&) = delete;

private:
    int& depth_;
};

// Creates a node and stamps it with a source location.
template <typename T, typename... Args>
std::unique_ptr<T> make(SourceLocation loc, Args&&... args) {
    auto node = std::make_unique<T>(std::forward<Args>(args)...);
    node->setLoc(SourceLoc{loc.line, loc.column});
    return node;
}

// How a token kind appears in "expected X" messages.
std::string describe(TokenKind kind) {
    switch (kind) {
    case TokenKind::TOK_EOF:
    case TokenKind::TOK_IDENTIFIER:
    case TokenKind::TOK_INTEGER:
    case TokenKind::TOK_FLOAT:
    case TokenKind::TOK_STRING:
    case TokenKind::TOK_KEYWORDTYPE:
    case TokenKind::UNKNOWN_TOKEN_SYNTAX_ERROR:
        return std::string(tokenKindName(kind));
    default:
        return "'" + std::string(tokenKindName(kind)) + "'";
    }
}

// Tokens that can only begin a statement. Error recovery stops in front of them.
bool startsStatement(TokenKind kind) {
    switch (kind) {
    case TokenKind::TOK_DEFINE:
    case TokenKind::TOK_EXTERN:
    case TokenKind::TOK_RETURN:
    case TokenKind::TOK_IF:
    case TokenKind::TOK_WHILE:
    case TokenKind::TOK_FOR:
    case TokenKind::TOK_BREAK:
    case TokenKind::TOK_CONST:
    case TokenKind::TOK_STATIC:
    case TokenKind::TOK_KEYWORDTYPE:
    case TokenKind::TOK_IMPORT:
    case TokenKind::TOK_TRY:
    case TokenKind::TOK_THROW:
    case TokenKind::TOK_CASE:
        return true;
    default:
        return false;
    }
}

std::optional<AssignOp> assignOp(TokenKind kind) {
    switch (kind) {
    case TokenKind::TOK_ASSIGN:       return AssignOp::Assign;
    case TokenKind::TOK_PLUSEQUAL:    return AssignOp::Add;
    case TokenKind::TOK_MINUSEQUAL:   return AssignOp::Sub;
    case TokenKind::TOK_STAREQUAL:    return AssignOp::Mul;
    case TokenKind::TOK_SLASHEQUAL:   return AssignOp::Div;
    case TokenKind::TOK_PERCENTEQUAL: return AssignOp::Mod;
    default:                          return std::nullopt;
    }
}

}  // namespace

Parser::Parser(Lexer& lexer, DiagnosticEngine& diags) : lexer_(lexer), diags_(diags), tok_(lexer.next()) {}

// ---------------------------------------------------------------------------
// Token stream
// ---------------------------------------------------------------------------
const Token& Parser::peekNext() {
    if (stopped_) return tok_;  // EOF.
    if (!next_) next_ = lexer_.next();
    return *next_;
}

Token Parser::consume() {
    if (stopped_) return tok_;  // Stay at EOF.
    Token previous = std::move(tok_);
    if (next_) {
        tok_ = std::move(*next_);
        next_.reset();
    } else {
        tok_ = lexer_.next();
    }
    prevEnd_ = previous.range().end;
    return previous;
}

bool Parser::accept(TokenKind kind) {
    if (!check(kind)) return false;
    consume();
    return true;
}

bool Parser::expect(TokenKind kind, std::string_view context) {
    if (accept(kind)) return true;
    if (check(TokenKind::UNKNOWN_TOKEN_SYNTAX_ERROR)) return false;  // The lexer already reported it.

    std::string message = "expected " + describe(kind);
    if (!context.empty()) {
        message += ' ';
        message += context;
    }
    // A missing ';' or ')' is easier to spot at the end of the previous line
    // than at the start of the next one.
    SourceRange where = tok_.range();
    if (prevEnd_.isValid() && prevEnd_.line != tok_.location().line) where = {prevEnd_, prevEnd_};
    error(where, std::move(message));
    return false;
}

void Parser::error(SourceRange range, std::string message) {
    if (stopped_) return;  // Errors while unwinding after a fatal error are noise.
    if (lastErrorOffset_ == range.begin.offset) return;  // One error per location is enough.
    lastErrorOffset_ = range.begin.offset;
    diags_.error(range, std::move(message));
}

// Called right after entering a NestingGuard. Past the limit this is a fatal
// error: the parser reports it once, then treats the rest of the file as EOF so
// every caller unwinds without follow-on errors. Recovering instead would just
// hit the limit again further into the same deep construct.
bool Parser::tooDeep() {
    if (stopped_) return true;
    if (depth_ <= kMaxNestingDepth) return false;
    error(tok_.range(), "nesting is too deep (the limit is " + std::to_string(kMaxNestingDepth) +
                            " levels); the rest of the file is not checked");
    stopped_ = true;
    tok_ = Token(TokenKind::TOK_EOF, tok_.range(), "");
    next_.reset();
    return true;
}

// ---------------------------------------------------------------------------
// Error recovery
// ---------------------------------------------------------------------------

// Skips to a likely statement boundary so one mistake doesn't produce a
// cascade of follow-on errors. Stops after a ';', before a '}', after a whole
// '{ ... }' group, or before a token that starts a new statement.
void Parser::synchronize(std::uint32_t statementStart) {
    bool moved = tok_.location().offset != statementStart;
    while (!check(TokenKind::TOK_EOF)) {
        if (accept(TokenKind::TOK_SEMICOLON)) return;
        if (check(TokenKind::TOK_RBRACE)) return;
        if (check(TokenKind::TOK_LBRACE)) {
            skipBalancedBraces();
            return;
        }
        if (moved && startsStatement(tok_.kind())) return;
        consume();
        moved = true;
    }
}

// Skips a `{ ... }` group, including nested groups. The current token must be '{'.
void Parser::skipBalancedBraces() {
    int depth = 0;
    do {
        if (check(TokenKind::TOK_LBRACE)) ++depth;
        else if (check(TokenKind::TOK_RBRACE)) --depth;
        consume();
    } while (depth > 0 && !check(TokenKind::TOK_EOF));
}

// Skips a statement the parser doesn't support yet (e.g. a `for` loop):
// everything up to a ';' outside parentheses, or up to the end of its first block.
void Parser::skipUnsupported() {
    consume();  // The keyword.
    int parens = 0;
    while (!check(TokenKind::TOK_EOF)) {
        if (check(TokenKind::TOK_LBRACE)) {
            skipBalancedBraces();
            return;
        }
        if (check(TokenKind::TOK_RBRACE)) return;
        if (check(TokenKind::TOK_SEMICOLON) && parens <= 0) {
            consume();
            return;
        }
        if (check(TokenKind::TOK_LPAREN)) ++parens;
        else if (check(TokenKind::TOK_RPAREN)) --parens;
        consume();
    }
}

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------

// module := { statement }
Module Parser::parseModule(std::string name) {
    Module module;
    module.name = std::move(name);
    while (!check(TokenKind::TOK_EOF)) {
        if (check(TokenKind::TOK_RBRACE)) {
            error(tok_.range(), "unexpected '}' with no matching '{'");
            consume();
            continue;
        }
        parseStatementInto(module.body);
    }
    return module;
}

// Parses one statement and appends it to `out`. Constructs that produce no
// node (an empty `;`, unsupported statements, errors) append nothing.
void Parser::parseStatementInto(StmtList& out) {
    std::uint32_t start = tok_.location().offset;
    switch (tok_.kind()) {
    case TokenKind::TOK_SEMICOLON:
        consume();  // Empty statement.
        return;
    case TokenKind::TOK_LBRACE:
        error(tok_.range(), "a block can't stand on its own; it must follow 'def', 'if', 'else' or 'while'");
        skipBalancedBraces();
        return;
    case TokenKind::TOK_ELSE:
        error(tok_.range(), "'else' without a matching 'if'");
        consume();
        if (check(TokenKind::TOK_LBRACE)) skipBalancedBraces();
        return;
    case TokenKind::TOK_FOR:
    case TokenKind::TOK_IMPORT:
    case TokenKind::TOK_TRY:
    case TokenKind::TOK_CATCH:
    case TokenKind::TOK_THROW:
    case TokenKind::TOK_CASE:
        error(tok_.range(), "'" + std::string(tok_.spelling()) + "' is not supported yet");
        skipUnsupported();
        return;
    default:
        break;
    }
    if (auto stmt = parseStatement()) out.push_back(std::move(stmt));
    else synchronize(start);
}

// statement := function_def | extern_def | var_decl | return_stmt | if_stmt
//            | while_stmt | 'break' ';' | expr_or_assign
std::unique_ptr<Statement> Parser::parseStatement() {
    NestingGuard guard(depth_);
    if (tooDeep()) return nullptr;
    SourceLocation loc = tok_.location();
    switch (tok_.kind()) {
    case TokenKind::TOK_DEFINE:
        return parseFunctionDef(loc, false);
    case TokenKind::TOK_EXTERN:
        consume();
        if (!check(TokenKind::TOK_DEFINE)) {
            error(tok_.range(), "expected 'def' after 'extern'");
            return nullptr;
        }
        return parseFunctionDef(loc, true);
    case TokenKind::TOK_RETURN:
        return parseReturn();
    case TokenKind::TOK_IF:
        return parseIf();
    case TokenKind::TOK_WHILE:
        return parseWhile();
    case TokenKind::TOK_BREAK:
        consume();
        if (!expect(TokenKind::TOK_SEMICOLON, "after 'break'")) return nullptr;
        return make<BreakStmt>(loc);
    case TokenKind::TOK_STATIC: {
        consume();
        DeclFlags flags = DeclFlags::Static;
        if (accept(TokenKind::TOK_CONST)) flags = flags | DeclFlags::Const;
        return parseVarDecl(loc, flags);
    }
    case TokenKind::TOK_CONST:
        consume();
        return parseVarDecl(loc, DeclFlags::Const);
    case TokenKind::TOK_KEYWORDTYPE:
        // `float(x);` is a call-style cast, not a declaration.
        if (peekNext().is(TokenKind::TOK_LPAREN)) return parseExprOrAssign();
        return parseVarDecl(loc, DeclFlags::None);
    default:
        return parseExprOrAssign();
    }
}

// function_def := 'def' IDENT '(' params ')' [ '->' type ] block
// extern_def   := 'extern' 'def' IDENT '(' params ')' [ '->' type ] ';'
// Without '->', a def's return type is "auto" (inferred later by semantic
// analysis) and an extern's is "void".
std::unique_ptr<Statement> Parser::parseFunctionDef(SourceLocation loc, bool isExtern) {
    consume();  // 'def'
    if (!check(TokenKind::TOK_IDENTIFIER)) {
        error(tok_.range(), "expected function name after 'def'");
        return nullptr;
    }
    std::string name(consume().spelling());
    if (!expect(TokenKind::TOK_LPAREN, "after function name")) return nullptr;

    std::vector<FunctionDef::Param> params;
    if (!parseParams(params)) return nullptr;

    std::unique_ptr<TypeExpr> returnType;
    if (accept(TokenKind::TOK_ARROW)) {
        returnType = parseType();
        if (!returnType) return nullptr;
    } else {
        returnType = make<TypeExpr>(loc, isExtern ? "void" : "auto");
    }

    StmtList body;
    if (isExtern) {
        if (!expect(TokenKind::TOK_SEMICOLON, "after extern declaration")) return nullptr;
    } else if (!parseBlock(body)) {
        return nullptr;
    }

    std::string externName = isExtern ? name : std::string();
    return make<FunctionDef>(loc, std::move(name), std::move(params), std::move(returnType), std::move(body),
                             std::move(externName));
}

// params := [ type IDENT { ',' type IDENT } ] ')'
bool Parser::parseParams(std::vector<FunctionDef::Param>& params) {
    if (accept(TokenKind::TOK_RPAREN)) return true;
    do {
        auto type = parseType();
        if (!type) return false;
        if (!check(TokenKind::TOK_IDENTIFIER)) {
            error(tok_.range(), "expected parameter name after type");
            return false;
        }
        params.push_back({std::string(consume().spelling()), std::move(type)});
    } while (accept(TokenKind::TOK_COMMA));
    return expect(TokenKind::TOK_RPAREN, "after parameter list");
}

// type := TYPE_NAME [ '<' type { ',' type } '>' ]      e.g. int, matrix<float>
std::unique_ptr<TypeExpr> Parser::parseType() {
    NestingGuard guard(depth_);
    if (tooDeep()) return nullptr;
    if (!check(TokenKind::TOK_KEYWORDTYPE)) {
        if (check(TokenKind::TOK_IDENTIFIER))
            error(tok_.range(), "unknown type name " + quoteForMessage(tok_.spelling()));
        else
            error(tok_.range(), "expected a type name");
        return nullptr;
    }
    Token nameTok = consume();
    std::vector<std::unique_ptr<TypeExpr>> args;
    if (accept(TokenKind::TOK_LESS)) {
        do {
            auto arg = parseType();
            if (!arg) return nullptr;
            args.push_back(std::move(arg));
        } while (accept(TokenKind::TOK_COMMA));

        if (check(TokenKind::TOK_SHR)) {
            // `matrix<matrix<float>>`: the lexer reads `>>` as one token. Use up
            // the first '>' here and leave the second for the outer type.
            SourceRange rest = tok_.range();
            rest.begin.offset += 1;
            rest.begin.column += 1;
            tok_ = Token(TokenKind::TOK_GREATER, rest, ">");
        } else if (!expect(TokenKind::TOK_GREATER, "to close the type arguments")) {
            return nullptr;
        }
    }
    return make<TypeExpr>(nameTok.location(), std::string(nameTok.spelling()), std::move(args));
}

// block := '{' { statement } '}'
bool Parser::parseBlock(StmtList& body) {
    if (!expect(TokenKind::TOK_LBRACE, "to start a block")) return false;
    while (!check(TokenKind::TOK_RBRACE) && !check(TokenKind::TOK_EOF)) parseStatementInto(body);
    return expect(TokenKind::TOK_RBRACE, "to close the block");
}

// var_decl := [ 'static' ] [ 'const' ] [ type ] IDENT [ '=' expr ] ';'
// The type may be left out only when there is an initializer: `const n = 4;`
std::unique_ptr<Statement> Parser::parseVarDecl(SourceLocation loc, DeclFlags flags) {
    std::unique_ptr<TypeExpr> type;
    if (check(TokenKind::TOK_KEYWORDTYPE)) {
        type = parseType();
        if (!type) return nullptr;
    }
    if (!check(TokenKind::TOK_IDENTIFIER)) {
        error(tok_.range(), "expected variable name");
        return nullptr;
    }
    Token nameTok = consume();

    std::unique_ptr<Expr> init;
    if (accept(TokenKind::TOK_ASSIGN)) {
        init = parseExpression();
        if (!init) return nullptr;
    }
    if (!type && !init) {
        error(nameTok.range(), "variable " + quoteForMessage(nameTok.spelling()) + " needs a type or an initial value");
        return nullptr;
    }
    if (!expect(TokenKind::TOK_SEMICOLON, "after variable declaration")) return nullptr;
    return make<DeclareVar>(loc, std::string(nameTok.spelling()), std::move(type), std::move(init), flags);
}

// return_stmt := 'return' [ expr ] ';'
std::unique_ptr<Statement> Parser::parseReturn() {
    SourceLocation loc = consume().location();
    std::unique_ptr<Expr> value;
    if (!check(TokenKind::TOK_SEMICOLON)) {
        value = parseExpression();
        if (!value) return nullptr;
    }
    if (!expect(TokenKind::TOK_SEMICOLON, "after return statement")) return nullptr;
    return make<ReturnStmt>(loc, std::move(value));
}

// if_stmt := 'if' expr block [ 'else' ( if_stmt | block ) ]
// Parentheses around the condition are optional: they parse as a normal
// parenthesized expression.
std::unique_ptr<Statement> Parser::parseIf() {
    NestingGuard guard(depth_);  // `else if` chains recurse here directly.
    if (tooDeep()) return nullptr;
    SourceLocation loc = consume().location();
    auto condition = parseExpression();
    if (!condition) return nullptr;

    StmtList thenBody;
    StmtList elseBody;
    if (!parseBlock(thenBody)) return nullptr;
    if (accept(TokenKind::TOK_ELSE)) {
        if (check(TokenKind::TOK_IF)) {
            auto elseIf = parseIf();
            if (!elseIf) return nullptr;
            elseBody.push_back(std::move(elseIf));
        } else if (!parseBlock(elseBody)) {
            return nullptr;
        }
    }
    return make<IfStmt>(loc, std::move(condition), std::move(thenBody), std::move(elseBody));
}

// while_stmt := 'while' expr block
std::unique_ptr<Statement> Parser::parseWhile() {
    SourceLocation loc = consume().location();
    auto condition = parseExpression();
    if (!condition) return nullptr;
    StmtList body;
    if (!parseBlock(body)) return nullptr;
    return make<WhileStmt>(loc, std::move(condition), std::move(body));
}

// expr_or_assign := expr [ assign_op expr ] ';'
// assign_op      := '=' | '+=' | '-=' | '*=' | '/=' | '%='
std::unique_ptr<Statement> Parser::parseExprOrAssign() {
    SourceLocation loc = tok_.location();
    auto expr = parseExpression();
    if (!expr) return nullptr;

    if (auto op = assignOp(tok_.kind())) {
        Token opTok = consume();
        if (expr->kind() != NodeKind::NameExpr) {
            error(opTok.range(), "left side of '" + std::string(opTok.spelling()) + "' must be a variable name");
            return nullptr;
        }
        auto value = parseExpression();
        if (!value) return nullptr;
        if (!expect(TokenKind::TOK_SEMICOLON, "after assignment")) return nullptr;
        return make<AssignStmt>(loc, *op, std::move(expr), std::move(value));
    }
    if (!expect(TokenKind::TOK_SEMICOLON, "after expression")) return nullptr;
    return make<ExprStmt>(loc, std::move(expr));
}

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

// expr := 'not' expr | unary { binary_op unary }     (precedence climbing)
std::unique_ptr<Expr> Parser::parseExpression(int minPrecedence) {
    NestingGuard guard(depth_);
    if (tooDeep()) return nullptr;
    std::unique_ptr<Expr> lhs;
    if (check(TokenKind::TOK_NOT) && minPrecedence <= kPrecNot) {
        SourceLocation loc = consume().location();
        auto operand = parseExpression(kPrecNot);
        if (!operand) return nullptr;
        lhs = make<UnaryExpr>(loc, UnaryOp::Not, std::move(operand));
    } else {
        lhs = parseUnary();
        if (!lhs) return nullptr;
    }

    while (auto info = currentBinaryOp()) {
        if (info->precedence < minPrecedence) break;
        SourceLocation loc = tok_.location();
        for (int i = 0; i < info->tokenCount; ++i) consume();
        auto rhs = parseExpression(info->precedence + 1);  // +1: left-associative.
        if (!rhs) return nullptr;
        lhs = make<BinaryExpr>(loc, info->op, std::move(lhs), std::move(rhs));
    }
    return lhs;
}

std::optional<Parser::BinaryOpInfo> Parser::currentBinaryOp() {
    switch (tok_.kind()) {
    case TokenKind::TOK_OR:           return BinaryOpInfo{BinaryOp::Or, kPrecOr, 1};
    case TokenKind::TOK_AND:          return BinaryOpInfo{BinaryOp::And, kPrecAnd, 1};
    case TokenKind::TOK_EQUALEQUAL:   return BinaryOpInfo{BinaryOp::Eq, kPrecCompare, 1};
    case TokenKind::TOK_NOTEQUAL:     return BinaryOpInfo{BinaryOp::Ne, kPrecCompare, 1};
    case TokenKind::TOK_LESS:         return BinaryOpInfo{BinaryOp::Lt, kPrecCompare, 1};
    case TokenKind::TOK_LESSEQUAL:    return BinaryOpInfo{BinaryOp::Le, kPrecCompare, 1};
    case TokenKind::TOK_GREATER:      return BinaryOpInfo{BinaryOp::Gt, kPrecCompare, 1};
    case TokenKind::TOK_GREATEREQUAL: return BinaryOpInfo{BinaryOp::Ge, kPrecCompare, 1};
    case TokenKind::TOK_IN:           return BinaryOpInfo{BinaryOp::In, kPrecCompare, 1};
    case TokenKind::TOK_IS:
        if (peekNext().is(TokenKind::TOK_NOT)) return BinaryOpInfo{BinaryOp::IsNot, kPrecCompare, 2};
        return BinaryOpInfo{BinaryOp::Is, kPrecCompare, 1};
    case TokenKind::TOK_NOT:
        if (peekNext().is(TokenKind::TOK_IN)) return BinaryOpInfo{BinaryOp::NotIn, kPrecCompare, 2};
        return std::nullopt;
    case TokenKind::TOK_PIPE:         return BinaryOpInfo{BinaryOp::BitOr, kPrecBitOr, 1};
    case TokenKind::TOK_CARET:        return BinaryOpInfo{BinaryOp::BitXor, kPrecBitXor, 1};
    case TokenKind::TOK_AMP:          return BinaryOpInfo{BinaryOp::BitAnd, kPrecBitAnd, 1};
    case TokenKind::TOK_SHL:          return BinaryOpInfo{BinaryOp::Shl, kPrecShift, 1};
    case TokenKind::TOK_SHR:          return BinaryOpInfo{BinaryOp::Shr, kPrecShift, 1};
    case TokenKind::TOK_PLUS:         return BinaryOpInfo{BinaryOp::Add, kPrecAdditive, 1};
    case TokenKind::TOK_MINUS:        return BinaryOpInfo{BinaryOp::Sub, kPrecAdditive, 1};
    case TokenKind::TOK_STARMULT:     return BinaryOpInfo{BinaryOp::Mul, kPrecMultiplicative, 1};
    case TokenKind::TOK_SLASH:        return BinaryOpInfo{BinaryOp::Div, kPrecMultiplicative, 1};
    case TokenKind::TOK_SLASHSLASH:   return BinaryOpInfo{BinaryOp::FloorDiv, kPrecMultiplicative, 1};
    case TokenKind::TOK_PERCENT:      return BinaryOpInfo{BinaryOp::Mod, kPrecMultiplicative, 1};
    case TokenKind::TOK_AT:           return BinaryOpInfo{BinaryOp::MatMul, kPrecMultiplicative, 1};
    default:                          return std::nullopt;
    }
}

// unary := ( '-' | '+' | '~' | '++' | '--' ) unary | power
std::unique_ptr<Expr> Parser::parseUnary() {
    SourceLocation loc = tok_.location();
    UnaryOp op;
    switch (tok_.kind()) {
    case TokenKind::TOK_MINUS:
        consume();
        // -9223372036854775808 fits in int64 but 9223372036854775808 doesn't,
        // so this one literal has to be negated while parsing.
        if (check(TokenKind::TOK_INTEGER) && tok_.spelling() == "9223372036854775808") {
            consume();
            return make<IntegerLiteral>(loc, std::numeric_limits<std::int64_t>::min());
        }
        op = UnaryOp::Neg;
        break;
    case TokenKind::TOK_PLUS: {
        consume();
        NestingGuard guard(depth_);
        if (tooDeep()) return nullptr;
        return parseUnary();  // Unary plus does nothing.
    }
    case TokenKind::TOK_TILDE:         consume(); op = UnaryOp::Inv; break;
    case TokenKind::TOK_PLUSPLUSINC:   consume(); op = UnaryOp::PreInc; break;
    case TokenKind::TOK_MINUSMINUSDEC: consume(); op = UnaryOp::PreDec; break;
    default:
        return parsePower();
    }
    NestingGuard guard(depth_);
    if (tooDeep()) return nullptr;
    auto operand = parseUnary();
    if (!operand) return nullptr;
    return make<UnaryExpr>(loc, op, std::move(operand));
}

// power := postfix [ '**' unary ]      Right-associative; -2**2 is -(2**2).
std::unique_ptr<Expr> Parser::parsePower() {
    auto base = parsePostfix();
    if (!base || !check(TokenKind::TOK_STARSTAR)) return base;
    SourceLocation loc = consume().location();
    NestingGuard guard(depth_);  // `a ** b ** c ...` recurses through the exponent.
    if (tooDeep()) return nullptr;
    auto exponent = parseUnary();
    if (!exponent) return nullptr;
    return make<BinaryExpr>(loc, BinaryOp::Pow, std::move(base), std::move(exponent));
}

// postfix := primary { '(' args ')' | 'as' type | '++' | '--' }
std::unique_ptr<Expr> Parser::parsePostfix() {
    auto expr = parsePrimary();
    while (expr) {
        SourceLocation loc = tok_.location();
        if (accept(TokenKind::TOK_LPAREN)) {
            expr = parseCallArgs(std::move(expr), loc);
        } else if (accept(TokenKind::TOK_AS)) {
            auto target = parseType();
            if (!target) return nullptr;
            expr = make<CastExpr>(loc, std::move(expr), std::move(target));
        } else if (accept(TokenKind::TOK_PLUSPLUSINC)) {
            expr = make<UnaryExpr>(loc, UnaryOp::PostInc, std::move(expr));
        } else if (accept(TokenKind::TOK_MINUSMINUSDEC)) {
            expr = make<UnaryExpr>(loc, UnaryOp::PostDec, std::move(expr));
        } else {
            break;
        }
    }
    return expr;
}

// args := [ arg { ',' arg } ] ')'      arg := [ IDENT '=' ] expr
// Named arguments must come after all positional ones.
std::unique_ptr<Expr> Parser::parseCallArgs(std::unique_ptr<Expr> callee, SourceLocation loc) {
    std::vector<std::unique_ptr<Expr>> args;
    std::vector<std::string> names;
    bool anyNamed = false;
    if (!check(TokenKind::TOK_RPAREN)) {
        do {
            std::string name;
            if (check(TokenKind::TOK_IDENTIFIER) && peekNext().is(TokenKind::TOK_ASSIGN)) {
                name = std::string(consume().spelling());
                consume();  // '='
                anyNamed = true;
            } else if (anyNamed) {
                error(tok_.range(), "positional argument can't follow a named argument");
                return nullptr;
            }
            auto arg = parseExpression();
            if (!arg) return nullptr;
            args.push_back(std::move(arg));
            names.push_back(std::move(name));
        } while (accept(TokenKind::TOK_COMMA));
    }
    if (!expect(TokenKind::TOK_RPAREN, "after call arguments")) return nullptr;
    if (!anyNamed) names.clear();
    return make<CallExpr>(loc, std::move(callee), std::move(args), std::vector<std::unique_ptr<TypeExpr>>{},
                          std::move(names));
}

// primary := INTEGER | FLOAT | STRING | 'true' | 'false' | IDENT
//          | '(' expr ')' | type '(' expr ')'
std::unique_ptr<Expr> Parser::parsePrimary() {
    SourceLocation loc = tok_.location();
    switch (tok_.kind()) {
    case TokenKind::TOK_INTEGER:
        return parseIntegerLiteral(consume());
    case TokenKind::TOK_FLOAT:
        return parseFloatLiteral(consume());
    case TokenKind::TOK_STRING:
        return make<StringLiteral>(loc, std::string(consume().spelling()));
    case TokenKind::TOK_TRUE:
        consume();
        return make<BooleanLiteral>(loc, true);
    case TokenKind::TOK_FALSE:
        consume();
        return make<BooleanLiteral>(loc, false);
    case TokenKind::TOK_IDENTIFIER:
        return make<NameExpr>(loc, std::string(consume().spelling()));
    case TokenKind::TOK_LPAREN: {
        consume();
        auto inner = parseExpression();
        if (!inner || !expect(TokenKind::TOK_RPAREN, "to close the parenthesized expression")) return nullptr;
        return inner;
    }
    case TokenKind::TOK_KEYWORDTYPE: {
        // Call-style cast: `float(x)` means `x as float`.
        auto target = parseType();
        if (!target) return nullptr;
        if (!expect(TokenKind::TOK_LPAREN, "after type name in a cast")) return nullptr;
        auto value = parseExpression();
        if (!value || !expect(TokenKind::TOK_RPAREN, "after the value being cast")) return nullptr;
        return make<CastExpr>(loc, std::move(value), std::move(target));
    }
    case TokenKind::TOK_NONE:
        error(tok_.range(), "'none' is not supported yet");
        consume();
        return nullptr;
    case TokenKind::UNKNOWN_TOKEN_SYNTAX_ERROR:
        consume();  // The lexer already reported it.
        return nullptr;
    default:
        error(tok_.range(), "expected an expression");
        return nullptr;
    }
}

std::unique_ptr<Expr> Parser::parseIntegerLiteral(const Token& tok) {
    std::string_view text = tok.spelling();
    int base = 10;
    if (text.size() > 2 && text[0] == '0') {
        if (text[1] == 'x' || text[1] == 'X') base = 16;
        else if (text[1] == 'b' || text[1] == 'B') base = 2;
        if (base != 10) text.remove_prefix(2);
    }
    std::uint64_t value = 0;
    auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value, base);
    if (ec != std::errc() || end != text.data() + text.size() ||
        value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        error(tok.range(), "integer literal " + quoteForMessage(tok.spelling()) + " is too large for int64");
        return nullptr;
    }
    return make<IntegerLiteral>(tok.location(), static_cast<std::int64_t>(value));
}

std::unique_ptr<Expr> Parser::parseFloatLiteral(const Token& tok) {
    std::string text(tok.spelling());
    bool isF32 = !text.empty() && text.back() == 'f';
    if (isF32) text.pop_back();
    char* end = nullptr;
    double value = std::strtod(text.c_str(), &end);
    if (end != text.c_str() + text.size() || std::isinf(value)) {
        error(tok.range(), "floating-point literal " + quoteForMessage(tok.spelling()) + " is out of range");
        return nullptr;
    }
    return make<FloatLiteral>(tok.location(), value, isF32);
}

}  // namespace MemoLang
