#ifndef MEMOLANG_MACRO_TABLE_H_
#define MEMOLANG_MACRO_TABLE_H_
// The macros a file defines, by name. Filled and read by the macro expander,
// a stage between the lexer and the parser (see MacroExpander.h).
//
// 2026-11-24: moved here from lexer.h, where it was first sketched with
// llvm::StringMap / SmallVector / StringRef. The front end builds without
// LLVM (MEMOLANG_USE_LLVM is OFF by default, and CI builds without it), so
// this uses the standard library. A file defines a handful of macros, so
// std::unordered_map is plenty.
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Lex/token.h"
#include "Support/Compiler.h"

namespace MemoLang {

struct MacroDef {
  std::vector<std::string> params; // Parameter names, in order.
  std::vector<Token> bodyTokens;   // The body, substituted at each use.
  bool isFunctionLike = false;     // `name!(args)` rather than `name!`.
  bool isVariadic = false;
  SourceRange range{}; // The name in the definition, for redefinition notes.
};

class MacroTable {
public:
  [[nodiscard]] MacroDef *find(std::string_view name) MEMOLANG_LIFETIMEBOUND {
    auto it = macros_.find(name);
    return it == macros_.end() ? nullptr : &it->second;
  }
  [[nodiscard]] const MacroDef *lookup(std::string_view name) const MEMOLANG_LIFETIMEBOUND {
    auto it = macros_.find(name);
    return it == macros_.end() ? nullptr : &it->second;
  }
  // The definition for `name`, created empty if it doesn't exist yet.
  MacroDef &insert(std::string_view name) MEMOLANG_LIFETIMEBOUND { return macros_[std::string(name)]; }
  // Starts a fresh definition of `name`, replacing any earlier one. Check
  // isDefined() first to report a redefinition.
  MacroDef &defineMacro(std::string_view name) MEMOLANG_LIFETIMEBOUND {
    MacroDef &def = macros_[std::string(name)];
    def = MacroDef{};
    return def;
  }
  // Was `name` already defined (so defining it again is a redefinition)?
  [[nodiscard]] bool isDefined(std::string_view name) const {
    return macros_.find(name) != macros_.end();
  }
  bool erase(std::string_view name) {
    // erase() can't take a string_view until C++23.
    auto it = macros_.find(name);
    if (it == macros_.end())
      return false;
    macros_.erase(it);
    return true;
  }
  void clear() { macros_.clear(); }
  [[nodiscard]] std::size_t size() const noexcept { return macros_.size(); }

private:
  // Lets find() take a string_view without building a std::string (the
  // same trick as Semantics/Scope.h).
  struct NameHash {
    using is_transparent = void;
    std::size_t operator()(std::string_view s) const noexcept {
      return std::hash<std::string_view>{}(s);
    }
  };
  std::unordered_map<std::string, MacroDef, NameHash, std::equal_to<>> macros_;
};

} // namespace MemoLang
#endif // MEMOLANG_MACRO_TABLE_H_
