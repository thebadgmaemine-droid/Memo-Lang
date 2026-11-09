#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "AST/AST.h"

namespace MemoLang {
class DiagnosticEngine;

namespace sema {

using SourceLocation = SourceLoc;

struct Symbol {
    std::string name;
    const Type* type = nullptr;
    bool isFunction = false;
    bool isParameter = false;
    bool isLocal = false;
    std::uint32_t scopeDepth = 0;
    SourceLocation location;
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
    SemanticAnalyzer();
    explicit SemanticAnalyzer(std::shared_ptr<DiagnosticEngine> diag);
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
    std::shared_ptr<DiagnosticEngine> diagnostics_;
    std::unordered_map<std::string, const Type*> namedTypes_;
};

} // namespace sema
} // namespace MemoLang
