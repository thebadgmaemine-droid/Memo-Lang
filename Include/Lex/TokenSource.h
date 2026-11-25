#ifndef MEMOLANG_TOKEN_SOURCE_H_
#define MEMOLANG_TOKEN_SOURCE_H_
#include "Lex/token.h"

namespace MemoLang {
// Anything the parser can read tokens from: the lexer, or the macro
// expander wrapping it.
class TokenSource {
public:
  virtual ~TokenSource() = default;
  virtual Token next() = 0;
};
} // namespace MemoLang
#endif