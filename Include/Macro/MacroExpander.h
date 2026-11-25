#ifndef MEMOLANG_MACRO_EXPANDER_H_
#define MEMOLANG_MACRO_EXPANDER_H_
// ---------------------------------------------------------------------------
// The macro expander: a TokenSource between the Lexer and the Parser.
//
//   Lexer -> [MacroExpander] -> Parser
//
// It reads `macro` definitions into its MacroTable and replaces each use
// (`name!` or `name!(args)`) with the macro's body. Standard library only:
// the front end builds without LLVM.
//
//   macro square(x) { (x) * (x) }     # function-like: square!(n + 1)
//   macro limit { 100 }               # object-like:   limit!
//
// Rules:
// - A macro must be defined, at the top level, before it is used.
// - Arguments are split on top-level commas and expanded before they are
//   substituted, so square!(square!(2)) works. The result is then rescanned
//   for further uses.
// - A macro that (directly or through others) uses itself is an error, as is
//   nesting deeper than kMaxExpansionDepth or producing more than
//   kMaxExpandedTokens tokens in total.
// - Substitution is textual: no hygiene, and an argument used twice is
//   evaluated twice. Parenthesize parameters in the body.
// ---------------------------------------------------------------------------
#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Diag/diagengine.h"
#include "Lex/TokenSource.h"
#include "Lex/token.h"
#include "Macro/MacroTable.h"

namespace MemoLang {
class MacroExpander final : public TokenSource {
public:
  // Same idea as Parser::kMaxNestingDepth.
  static constexpr std::size_t kMaxExpansionDepth = 256;
  // Stops exponential blow-up (each macro using the previous one twice).
  static constexpr std::size_t kMaxExpandedTokens = 1u << 20;

  MacroExpander(TokenSource &input, DiagnosticEngine &diags);
  Token next() override; // The next token after expansion.
  // Expands the rest of the input. The last token is always TOK_EOF.
  [[nodiscard]] std::vector<Token> tokenize();
  [[nodiscard]] const MacroTable &macros() const noexcept { return table_; }

private:
  Token expandNext(); // next() without the brace-depth bookkeeping.
  Token pull();       // pending_ first, then input_; skips end markers.
  void unpull(Token token);
  void defineMacro(const Token &macroKeyword);
  void skipDefinition();
  // False after an error; the use then becomes an error token, so the
  // parser doesn't report it again.
  bool expandUse(const Token &name, const MacroDef &def);
  // Reads `( args )` after `name!`; the `(` is already consumed. Splits on
  // top-level commas, expanding macro uses inside the arguments.
  bool collectArgs(const Token &name, const Token &lparen,
                   std::vector<std::vector<Token>> &args);
  void skipBalancedParens(); // After an error at `name!(`.
  [[nodiscard]] std::vector<Token>
  substitute(const MacroDef &def,
             const std::vector<std::vector<Token>> &args) const;
  [[nodiscard]] bool isActive(std::string_view name) const;
  // Reports an error, plus a note for each expansion it happened in.
  void error(SourceRange range, std::string message);

  struct Expansion {
    std::string name;
    SourceRange use; // The `name` in `name!`.
  };

  TokenSource &input_;
  DiagnosticEngine &diags_;
  MacroTable table_;
  // Tokens from expansions, read before input_. std::nullopt marks the end
  // of an expansion: reading it pops active_.
  std::deque<std::optional<Token>> pending_;
  std::vector<Expansion> active_; // Being expanded (recursion guard).
  std::size_t expandedTokens_ = 0;
  bool tooManyTokens_ = false;
  int braceDepth_ = 0; // Of the tokens handed out; 0 = top level.
};

} // namespace MemoLang

#endif // MEMOLANG_MACRO_EXPANDER_H_
