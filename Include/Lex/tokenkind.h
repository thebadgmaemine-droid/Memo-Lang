#ifndef TOKENKIND_H_
#define TOKENKIND_H_
#pragma once
#include <string_view>

namespace MemoLang {
    enum class TokenKind {
        TOK_EOF,
        TOK_NEWLINE,
        TOK_INDENTATION,
        TOK_IDENTIFIER,
        TOK_INTEGER,
        TOK_FLOAT,
        TOK_STRING,
        TOK_BYTES,
        TOK_REGEX,
        TOK_COMMENT,
        TOK_PLUS,
        TOK_PLUSPLUSINC,
        TOK_PLUSEQUAL,
        TOK_DEFINE,
        TOK_MINUS,
        TOK_MINUSMINUSDEC,
        TOK_MINUSEQUAL,
        TOK_STARMULT,
        TOK_STARSTAR,
        TOK_STAREQUAL,
        TOK_PERCENTDIVIDE,
        TOK_PERCENTEQUAL,
        TOK_SEMICOLON,
        TOK_FALSE,
        TOK_TRUE,
        TOK_NONE,
        TOK_CONST,
        TOK_BREAK,
        TOK_RETURN,
        TOK_FOR,
        TOK_WHILE,
        TOK_ELSE,
        TOK_CASE,
        TOK_IF,
        TOK_IMPORT,
        TOK_NOT,
        TOK_TRY,
        TOK_CATCH,
        TOK_THROW,
        TOK_KEYWORDTYPE,
        UNKNOWN_TOKEN_SYNTAX_ERROR,


    };
    
    [[nodiscard]] std::string_view tokenKindName(TokenKind kind);
    [[nodiscard]] TokenKind keywordKind(std::string_view spelling);
}

#endif TOKENKIND_H_