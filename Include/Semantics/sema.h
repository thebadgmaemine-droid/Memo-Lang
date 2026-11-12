#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "AST/AST.h"

// 2026-11-12: removed `using SourceLocation = SourceLoc;`, which hid the real
// MemoLang::SourceLocation behind a different struct, and DiagnosticSink:
// errors now go to the DiagnosticEngine every other stage uses, so they are
// printed with source context, counted, and set the exit code.

namespace MemoLang {
class DiagnosticEngine;

namespace sema {

struct Symbol {
    std::string name;
    const Type* type = nullptr;
    bool isFunction = false;
    bool isParameter = false;
    bool isLocal = false;
    std::uint32_t scopeDepth = 0;
    SourceLoc location; // Where it was declared (Node::loc()).
};

class Scope {
public:
    Scope();
    explicit Scope(std::shared_ptr<Scope> parent);
    ~Scope();

    void push();
    void pop();

    void define(const std::string& name, const Symbol& symbol);
    Symbol* lookup(const std::string& name);
    Symbol* lookupLocal(const std::string& name);

private:
    std::shared_ptr<Scope> parent_;
    std::unordered_map<std::string, Symbol> symbols_;
};

class SemanticAnalyzer {
public:
    // `diags` must outlive the analyzer.
    explicit SemanticAnalyzer(DiagnosticEngine& diags);
    ~SemanticAnalyzer();

    bool analyzeModule(Module* module);
    bool analyzeFunction(FunctionDef* function);
    bool analyzeStatement(Statement* stmt);
    bool analyzeExpression(Expr* expr);

    const Type* resolveType(TypeExpr* node);
    const Type* inferExpressionType(Expr* expr);

    bool checkAssignment(const Type& lhs, const Type& rhs);
    bool checkCall(const Symbol& callee,
                   const std::vector<const Type*>& argTypes);
    bool checkArithmetic(const Type& lhs, const Type& rhs);
    bool checkComparison(const Type& lhs, const Type& rhs);

    std::string dumpSymbolTable() const;

private:
    std::shared_ptr<Scope> globalScope_;
    std::vector<std::shared_ptr<Scope>> scopes_;
    DiagnosticEngine& diagnostics_;
    std::unordered_map<std::string, const Type*> namedTypes_;
};

} // namespace sema
} // namespace MemoLang
