#include "Semantics/Scope.h"

#include <utility>

namespace MemoLang {

bool Scope::declare(std::string name, SymbolId id) {
  return names_.try_emplace(std::move(name), id).second;
}

std::optional<SymbolId> Scope::lookupLocal(std::string_view name) const {
  auto it = names_.find(name);
  if (it == names_.end())
    return std::nullopt;
  return it->second;
}

std::optional<SymbolId> Scope::lookup(std::string_view name) const {
  for (const Scope *scope = this; scope != nullptr; scope = scope->parent_)
    if (auto id = scope->lookupLocal(name))
      return id;
  return std::nullopt;
}

} // namespace MemoLang
