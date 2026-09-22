#ifndef EXPRAST_H_
#define EXPRAST_H_
#pragma once
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
// REMINDER THAT LLVM IS HERE FOR DEMO CHECKS, FINAL BACKEND IS WRITTEN IN ASSEMBLY
#include "llvm/ADT/APFloat.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
// REMINDER THAT LLVM IS HERE FOR DEMO CHECKS, FINAL BACKEND IS WRITTEN IN ASSEMBLY

namespace llvm_frontend {

using namespace llvm;
class ExprAST {
public:
    virtual ~ExprAST() = default;
    virtual Value* codegen() = 0; // Codegen() is passed as a virtual function. Value* refers to the POINTER to the OPERANDS used.  
};

// If expression class
class IfExprAST : public ExprAST {
    std::unique_ptr<ExprAST> Cond, Then, Else;
public:
    IfExprAST(std::unique_ptr<ExprAST> Cond, std::unique_ptr<ExprAST> Then,
          std::unique_ptr<ExprAST> Else)
        : Cond(std::move(Cond)), Then(std::move(Then)), Else(std::move(Else)) {}

    Value* codegen() override {
        // TODO: Implement proper codegen for if expressions.
        return nullptr;
    }
};

class BlockExprAST : ExprAST {
    std::vector<std::unique_ptr<ExprAST>> Body;
    public:
        explicit BlockExprAST(std::vector<std::unique_ptr<ExprAST>> Body)
            : Body(std::move(Body)) {

        }
        Value* codegen() override {
            Value* Last = nullptr;
            for (auto& E : Body) {
                Last = E->codegen();
                if (!Last) {
                    return nullptr;
                }
                return Last;
            }
        };

};

//----------------------------------------Error logging------------------------------------------------------------------------//
inline Value* LogErrorV(const char* Message) {
    errs() << "Error: " << Message << '\n'; // errs make sure stdout is flushed before stderr
    return nullptr;
}
//------------------------------------Abstract Syntax Tree class declaration------------------------------------------------//

// Numeric literals expression class

class NumberExprAST : public ExprAST {
    double Val;

public:
    explicit NumberExprAST(const double Val)
        : Val(Val) { // If you modify this, make sure to leave :  Val(Val) on the second line
    }
                    // Note: Overrides the virtual: While parsing through each lines, we call codegen() in general. Codegen() is a virtual function, which means it's method can be
                    // overriden using override so that it does different things depending on context provided by the arguments
    Value* codegen() override {
        return ConstantFP::get(*TheContext, APFloat(Val)); // <- Here codegen() returns Context and APFloat(Val) while below returns differently  
    }
};

// For variable reference
class VariableExprAST : public ExprAST {
    std::string Name;

public:
    explicit VariableExprAST(std::string Name)
        : Name(std::move(Name)) {
    }

    Value* codegen() override {
        auto It = NamedValues.find(Name);
        if (It == NamedValues.end()) {
            return LogErrorV("Unknown variable name");
        }
        return It->second;
    }
};

// For binary
class BinaryExprAST : public ExprAST {
    char Op;
    std::unique_ptr<ExprAST> LHS, RHS;

public:
    BinaryExprAST(const char Op, std::unique_ptr<ExprAST> LHS,
        std::unique_ptr<ExprAST> RHS)
        : Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {
    }

    Value* codegen() override {
        Value* L = LHS->codegen(); // Right hand side, left hand side
        Value* R = RHS->codegen();
        if (!L || !R) {
            return nullptr;
        }

        switch (Op) {
        case '+':
            return Builder->CreateFAdd(L, R, "addtmp");
        case '-':
            return Builder->CreateFSub(L, R, "subtmp");
        case '*':
            return Builder->CreateFMul(L, R, "multmp");
        case '<': {
            Value* Comparison = Builder->CreateFCmpULT(L, R, "cmptmp");
            return Builder->CreateUIToFP(
                Comparison, Type::getDoubleTy(*TheContext), "booltmp");
        }
        default:
            return LogErrorV("Invalid binary operator");
        }
    }
};

// For function call
class FunctionExprAST : public ExprAST {
    std::string Callee;
    std::vector<std::unique_ptr<ExprAST>> Args;

public:
    // This implements the constructor for the function call expression AST node.
    // It takes a string; the name of the function called (Callee)
    // plus a vector of unique pointers to ExprAST objects as the arguments.
    FunctionExprAST(std::string Callee,
        std::vector<std::unique_ptr<ExprAST>> Args)
        : Callee(std::move(Callee)), Args(std::move(Args)) {
    }

    Value* codegen() override {
        Function* CalleeFunction = TheModule->getFunction(Callee);
        if (!CalleeFunction) {
            return LogErrorV("Unknown function referenced");
        }
        if (CalleeFunction->arg_size() != Args.size()) {
            return LogErrorV("Incorrect number of arguments passed");
        }

        std::vector<Value*> ArgumentValues;
        for (const auto& Arg : Args) {
            Value* ArgumentValue = Arg->codegen(); 
            if (!ArgumentValue) {
                return nullptr;
            }
            ArgumentValues.push_back(ArgumentValue);
        }

        return Builder->CreateCall(CalleeFunction, ArgumentValues, "calltmp");
    }
};
/*------------------------------------------------Prototype--------------------------------------*/
class PrototypeAST {
    std::string Name;
    std::vector<std::string> Args;

public:
    PrototypeAST(std::string Name, std::vector<std::string> Args)
        : Name(std::move(Name)), Args(std::move(Args)) {
    }

    const std::string& getName() const {
        return Name;
    }

    Function* codegen() {
        std::vector<Type*> ArgumentTypes(
            Args.size(), Type::getDoubleTy(*TheContext));
        FunctionType* FunctionTypeValue = FunctionType::get(
            Type::getDoubleTy(*TheContext), ArgumentTypes, false);

        Function* FunctionValue = Function::Create(
            FunctionTypeValue, Function::ExternalLinkage, Name, TheModule.get());

        unsigned Index = 0;
        for (Argument& Arg : FunctionValue->args()) {
            Arg.setName(Args[Index++]);
        }

        return FunctionValue;
    }
};
/*-------------------------------------------------FUNCTION--------------------------------------------------*/
class FunctionAST {
    std::unique_ptr<PrototypeAST> Proto;
    std::unique_ptr<ExprAST> Body;

public:
    FunctionAST(std::unique_ptr<PrototypeAST> Proto,
        std::unique_ptr<ExprAST> Body)
        : Proto(std::move(Proto)), Body(std::move(Body)) {
    }

    Function* codegen() {
        Function* FunctionValue = TheModule->getFunction(Proto->getName());
        if (!FunctionValue) {
            FunctionValue = Proto->codegen();
        }
        if (!FunctionValue || !FunctionValue->empty()) { 
            return nullptr;
        }

        BasicBlock* Block = BasicBlock::Create(
            *TheContext, "entry", FunctionValue);
        Builder->SetInsertPoint(Block);
        NamedValues.clear();

        for (Argument& Arg : FunctionValue->args()) {
            NamedValues[std::string(Arg.getName())] = &Arg;
        }

        Value* ReturnValue = Body->codegen();
        if (!ReturnValue) {
            FunctionValue->eraseFromParent(); // Unlinks from container module and deletes it
            return nullptr;
        }

        Builder->CreateRet(ReturnValue);
        if (verifyFunction(*FunctionValue, &errs())) {
            FunctionValue->eraseFromParent();
            return nullptr;
        }

        return FunctionValue;
    }
};
*/
} // namespace llvm_frontend

namespace Memolang {
    enum class Node {
        Expr,
        IntegerLiteral,
        FloatLiteral,
        StringLiteral,
        BooleanLiteral,
        BinaryExpr,
        UnaryExpr,
        CallExpr,
        MemberExpr,
        [[maybe_unused]] CastExpr, // Maybe unusued
        LambdaExpr,
        VarDeclaration,
        FunctionDef,
        MacroDef,
        Module,
    };
    class Node {
    public:
        virtual ~Node() = default; 
        Node(Node&&) = default;

    private:
        const Type* resolvedtype = nullptr;

    };

    class Expr final : public Node {

    };





}


// ----------------------------------------------------------------------------------------------------------------//
#endif // EXPRAST_H_

