#include "Lex/lexer.h"

#include <cctype>
#include <ostream>
#include <string>

#include "Diag/diagengine.h"
#include "Source/SourceManager.h"

namespace MemoLang {
namespace {

bool isIdentStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool isIdentChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }
bool isDigit(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }
bool isHexDigit(char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; }

}  // namespace

Lexer::Lexer(const SourceManager& sources, DiagnosticEngine& diags)
    : sources_(sources), diags_(diags), text_(sources.text()) {}

char Lexer::peek(std::uint32_t ahead) const noexcept {
    std::size_t i = static_cast<std::size_t>(pos_) + ahead;
    return i < text_.size() ? text_[i] : '\0';
}

char Lexer::advance() noexcept { return pos_ < text_.size() ? text_[pos_++] : '\0'; }

bool Lexer::match(char expected) noexcept {
    if (pos_ >= text_.size() || text_[pos_] != expected) return false;
    ++pos_;
    return true;
}

void Lexer::skipWhitespaceAndComments() {
    while (pos_ < text_.size()) {
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v') {
            ++pos_;
        } else if (c == '#') {  // Line comment. (`//` is floor division, not a comment.)
            while (pos_ < text_.size() && peek() != '\n') ++pos_;
        } else {
            return;
        }
    }
}

SourceRange Lexer::rangeFrom(std::uint32_t start) const {
    return {sources_.locationAt(start), sources_.locationAt(pos_)};
}

Token Lexer::makeToken(TokenKind kind, std::uint32_t start) const {
    return Token(kind, rangeFrom(start), std::string(text_.substr(start, pos_ - start)));
}

Token Lexer::next() {
    skipWhitespaceAndComments();
    std::uint32_t start = pos_;
    if (pos_ >= text_.size()) return makeToken(TokenKind::TOK_EOF, start);

    char c = advance();
    if (isIdentStart(c)) return lexIdentifier(start);
    if (isDigit(c) || (c == '.' && isDigit(peek()))) return lexNumber(start);
    if (c == '"') return lexString(start);
    return lexOperator(start, c);
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    do {
        tokens.push_back(next());
    } while (tokens.back().isNot(TokenKind::TOK_EOF));
    return tokens;
}

Token Lexer::lexIdentifier(std::uint32_t start) {
    while (isIdentChar(peek())) ++pos_;
    return makeToken(keywordKind(text_.substr(start, pos_ - start)), start);
}

// number := digits [ '.' digits ] [ ('e'|'E') ['+'|'-'] digits ] [ 'f' ]
//         | '.' digits ...  | '0x' hexdigits | '0b' bindigits
// The 'f' suffix marks an f32 literal, e.g. 1.5f.
Token Lexer::lexNumber(std::uint32_t start) {
    char first = text_[start];
    if (first == '0' && (peek() == 'x' || peek() == 'X' || peek() == 'b' || peek() == 'B')) {
        bool hex = peek() == 'x' || peek() == 'X';
        ++pos_;
        std::uint32_t digitsStart = pos_;
        while (hex ? isHexDigit(peek()) : (peek() == '0' || peek() == '1')) ++pos_;
        if (pos_ == digitsStart || isIdentChar(peek())) {
            while (isIdentChar(peek())) ++pos_;
            diags_.error(rangeFrom(start), hex ? "invalid hexadecimal literal" : "invalid binary literal");
            return makeToken(TokenKind::UNKNOWN_TOKEN_SYNTAX_ERROR, start);
        }
        return makeToken(TokenKind::TOK_INTEGER, start);
    }

    bool isFloat = first == '.';
    while (isDigit(peek())) ++pos_;
    if (!isFloat && peek() == '.' && isDigit(peek(1))) {
        isFloat = true;
        ++pos_;
        while (isDigit(peek())) ++pos_;
    }
    if (peek() == 'e' || peek() == 'E') {
        std::uint32_t beforeExponent = pos_;
        ++pos_;
        if (peek() == '+' || peek() == '-') ++pos_;
        if (isDigit(peek())) {
            isFloat = true;
            while (isDigit(peek())) ++pos_;
        } else {
            pos_ = beforeExponent;  // Not an exponent; caught as a bad suffix below.
        }
    }
    if (peek() == 'f' && !isIdentChar(peek(1))) {
        ++pos_;
        isFloat = true;
    }
    if (isIdentChar(peek())) {  // e.g. `12abc`
        while (isIdentChar(peek())) ++pos_;
        diags_.error(rangeFrom(start),
                     "invalid suffix on numeric literal " + quoteForMessage(text_.substr(start, pos_ - start)));
        return makeToken(TokenKind::UNKNOWN_TOKEN_SYNTAX_ERROR, start);
    }
    return makeToken(isFloat ? TokenKind::TOK_FLOAT : TokenKind::TOK_INTEGER, start);
}

// string := '"' { char | escape } '"'    escape := \n \t \r \0 \\ \"
// Strings may not span lines.
Token Lexer::lexString(std::uint32_t start) {
    std::string value;
    while (true) {
        if (pos_ >= text_.size() || peek() == '\n') {
            diags_.error(rangeFrom(start), "unterminated string literal");
            return Token(TokenKind::UNKNOWN_TOKEN_SYNTAX_ERROR, rangeFrom(start), std::move(value));
        }
        char c = advance();
        if (c == '"') break;
        if (c != '\\') {
            value += c;
            continue;
        }
        if (pos_ >= text_.size() || peek() == '\n') continue;  // Reported as unterminated above.
        std::uint32_t escapeStart = pos_ - 1;
        char e = advance();
        switch (e) {
        case 'n': value += '\n'; break;
        case 't': value += '\t'; break;
        case 'r': value += '\r'; break;
        case '0': value += '\0'; break;
        case '\\': value += '\\'; break;
        case '"': value += '"'; break;
        default:
            diags_.error(rangeFrom(escapeStart), std::string("unknown escape sequence '\\") + e + "'");
            value += e;
        }
    }
    return Token(TokenKind::TOK_STRING, rangeFrom(start), std::move(value));
}

Token Lexer::lexOperator(std::uint32_t start, char c) {
    using K = TokenKind;
    K kind;
    switch (c) {
    case '(': kind = K::TOK_LPAREN; break;
    case ')': kind = K::TOK_RPAREN; break;
    case '{': kind = K::TOK_LBRACE; break;
    case '}': kind = K::TOK_RBRACE; break;
    case '[': kind = K::TOK_LBRACKET; break;
    case ']': kind = K::TOK_RBRACKET; break;
    case ',': kind = K::TOK_COMMA; break;
    case ':': kind = K::TOK_COLON; break;
    case ';': kind = K::TOK_SEMICOLON; break;
    case '.': kind = K::TOK_DOT; break;
    case '@': kind = K::TOK_AT; break;
    case '&': kind = K::TOK_AMP; break;
    case '|': kind = K::TOK_PIPE; break;
    case '^': kind = K::TOK_CARET; break;
    case '~': kind = K::TOK_TILDE; break;
    case '+': kind = match('+') ? K::TOK_PLUSPLUSINC : match('=') ? K::TOK_PLUSEQUAL : K::TOK_PLUS; break;
    case '-':
        kind = match('-') ? K::TOK_MINUSMINUSDEC
             : match('=') ? K::TOK_MINUSEQUAL
             : match('>') ? K::TOK_ARROW
                          : K::TOK_MINUS;
        break;
    case '*': kind = match('*') ? K::TOK_STARSTAR : match('=') ? K::TOK_STAREQUAL : K::TOK_STARMULT; break;
    case '/': kind = match('/') ? K::TOK_SLASHSLASH : match('=') ? K::TOK_SLASHEQUAL : K::TOK_SLASH; break;
    case '%': kind = match('=') ? K::TOK_PERCENTEQUAL : K::TOK_PERCENT; break;
    case '=': kind = match('=') ? K::TOK_EQUALEQUAL : K::TOK_ASSIGN; break;
    case '<': kind = match('<') ? K::TOK_SHL : match('=') ? K::TOK_LESSEQUAL : K::TOK_LESS; break;
    case '>': kind = match('>') ? K::TOK_SHR : match('=') ? K::TOK_GREATEREQUAL : K::TOK_GREATER; break;
    case '!':
        if (match('=')) {
            kind = K::TOK_NOTEQUAL;
            break;
        }
        diags_.error(rangeFrom(start), "unexpected '!'; use 'not' for logical negation");
        return makeToken(K::UNKNOWN_TOKEN_SYNTAX_ERROR, start);
    default:
        // Take the rest of a multi-byte UTF-8 character so it is reported once.
        while ((static_cast<unsigned char>(peek()) & 0xC0) == 0x80) ++pos_;
        diags_.error(rangeFrom(start),
                     "unexpected character '" + std::string(text_.substr(start, pos_ - start)) + "'");
        return makeToken(K::UNKNOWN_TOKEN_SYNTAX_ERROR, start);
    }
    return makeToken(kind, start);
}

void printTokens(const std::vector<Token>& tokens, std::ostream& out) {
    for (const Token& tok : tokens) {
        SourceLocation loc = tok.location();
        out << loc.line << ':' << loc.column << '\t' << tokenKindName(tok.kind());
        switch (tok.kind()) {
        case TokenKind::TOK_IDENTIFIER:
        case TokenKind::TOK_INTEGER:
        case TokenKind::TOK_FLOAT:
        case TokenKind::TOK_STRING:
        case TokenKind::TOK_KEYWORDTYPE:
            out << "\t'" << tok.spelling() << '\'';
            break;
        default:
            break;
        }
        out << '\n';
    }
}

}  // namespace MemoLang
