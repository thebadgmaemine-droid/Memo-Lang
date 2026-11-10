#ifndef SCOPE_H_
#define SCOPE_H_
// Interface for tracking variable scope and lifetime.
// SymbolId represents the current state of the variable.
// The SymbolId-bound record is very important for destructive analysis.
// Lookup functions and explicit shadowing should be implemented.
// Affine states are mutable, and test functions will be below.
//
// 2026-11-10: added the missing `;` after SymbolId, Binding and Scope, fixed
// the include guard (#ifndef SCOPE_H / #define SCOPER_H never matched),
// included <string_view>, and wrote declare()/lookup() in Scope.cpp.
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace MemoLang {

struct SymbolId {
  std::uint32_t value = 0;

  friend bool operator==(SymbolId, SymbolId) = default;
};

struct Binding {
  std::string name;
  // overload this with type & location if plausible
};

class Scope {
public:
  explicit Scope(Scope *parent = nullptr) : parent_(parent) {}

  // Adds `name` to this scope. Returns false (and changes nothing) if this
  // scope already declares it. A name from a parent scope may be shadowed.
  bool declare(std::string name, SymbolId id);
  // Finds `name` in this scope or the nearest parent that declares it.
  [[nodiscard]] std::optional<SymbolId> lookup(std::string_view name) const;
  // Like lookup(), but only this scope.
  [[nodiscard]] std::optional<SymbolId>
  lookupLocal(std::string_view name) const;

  [[nodiscard]] Scope *parent() const noexcept { return parent_; }

private:
  // Lets find() take a string_view without building a std::string.
  struct NameHash {
    using is_transparent = void;
    std::size_t operator()(std::string_view s) const noexcept {
      return std::hash<std::string_view>{}(s);
    }
  };

  Scope *parent_; // Non-owning; parent must outlive this scope.
  std::unordered_map<std::string, SymbolId, NameHash, std::equal_to<>> names_;
};

} // namespace MemoLang

#endif // SCOPE_H_
