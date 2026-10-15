#ifndef PARSER_H_
#define PARSER_H_
#pragma once
#include "AST/AST.h"
#include "Lex/token.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace MemoLang {
    class DiagnosticEngine;
    class Lexer;

    // Recursive-descent parser; binary operators use precedence climbing.
    // The grammar is written out in docs/STATUS.md.
    //
    // On a syntax error the parser reports it, skips to the next statement
    // boundary and keeps going, so one run reports as many errors as possible.
    // The returned Module then holds every statement that did parse.
    class Parser {
    public:
        // Each level of nesting (parentheses, blocks, prefix operators, type
        // arguments, ...) uses a few stack frames. Past this depth the parser
        // reports an error and stops parsing the file, instead of risking a
        // stack overflow (clang treats its bracket-depth limit the same way).
        static constexpr int kMaxNestingDepth = 256;

        Parser(Lexer& lexer, DiagnosticEngine& diags);

        [[nodiscard]] Module parseModule(std::string name);

    private:
        using StmtList = std::vector<std::unique_ptr<Statement>>;

        struct BinaryOpInfo {
            BinaryOp op;
            int precedence;
            int tokenCount;  // 2 for `is not` and `not in`.
        };

        // Token stream
        [[nodiscard]] const Token& peekNext();
        Token consume();
        [[nodiscard]] bool check(TokenKind kind) const noexcept { return tok_.is(kind); }
        bool accept(TokenKind kind);
        bool expect(TokenKind kind, std::string_view context);
        void error(SourceRange range, std::string message);
        bool tooDeep();

        // Error recovery
        void synchronize(std::uint32_t statementStart);
        void skipBalancedBraces();
        void skipUnsupported();

        // Statements
        void parseStatementInto(StmtList& out);
        std::unique_ptr<Statement> parseStatement();
        std::unique_ptr<Statement> parseFunctionDef(SourceLocation loc, bool isExtern);
        bool parseParams(std::vector<FunctionDef::Param>& params);
        std::unique_ptr<TypeExpr> parseType();
        bool parseBlock(StmtList& body);
        std::unique_ptr<Statement> parseVarDecl(SourceLocation loc, DeclFlags flags);
        std::unique_ptr<Statement> parseReturn();
        std::unique_ptr<Statement> parseIf();
        std::unique_ptr<Statement> parseWhile();
        std::unique_ptr<Statement> parseExprOrAssign();

        // Expressions
        std::unique_ptr<Expr> parseExpression(int minPrecedence = 1);
        std::optional<BinaryOpInfo> currentBinaryOp();
        std::unique_ptr<Expr> parseUnary();
        std::unique_ptr<Expr> parsePower();
        std::unique_ptr<Expr> parsePostfix();
        std::unique_ptr<Expr> parsePrimary();
        std::unique_ptr<Expr> parseCallArgs(std::unique_ptr<Expr> callee, SourceLocation loc);
        std::unique_ptr<Expr> parseIntegerLiteral(const Token& tok);
        std::unique_ptr<Expr> parseFloatLiteral(const Token& tok);

        Lexer& lexer_;
        DiagnosticEngine& diags_;
        Token tok_;                    // Current token.
        std::optional<Token> next_;    // One token of lookahead, filled by peekNext().
        SourceLocation prevEnd_{};     // End of the last consumed token.
        std::optional<std::uint32_t> lastErrorOffset_;
        int depth_ = 0;                // Current nesting depth; see kMaxNestingDepth.
        bool stopped_ = false;         // Set after a fatal error; the rest of the file reads as EOF.
    };
}

#endif // PARSER_H_
