#ifndef MEMOLANG_MACRO_TABLE_H_
#define MEMOLANG_MACRO_TABLE_H_
// The macros a file defines, by name. Filled and read by the macro expander,
// a stage between the lexer and the parser (planned; nothing uses this yet).
//
// 2026-11-24: moved here from lexer.h, where it was first sketched with
// llvm::StringMap / SmallVector / StringRef. The front end builds without
// LLVM (MEMOLANG_USE_LLVM is OFF by default, and CI builds without it), so
// this uses the standard library. A file defines a handful of macros, so
// std::unordered_map is plenty.
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Lex/token.h"

namespace MemoLang {

struct MacroDef {
  std::vector<std::string> params; // Parameter names, in order.
  std::vector<Token> bodyTokens;   // The body, substituted at each use.
  bool isFunctionLike = false;     // `name!(args)` rather than `name!`.
  bool isVariadic = false;
};

class MacroTable {
public:
  [[nodiscard]] MacroDef *find(std::string_view name) {
    auto it = macros_.find(std::string(name));
    return it == macros_.end() ? nullptr : &it->second;
  }
  [[nodiscard]] const MacroDef *lookup(std::string_view name) const {
    auto it = macros_.find(std::string(name));
    return it == macros_.end() ? nullptr : &it->second;
  }
  // The definition for `name`, created empty if it doesn't exist yet.
  MacroDef &insert(std::string_view name) { return macros_[std::string(name)]; }
  // Starts a fresh definition of `name`, replacing any earlier one. Check
  // isDefined() first to report a redefinition.
  MacroDef &defineMacro(std::string_view name) {
    MacroDef &def = macros_[std::string(name)];
    def = MacroDef{};
    return def;
  }
  // Was `name` already defined (so defining it again is a redefinition)?
  [[nodiscard]] bool isDefined(std::string_view name) const {
    return macros_.count(std::string(name)) != 0;
  }
  bool erase(std::string_view name) {
    return macros_.erase(std::string(name)) != 0;
  }
  void clear() { macros_.clear(); }
  [[nodiscard]] std::size_t size() const noexcept { return macros_.size(); }

private:
  std::unordered_map<std::string, MacroDef> macros_;
};

} // namespace MemoLang
#endif // MEMOLANG_MACRO_TABLE_H_
