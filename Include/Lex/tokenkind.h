#ifndef TOKENKIND_H_
#define TOKENKIND_H_
#pragma once
#include <string_view>

// Every token kind, as (enumerator, spelling). The spelling is what
// diagnostics show, e.g. "expected ')'". Keywords live in their own list so
// keywordKind() can look them up by spelling. Add new tokens here only; the
// enum and tokenKindName() are generated from these lists.
#define MEMOLANG_TOKEN_LIST(TOK)                       \
    /* Special */                                      \
    TOK(TOK_EOF, "end of file")                        \
    TOK(TOK_IDENTIFIER, "identifier")                  \
    TOK(TOK_INTEGER, "integer literal")                \
    TOK(TOK_FLOAT, "float literal")                    \
    TOK(TOK_STRING, "string literal")                  \
    TOK(TOK_KEYWORDTYPE, "type name")                  \
    TOK(UNKNOWN_TOKEN_SYNTAX_ERROR, "invalid token")   \
    /* Punctuation */                                  \
    TOK(TOK_LPAREN, "(")                               \
    TOK(TOK_RPAREN, ")")                               \
    TOK(TOK_LBRACE, "{")                               \
    TOK(TOK_RBRACE, "}")                               \
    TOK(TOK_LBRACKET, "[")                             \
    TOK(TOK_RBRACKET, "]")                             \
    TOK(TOK_COMMA, ",")                                \
    TOK(TOK_COLON, ":")                                \
    TOK(TOK_SEMICOLON, ";")                            \
    TOK(TOK_DOT, ".")                                  \
    TOK(TOK_ARROW, "->")                               \
    /* Operators */                                    \
    TOK(TOK_PLUS, "+")                                 \
    TOK(TOK_PLUSPLUSINC, "++")                         \
    TOK(TOK_PLUSEQUAL, "+=")                           \
    TOK(TOK_MINUS, "-")                                \
    TOK(TOK_MINUSMINUSDEC, "--")                       \
    TOK(TOK_MINUSEQUAL, "-=")                          \
    TOK(TOK_STARMULT, "*")                             \
    TOK(TOK_STARSTAR, "**")                            \
    TOK(TOK_STAREQUAL, "*=")                           \
    TOK(TOK_SLASH, "/")                                \
    TOK(TOK_SLASHSLASH, "//")                          \
    TOK(TOK_SLASHEQUAL, "/=")                          \
    TOK(TOK_PERCENT, "%")                              \
    TOK(TOK_PERCENTEQUAL, "%=")                        \
    TOK(TOK_AT, "@") /* matrix multiplication */       \
    TOK(TOK_ASSIGN, "=")                               \
    TOK(TOK_EQUALEQUAL, "==")                          \
    TOK(TOK_NOTEQUAL, "!=")                            \
    TOK(TOK_LESS, "<")                                 \
    TOK(TOK_LESSEQUAL, "<=")                           \
    TOK(TOK_GREATER, ">")                              \
    TOK(TOK_GREATEREQUAL, ">=")                        \
    TOK(TOK_SHL, "<<")                                 \
    TOK(TOK_SHR, ">>")                                 \
    TOK(TOK_AMP, "&")                                  \
    TOK(TOK_PIPE, "|")                                 \
    TOK(TOK_CARET, "^")                                \
    TOK(TOK_TILDE, "~")

#define MEMOLANG_KEYWORD_LIST(TOK) \
    TOK(TOK_DEFINE, "def")         \
    TOK(TOK_EXTERN, "extern")      \
    TOK(TOK_RETURN, "return")      \
    TOK(TOK_IF, "if")              \
    TOK(TOK_ELSE, "else")          \
    TOK(TOK_WHILE, "while")        \
    TOK(TOK_FOR, "for")            \
    TOK(TOK_BREAK, "break")        \
    TOK(TOK_CONST, "const")        \
    TOK(TOK_STATIC, "static")      \
    TOK(TOK_TRUE, "true")          \
    TOK(TOK_FALSE, "false")        \
    TOK(TOK_NONE, "none")          \
    TOK(TOK_IMPORT, "import")      \
    TOK(TOK_NOT, "not")            \
    TOK(TOK_AND, "and")            \
    TOK(TOK_OR, "or")              \
    TOK(TOK_IN, "in")              \
    TOK(TOK_IS, "is")              \
    TOK(TOK_AS, "as")              \
    TOK(TOK_TRY, "try")            \
    TOK(TOK_CATCH, "catch")        \
    TOK(TOK_THROW, "throw")        \
    TOK(TOK_CASE, "case")

namespace MemoLang {
    enum class TokenKind {
#define MEMOLANG_TOKEN(name, spelling) name,
        MEMOLANG_TOKEN_LIST(MEMOLANG_TOKEN)
        MEMOLANG_KEYWORD_LIST(MEMOLANG_TOKEN)
#undef MEMOLANG_TOKEN
    };

    [[nodiscard]] std::string_view tokenKindName(TokenKind kind);
    // Returns the keyword's kind, TOK_KEYWORDTYPE for a built-in type name
    // (int, float, matrix, ...), or TOK_IDENTIFIER for anything else.
    [[nodiscard]] TokenKind keywordKind(std::string_view spelling);
}

#endif // TOKENKIND_H_
