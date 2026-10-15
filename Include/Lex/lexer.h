#ifndef LEXER_H_
#define LEXER_H_
#pragma once
#include <cstdint>
#include <iosfwd>
#include <string_view>
#include <vector>
#include "Lex/token.h"

namespace MemoLang {
class DiagnosticEngine;
class SourceManager;

// Turns source text into tokens. Whitespace and `#` line comments are skipped.
// Errors (unknown characters, unterminated strings, bad numbers) are reported
// to the DiagnosticEngine and returned as UNKNOWN_TOKEN_SYNTAX_ERROR, so one
// run reports every problem instead of stopping at the first.
class Lexer {
    public:
    Lexer(const SourceManager& sources, DiagnosticEngine& diags);

    // Returns the next token. Keeps returning TOK_EOF once the input is used up.
    [[nodiscard]] Token next();
    // Lexes the remaining input. The last token is always TOK_EOF.
    [[nodiscard]] std::vector<Token> tokenize();

    private:
    [[nodiscard]] char peek(std::uint32_t ahead = 0) const noexcept;
    char advance() noexcept;
    bool match(char expected) noexcept;
    void skipWhitespaceAndComments();

    [[nodiscard]] SourceRange rangeFrom(std::uint32_t start) const;
    [[nodiscard]] Token makeToken(TokenKind kind, std::uint32_t start) const;
    [[nodiscard]] Token lexIdentifier(std::uint32_t start);
    [[nodiscard]] Token lexNumber(std::uint32_t start);
    [[nodiscard]] Token lexString(std::uint32_t start);
    [[nodiscard]] Token lexOperator(std::uint32_t start, char c);

    const SourceManager& sources_;
    DiagnosticEngine& diags_;
    std::string_view text_;
    std::uint32_t pos_ = 0;
};

// Prints one token per line: "line:col<TAB>kind[<TAB>'spelling']". Used by --dump-tokens.
void printTokens(const std::vector<Token>& tokens, std::ostream& out);
}

#endif // LEXER_H_
