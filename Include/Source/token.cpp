// ---------------------------------------------------------------------------
// token.cpp: names and keyword lookup for token kinds
//
// tokenkind.h lists every token exactly once, in MEMOLANG_TOKEN_LIST and
// MEMOLANG_KEYWORD_LIST (an "X-macro"). The TokenKind enum and both functions
// below are generated from those lists, so adding a token in one place updates
// all three.
//
// History: added 2026-10-14.
// ---------------------------------------------------------------------------
#include "Lex/tokenkind.h"

namespace MemoLang {

std::string_view tokenKindName(TokenKind kind) {
    switch (kind) {
#define MEMOLANG_TOKEN(name, spelling) \
    case TokenKind::name: return spelling;
        MEMOLANG_TOKEN_LIST(MEMOLANG_TOKEN)
        MEMOLANG_KEYWORD_LIST(MEMOLANG_TOKEN)
#undef MEMOLANG_TOKEN
    }
    return "invalid token";
}

TokenKind keywordKind(std::string_view spelling) {
#define MEMOLANG_TOKEN(name, text) \
    if (spelling == text) return TokenKind::name;
    MEMOLANG_KEYWORD_LIST(MEMOLANG_TOKEN)
#undef MEMOLANG_TOKEN

    // Built-in type names. The parser turns these into TypeExpr nodes.
    static constexpr std::string_view typeNames[] = {
        "int", "i32", "i64", "float", "f32", "f64", "bool", "str", "byte", "void", "matrix",
    };
    for (std::string_view type : typeNames)
        if (spelling == type) return TokenKind::TOK_KEYWORDTYPE;
    return TokenKind::TOK_IDENTIFIER;
}

}  // namespace MemoLang
