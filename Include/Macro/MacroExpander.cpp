// ---------------------------------------------------------------------------
// MacroExpander.cpp: expands `macro` definitions between lexer and parser.
// See MacroExpander.h for the rules.
//
// History: added 2026-11-25.
// ---------------------------------------------------------------------------
#include "Macro/MacroExpander.h"

#include <algorithm>
#include <utility>

namespace MemoLang {
namespace {

std::string quoted(std::string_view name) {
  return "'" + std::string(name) + "'";
}

std::string plural(std::size_t n, std::string_view word) {
  return std::to_string(n) + " " + std::string(word) + (n == 1 ? "" : "s");
}

bool opens(TokenKind kind) {
  return kind == TokenKind::TOK_LPAREN || kind == TokenKind::TOK_LBRACKET ||
         kind == TokenKind::TOK_LBRACE;
}

bool closes(TokenKind kind) {
  return kind == TokenKind::TOK_RPAREN || kind == TokenKind::TOK_RBRACKET ||
         kind == TokenKind::TOK_RBRACE;
}

} // namespace

MacroExpander::MacroExpander(TokenSource &input, DiagnosticEngine &diags)
    : input_(input), diags_(diags) {}

// ---------------------------------------------------------------------------
// Token stream
// ---------------------------------------------------------------------------
Token MacroExpander::pull() {
  while (!pending_.empty()) {
    std::optional<Token> front = std::move(pending_.front());
    pending_.pop_front();
    if (front)
      return std::move(*front);
    active_.pop_back(); // End of an expansion.
  }
  return input_.next();
}

void MacroExpander::unpull(Token token) {
  pending_.push_front(std::move(token));
}
// Brace depth manager
Token MacroExpander::next() {
  Token token = expandNext();
  if (token.is(TokenKind::TOK_LBRACE))
    ++braceDepth_;
  else if (token.is(TokenKind::TOK_RBRACE) && braceDepth_ > 0)
    --braceDepth_;
  return token;
}

std::vector<Token> MacroExpander::tokenize() {
  std::vector<Token> tokens;
  do
    tokens.push_back(next());
  while (!tokens.back().is(TokenKind::TOK_EOF));
  return tokens;
}

Token MacroExpander::expandNext() {
  for (;;) {
    Token token = pull();
    if (token.is(TokenKind::TOK_MACRO)) {
      defineMacro(token);
      continue;
    }
    if (!token.is(TokenKind::TOK_IDENTIFIER))
      return token;

    Token after = pull();
    if (!after.is(TokenKind::TOK_BANG)) {
      unpull(std::move(after));
      return token;
    }

    // `name!`: a macro use.
    const MacroDef *def = table_.lookup(token.spelling());
    if (!def) {
      diags_.error(token.range(),
                   quoted(token.spelling()) +
                       " is not a macro (a macro must be defined before it "
                       "is used)");
      Token open = pull();
      if (open.is(TokenKind::TOK_LPAREN))
        skipBalancedParens();
      else
        unpull(std::move(open));
      // Already reported, so the parser stays quiet about it.
      return Token(TokenKind::UNKNOWN_TOKEN_SYNTAX_ERROR, token.range(),
                   std::string(token.spelling()));
    }
    if (!expandUse(token, *def))
      return Token(TokenKind::UNKNOWN_TOKEN_SYNTAX_ERROR, token.range(),
                   std::string(token.spelling()));
  }
}

bool MacroExpander::isActive(std::string_view name) const {
  return std::any_of(active_.begin(), active_.end(),
                     [&](const Expansion &e) { return e.name == name; });
}

void MacroExpander::error(SourceRange range, std::string message) {
  diags_.error(range, std::move(message));
  // Innermost first. For a deep chain, the innermost few and the use in the
  // source file are what matter.
  constexpr std::size_t kInner = 3;
  const std::size_t n = active_.size();
  for (std::size_t i = 0; i < n; ++i) {
    if (i == kInner && n > kInner + 1) {
      i = n - 1; // Jump to the outermost.
      diags_.note(active_[0].use,
                  "(" + std::to_string(n - kInner - 1) +
                      " more expansions)");
    }
    const Expansion &e = active_[n - 1 - i];
    diags_.note(e.use, "in the expansion of " + quoted(e.name + "!"));
  }
}

// ---------------------------------------------------------------------------
// Definitions: macro NAME [ ( params ) ] { body }
// ---------------------------------------------------------------------------
void MacroExpander::defineMacro(const Token &macroKeyword) {
  if (braceDepth_ > 0 || !active_.empty()) {
    diags_.error(macroKeyword.range(),
                 "a macro can only be defined at the top level");
    skipDefinition();
    return;
  }

  Token name = pull();
  if (!name.is(TokenKind::TOK_IDENTIFIER)) {
    diags_.error(name.range(), "expected a macro name after 'macro'");
    unpull(std::move(name));
    skipDefinition();
    return;
  }

  MacroDef def;
  def.range = name.range();

  Token tok = pull();
  if (tok.is(TokenKind::TOK_LPAREN)) {
    def.isFunctionLike = true;
    tok = pull();
    if (!tok.is(TokenKind::TOK_RPAREN)) {
      for (;;) {
        if (!tok.is(TokenKind::TOK_IDENTIFIER)) {
          diags_.error(tok.range(), "expected a parameter name");
          unpull(std::move(tok));
          skipDefinition();
          return;
        }
        std::string param(tok.spelling());
        if (std::find(def.params.begin(), def.params.end(), param) !=
            def.params.end()) {
          diags_.error(tok.range(), "duplicate parameter " + quoted(param) +
                                        " in macro " +
                                        quoted(name.spelling()));
        }
        def.params.push_back(std::move(param));
        tok = pull();
        if (tok.is(TokenKind::TOK_RPAREN))
          break;
        if (!tok.is(TokenKind::TOK_COMMA)) {
          diags_.error(tok.range(),
                       "expected ',' or ')' after a macro parameter");
          unpull(std::move(tok));
          skipDefinition();
          return;
        }
        tok = pull();
      }
    }
    tok = pull();
  }

  if (!tok.is(TokenKind::TOK_LBRACE)) {
    diags_.error(tok.range(), "expected '{' to start the body of macro " +
                                  quoted(name.spelling()));
    unpull(std::move(tok));
    skipDefinition();
    return;
  }

  const SourceRange open = tok.range();
  int depth = 1;
  for (;;) {
    Token bodyTok = pull();
    if (bodyTok.is(TokenKind::TOK_EOF)) {
      diags_.error(open, "unterminated body of macro " +
                             quoted(name.spelling()) + "; expected '}'");
      unpull(std::move(bodyTok));
      return;
    }
    if (bodyTok.is(TokenKind::TOK_LBRACE)) {
      ++depth;
    } else if (bodyTok.is(TokenKind::TOK_RBRACE) && --depth == 0) {
      break;
    } else if (bodyTok.is(TokenKind::TOK_MACRO)) {
      diags_.error(bodyTok.range(),
                   "a macro can only be defined at the top level");
    }
    def.bodyTokens.push_back(std::move(bodyTok));
  }

  if (const MacroDef *previous = table_.lookup(name.spelling())) {
    diags_.error(name.range(),
                 "macro " + quoted(name.spelling()) + " is already defined");
    diags_.note(previous->range, "the earlier definition is here");
    return;
  }
  table_.defineMacro(name.spelling()) = std::move(def);
}

// Skips the rest of a broken definition: up to and including its `{ ... }`
// body, or up to a `;` or the start of a line-level keyword if there is no
// body. Stops at EOF.
void MacroExpander::skipDefinition() {
  for (;;) {
    Token tok = pull();
    switch (tok.kind()) {
    case TokenKind::TOK_EOF:
    case TokenKind::TOK_DEFINE:
    case TokenKind::TOK_EXTERN:
    case TokenKind::TOK_MACRO:
      unpull(std::move(tok));
      return;
    case TokenKind::TOK_SEMICOLON:
      return;
    case TokenKind::TOK_LBRACE: {
      int depth = 1;
      while (depth > 0) {
        Token inner = pull();
        if (inner.is(TokenKind::TOK_EOF)) {
          unpull(std::move(inner));
          return;
        }
        if (inner.is(TokenKind::TOK_LBRACE))
          ++depth;
        else if (inner.is(TokenKind::TOK_RBRACE))
          --depth;
      }
      return;
    }
    default:
      break;
    }
  }
}

// ---------------------------------------------------------------------------
// Uses: NAME ! [ ( args ) ]
// ---------------------------------------------------------------------------
bool MacroExpander::expandUse(const Token &name, const MacroDef &def) {
  std::vector<std::vector<Token>> args;
  if (def.isFunctionLike) {
    Token open = pull();
    if (!open.is(TokenKind::TOK_LPAREN)) {
      error(open.range(),
                   "expected '(' after " + quoted(std::string(name.spelling()) +
                                                  "!") +
                       "; macro " + quoted(name.spelling()) + " takes " +
                       plural(def.params.size(), "argument"));
      unpull(std::move(open));
      return false;
    }
    if (!collectArgs(name, open, args))
      return false;
    // `f!()` is one empty argument; for a macro without parameters it is
    // zero arguments.
    if (def.params.empty() && args.size() == 1 && args[0].empty())
      args.clear();
    if (args.size() != def.params.size()) {
      error(name.range(), "macro " + quoted(name.spelling()) +
                                     " takes " +
                                     plural(def.params.size(), "argument") +
                                     ", but " + std::to_string(args.size()) +
                                     (args.size() == 1 ? " was" : " were") +
                                     " given");
      return false;
    }
  }

  // Checked after the arguments, which may use the same macro:
  // square!(square!(2)) is fine, a body that uses itself is not.
  if (isActive(name.spelling())) {
    error(name.range(),
                 "macro " + quoted(name.spelling()) + " expands into itself");
    return false;
  }
  if (active_.size() >= kMaxExpansionDepth) {
    error(name.range(), "macros are nested too deeply (more than " +
                                   std::to_string(kMaxExpansionDepth) +
                                   " levels)");
    return false;
  }

  std::vector<Token> expansion = substitute(def, args);
  expandedTokens_ += expansion.size();
  if (expandedTokens_ > kMaxExpandedTokens) {
    if (!tooManyTokens_)
      error(name.range(),
                   "macro expansion is too large (more than " +
                       std::to_string(kMaxExpandedTokens) + " tokens)");
    tooManyTokens_ = true;
    return false;
  }

  // Read the expansion next, then the end marker, then what followed.
  active_.push_back({std::string(name.spelling()), name.range()});
  pending_.push_front(std::nullopt);
  for (auto it = expansion.rbegin(); it != expansion.rend(); ++it)
    pending_.push_front(std::move(*it));
  return true;
}

bool MacroExpander::collectArgs(const Token &name, const Token &lparen,
                                std::vector<std::vector<Token>> &args) {
  args.emplace_back();
  int depth = 0;
  for (;;) {
    // expandNext(): macro uses inside an argument are expanded first.
    Token tok = expandNext();
    // EOF, or a `]` / `}` that closes something outside the use.
    if (tok.is(TokenKind::TOK_EOF) || (depth == 0 && closes(tok.kind()) &&
                                       !tok.is(TokenKind::TOK_RPAREN))) {
      error(tok.range(), "expected ')' to close the arguments to " +
                             quoted(std::string(name.spelling()) + "!"));
      diags_.note(lparen.range(), "the '(' is here");
      unpull(std::move(tok));
      return false;
    }
    if (depth == 0 && tok.is(TokenKind::TOK_RPAREN))
      return true;
    if (depth == 0 && tok.is(TokenKind::TOK_COMMA)) {
      args.emplace_back();
      continue;
    }
    if (opens(tok.kind()))
      ++depth;
    else if (closes(tok.kind()))
      --depth;
    args.back().push_back(std::move(tok));
  }
}

void MacroExpander::skipBalancedParens() {
  int depth = 1;
  while (depth > 0) {
    Token tok = pull();
    if (tok.is(TokenKind::TOK_EOF)) {
      unpull(std::move(tok));
      return;
    }
    if (tok.is(TokenKind::TOK_LPAREN))
      ++depth;
    else if (tok.is(TokenKind::TOK_RPAREN))
      --depth;
  }
}

std::vector<Token>
MacroExpander::substitute(const MacroDef &def,
                          const std::vector<std::vector<Token>> &args) const {
  std::vector<Token> out;
  out.reserve(def.bodyTokens.size());
  for (const Token &tok : def.bodyTokens) {
    if (tok.is(TokenKind::TOK_IDENTIFIER)) {
      auto param = std::find(def.params.begin(), def.params.end(),
                             tok.spelling());
      if (param != def.params.end()) {
        const auto &arg = args[param - def.params.begin()];
        out.insert(out.end(), arg.begin(), arg.end());
        continue;
      }
    }
    out.push_back(tok);
  }
  return out;
}

} // namespace MemoLang
