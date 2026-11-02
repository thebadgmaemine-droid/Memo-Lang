#pragma once
#ifndef PARSER_H_
#define PARSER_H_

#include <cctype>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ExprAST.h"
// Target 16/9/2026: Reformat comments this way
// <summary>
//
// </summary>
// <returns></returns>

// keyword: eat::= take in
namespace llvm_frontend {

    enum class Token {
        tok_eof = -1,
        tok_def = -2,
        tok_extern = -3,
        tok_identifier = -4,
        tok_number = -5,
        tok_if = -6,
        tok_then = -7,
        tok_else = -8,
        tok_for = -9
};

inline std::string IdentifierStr;
inline double NumVal = 0.0;
inline int Curtok = ' ';
inline std::map<char, int> BinopPrecedence;

inline int gettok() {
    static int LastChar = ' ';
    while (std::isspace(static_cast<unsigned char>(LastChar)))
        LastChar = std::getchar();
    if (LastChar == EOF)
        return static_cast<int>(Token::tok_eof);
    if (std::isalpha(static_cast<unsigned char>(LastChar)) || LastChar == '_') {
        IdentifierStr.clear();
        do {
            IdentifierStr += static_cast<char>(LastChar);
            LastChar = std::getchar();
        } while (std::isalnum(static_cast<unsigned char>(LastChar)) || LastChar == '_');
        if (IdentifierStr == "def" || IdentifierStr == "procedure")
            return static_cast<int>(Token::tok_def);
        if (IdentifierStr == "ext")
            return static_cast<int>(Token::tok_extern);
        if (IdentifierStr == "if")
            return static_cast<int>(Token::tok_if);
        if (IdentifierStr == "for")
            return static_cast<int>(Token::tok_for);
        if (IdentifierStr == "else")
            return static_cast<int>(Token::tok_else);
        if (IdentifierStr == "then")
            return static_cast<int>(Token::tok_then);
        return static_cast<int>(Token::tok_identifier);
    }
    if (std::isdigit(static_cast<unsigned char>(LastChar)) || LastChar == '.') {
        std::string Number;
        bool HasDot = false;
        do {
            if (LastChar == '.') HasDot = true;
            Number += static_cast<char>(LastChar);
            LastChar = std::getchar();
        } while (std::isdigit(static_cast<unsigned char>(LastChar)) || (!HasDot && LastChar == '.'));
        NumVal = std::strtod(Number.c_str(), nullptr);
        return static_cast<int>(Token::tok_number);
    }
    int ThisChar = LastChar;
    LastChar = std::getchar();
    return ThisChar;
}

inline int getNextToken() { return Curtok = gettok(); }

inline int GetTokPrecedence() {
    if (!isascii(Curtok)) return -1;
    auto It = BinopPrecedence.find(static_cast<char>(Curtok));
    return It == BinopPrecedence.end() || It->second <= 0 ? -1 : It->second;
}

inline std::unique_ptr<ExprAST> LogError(const char *Message) {
    std::cerr << "Error: " << Message << '\n';
    return nullptr;
}

inline std::unique_ptr<ExprAST> ParseExpression();

inline std::unique_ptr<ExprAST> ParseNumberExpr() {
    auto Result = std::make_unique<NumberExprAST>(NumVal);
    getNextToken();
    return std::move(Result);
}

inline std::unique_ptr<ExprAST> ParseParenExpr() {
    getNextToken();
    auto Value = ParseExpression();
    if (!Value) return nullptr;
    if (Curtok != ')') return LogError("Expected ')' ");
    getNextToken();
    return Value;
}

inline std::unique_ptr<PrototypeAST> LogErrorP(const char* Message) {
    LogError(Message);
    return nullptr;
}
// prototype ::= name '(' *id (<- here is kleene star zero, means zero repetition ')' to prevent )) 

inline std::unique_ptr<PrototypeAST> PrototypeParse() {
    if (Curtok != static_cast<int>(Token::tok_identifier)) {
        return LogErrorP("Expected function name in prototype");
    }
    std::string Fname = IdentifierStr;
    getNextToken();
    if (Curtok != '(') return LogErrorP("Expected '(' in prototype");
    std::vector<std::string> ArgsName;
    while (getNextToken() == static_cast<int>(Token::tok_identifier)) {
        ArgsName.push_back(IdentifierStr);
    
    }
    if (Curtok != ')') return LogErrorP("Expected ')' in prototype");

    getNextToken(); // eat the )
    return std::make_unique<PrototypeAST>(std::move(Fname), std::move(ArgsName));
     
}
// Note: Function here doesn't just mean the procedure or routine, every line is wrapped around an anonymous "function"
// Define prototype EXPRESSIONS
inline std::unique_ptr<FunctionAST> DefinitionParse() {
    getNextToken(); // eat defines and procedures 
    auto Proto = PrototypeParse();
    if (!Proto) return nullptr;
    if (auto Body = ParseExpression()) {
        return std::make_unique<FunctionAST>(std::move(Proto), std::move(Body));
    }
    return nullptr;
}
// Define prototype EXTERN
inline std::unique_ptr<PrototypeAST> ParseExtern() {
    getNextToken();
    return PrototypeParse();
}
// Anonymous function, as promised
inline std::unique_ptr<FunctionAST> ParseTopLevelExpr() { // Don't have this as Prototype
    if (auto E = ParseExpression()) {
        std::unique_ptr<PrototypeAST> Proto = std::make_unique<PrototypeAST>("__anon_expr", std::vector<std::string>()); // note that you should call using types so compiler doesn't have to determine auto, removes the little overhead but may be a problem for debugger
        return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
    }
    return nullptr;
}
// Block parser
inline std::unique_ptr<ExprAST> ParseBlock() {
    std::vector<std::unique_ptr<ExprAST>> Body;
    while (Curtok != static_castA<int>(Token::tok_else)) {
        auto Expr = ParseExpression();
        if (!Expr) {
            Body.push_back(std::move(Expr));
        }
        if (Curtok == ';') {
            getNextToken();
        }
        else {
            break;
        }
    }
    return std::make_unique<BlockExprAST>(std::move(Body)); // I have no idea why its saying overloaded logged 18/9/2026
    
    getNextToken();
    
}
// If/else/denn parser
inline std::unique_ptr<ExprAST> ParseIfExpr() {
    getNextToken(); // eat if

    auto Cond = ParseExpression();
    if (!Cond) return nullptr;

    if (Curtok != static_cast<int>(Token::tok_then))
        return LogError("Expected 'then'");
    getNextToken(); // eat then

    auto Then = ParseBlock(); // stops at tok_else
    if (!Then) return nullptr;

    if (Curtok != static_cast<int>(Token::tok_else))
        return LogError("Expected 'else'");
    getNextToken(); // eat 'else'

    auto Else = ParseBlock(); // <-- still stops at tok_else, which is wrong here
    if (!Else) return nullptr;

    return std::make_unique<IfExprAST>(std::move(Cond), std::move(Then), std::move(Else));
}

inline std::unique_ptr<ExprAST> ParseIdentifierExpr() {
    std::string IdName = IdentifierStr;
    getNextToken();
    if (Curtok != '(')
        return std::make_unique<VariableExprAST>(std::move(IdName));

    std::vector<std::unique_ptr<ExprAST>> Args;
    getNextToken();
    if (Curtok != ')') {
        while (true) {
            auto Arg = ParseExpression();
            if (!Arg) return nullptr;
            Args.push_back(std::move(Arg));
            if (Curtok == ')') break;
            if (Curtok != ',') return LogError("Expected ')' or ',' in argument list");
            getNextToken();
        }
    }
    getNextToken();
    return std::make_unique<FunctionExprAST>(std::move(IdName), std::move(Args));
}

inline std::unique_ptr<ExprAST> ParsePrimary() {
    switch (Curtok) {
    default: return LogError("Expected an expression");
    case static_cast<int>(Token::tok_identifier): return ParseIdentifierExpr();
    case static_cast<int>(Token::tok_number): return ParseNumberExpr();
    case '(': return ParseParenExpr();
    case static_cast<int>(Token::tok_if): return ParseifAST();
    }
}

inline std::unique_ptr<ExprAST> ParseBinOpRHS(int ExpressionPrecedence, std::unique_ptr<ExprAST> LHS) {
    while (true) {
        int TokenPrecedence = GetTokPrecedence();
        if (TokenPrecedence < ExpressionPrecedence) return LHS;
        int BinaryOperator = Curtok;
        getNextToken();
        auto RHS = ParsePrimary();
        if (!RHS) return nullptr;
        int NextPrecedence = GetTokPrecedence();
        if (TokenPrecedence < NextPrecedence)
            RHS = ParseBinOpRHS(TokenPrecedence + 1, std::move(RHS));
        if (!RHS) return nullptr;
        LHS = std::make_unique<BinaryExprAST>(static_cast<char>(BinaryOperator), std::move(LHS), std::move(RHS));
    }
}

inline std::unique_ptr<ExprAST> ParseExpression() {
    auto LHS = ParsePrimary();
    if (!LHS) return nullptr;
    return ParseBinOpRHS(0, std::move(LHS));
}

}

#endif // PARSER_H_