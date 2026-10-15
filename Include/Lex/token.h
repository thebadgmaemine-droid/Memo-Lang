
#ifndef TOKEN_H_
#define TOKEN_H_
#pragma once
#include <string>
#include <string_view>
#include <utility>
#include "Source/SourceLocation.h"
#include "Lex/tokenkind.h"
namespace MemoLang {
class Token {
    public:
    Token() = default;
    Token(TokenKind kind, SourceRange range, std::string spelling)
        : kind_(kind), range_(range), spelling_(std::move(spelling)) {}

    [[nodiscard]] TokenKind kind() const noexcept { return kind_; }
    [[nodiscard]] SourceRange range() const noexcept { return range_; }
    [[nodiscard]] SourceLocation location() const noexcept { return range_.begin; }
    // The token's source text. For string literals it is the decoded value:
    // no quotes, escapes already applied.
    [[nodiscard]] std::string_view spelling() const noexcept { return spelling_; }

    [[nodiscard]] bool is(TokenKind kind) const noexcept { return kind_ == kind; }
    [[nodiscard]] bool isNot(TokenKind kind) const noexcept { return kind_ != kind; }

    private:
    TokenKind kind_ = TokenKind::TOK_EOF;
    SourceRange range_{};
    std::string spelling_;
};
}
#endif // TOKEN_H_
