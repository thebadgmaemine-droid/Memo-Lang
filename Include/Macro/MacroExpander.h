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
// ---------------------------------------------------------------------------
#include <deque>
#include <string>
#include <vector>

#include "Diag/diagengine.h"
#include "Lex/TokenSource.h"
#include "Lex/token.h"
#include "Macro/MacroTable.h"

namespace MemoLang {
class MacroExpander final : public TokenSource {
public:
  MacroExpander(TokenSource &input, DiagnosticEngine &diags);
  Token next() override; // The next token after expansion.
  const MacroTable &macros() const;

private:
  Token pull(); // pending_ first, then input_.
  void defineMacro(const Token &macroKeyword);
  void expandUse(const Token &name, MacroDef &def);
  bool collectArgs(
      std::vector<std::vector<Token>> &args); // Splits on top-level commas.
  std::vector<Token> substitute(const MacroDef &def,
                                const std::vector<std::vector<Token>> &args,
                                SourceRange useSite);

  TokenSource &input_;
  DiagnosticEngine &diags_;
  MacroTable table_;
  std::deque<Token> pending_;       // Expanded tokens, read before input_.
  std::vector<std::string> active_; // Macros being expanded (recursion guard).
  static constexpr int kMaxExpansionDepth =
      256; // Same idea as kMaxNestingDepth.
};

} // namespace MemoLang

#endif // MEMOLANG_MACRO_EXPANDER_H_
