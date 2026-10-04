
#ifndef TOKEN_H_
#define TOKEN_H_
#pragma once
#include <memory>
#include <string>
#include <cstdint>
#include <string_view>
#include "Source/SourceLocation.h" // Dealing with source location later
#include "Lex/tokenkind.h"
namespace MemoLang {
class Token {
    public:
    Token(TokenKind Tok, SourceRange range, std::string spelling);
    [[nodiscard]] TokenKind kind() const;
    [[nodiscard]] SourceRange range() const;
    [[nodiscard]] SourceLocation location() const;
    [[nodiscard]] std::string_view spelling() const;

    private:
    
}
}
#endif // TOKEN_H_