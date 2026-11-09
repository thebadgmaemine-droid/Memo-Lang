#pragma once
#ifndef SCOPE_H
#define SCOPE_H
// Inteface for tracking variable scope and lifetime
// SymbolId  represents the current state of the variable
// The SymbolIID binded record is very important for destructive analysis
// Lookup functions and explicit shadowing should be implemented
// AFfine states are mutable, and test functiosn will be below
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
namespace MemoLang {
    struct SymbolId {
        std::uint32_t value;
    };
    struct Binding {
        std::string name;
        // overload this with type & location if plausible
    };
    class Scope {
        public:
        // Constructor
        explicit Scope(Scope* parent = nullptr) : parent_(parent) {}
        bool declare(std::string name, SymbolId id);
        std::optional<SymbolId> lookup(std::string_view name) const;

        private:
        Scope* parent_; // Non-owning; parent must outlive this scope.
        std::unordered_map<std::string, SymbolId> names_;
        
    };
    






}






#endif // scope.h