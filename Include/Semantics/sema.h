#pragma once

#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/DerivedTypes.h"

namespace memo {
namespace sema {

enum class TypeKind {
    Void,
    Bool,
    Integer,
    Float,
    String,
    Pointer,
    Array,
    Struct,
    Function,
    Unknown,
    Vector
};

struct SourceLocation {
    std::uint32_t line = 0;
    std::uint32_t column = 0;
};

struct Type {
    TypeKind kind = TypeKind::Unknown;
    std::string name;
    std::vector<Type> parameters;
    std::unique_ptr<Type> elementType;
    std::uint32_t size = 0;
};

struct Symbol {
    std::string name;
    Type type;
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

class DiagnosticSink {
public:
    DiagnosticSink();
    ~DiagnosticSink();

    void reportError(std::string_view message, const SourceLocation& location);
    void reportWarning(std::string_view message, const SourceLocation& location);

private:
    std::vector<std::string> errors_;
    std::vector<std::string> warnings_;
};

class ASTNode;
class Expr;
class Statement;
class FunctionDecl;
class ModuleDecl;

class SemanticAnalyzer {
public:
    SemanticAnalyzer();
    explicit SemanticAnalyzer(std::shared_ptr<DiagnosticSink> diag);
    ~SemanticAnalyzer();

    bool analyzeModule(ModuleDecl* module);
    bool analyzeFunction(FunctionDecl* function);
    bool analyzeStatement(Statement* stmt);
    bool analyzeExpression(Expr* expr);

    Type resolveType(ASTNode* node);
    Type inferExpressionType(Expr* expr);

    bool checkAssignment(const Type& lhs, const Type& rhs);
    bool checkCall(const Symbol& callee, const std::vector<Type>& argTypes);
    bool checkArithmetic(const Type& lhs, const Type& rhs);
    bool checkComparison(const Type& lhs, const Type& rhs);

    std::string dumpSymbolTable() const;

private:
    std::shared_ptr<Scope> globalScope_;
    std::vector<std::shared_ptr<Scope>> scopes_;
    std::shared_ptr<DiagnosticSink> diagnostics_;
    llvm::DenseMap<std::string, Type> namedTypes_;
};

} // namespace sema
} // namespace memo