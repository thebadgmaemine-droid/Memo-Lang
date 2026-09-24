#ifndef EXPRAST_H_
#define EXPRAST_H_
#pragma once
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "llvm/ADT/APFloat.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"

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
*/
/* // means holder has been declared, // // means sourcefile implemented, // // // is fully implemented*/
namespace Memolang {
    enum class Node {
        Expr, //
        TypeExpr, // 
        IntegerLiteral, //
        FloatLiteral, //
        StringLiteral, // 
        BooleanLiteral, //
        BinaryExpr, // 
        UnaryExpr,//
        CallExpr,//
        CastExpr, // 
        LambdaExpr,
        FunctionDef,//
        MacroDef,
        DeclareVar, // 
    };
    enum class State { Live, Consumed, MaybeConsumed }; // There's no maybe live
    enum class BinaryOp {
        Add,
        Sub,
        Mul,
        Div,
        Mod,
        Eq,
        isNot
    };
    enum class UnaryOp {
        Inc,   
        Dec,
        Neg,
        Inv,
    };
    enum class AssignOp {
        Assign,
        Add,
        Sub,
        Mul,
        Div
    };
    struct StringPart {
        std::string literal;
        std::unique_ptr<Expr> value;
        std::string spec; // Format spec after ':', Empty means plain stringo   


        inline StringPart clonestring(const StringPart& part) {
            StringPart copy; 
            copy.literal = part.literal;
            copy.spec = part.spec;
            return copy; 
            // For string copy semantics
        }
    };

  


    class Node {
    public:
        virtual ~Node() = default; 
        Node(Node&&) = default;

    private:
        const Type* resolvedtype = nullptr;

    };

    class TypeExpr final : public Node {
    public:
        Expr(std::string name, std::vector<std::unique_ptr<TypeExpr>> args);
        [[nodiscard]] const std::string& name() const;
        [[nodiscard]] const std::vector<std::unique_ptr<TypeExpr>>& args() const;
    private:
        std::string name_;
        std::vector<std::unique_ptr<TypeExpr>>& args_; 

    };
    // For future reading, private values are called [value]_ to store run time value ( actual data ) //
    // No discards to declare for memory safe //
    // This chunk dynamically holds objects value and safely discards when out of lifetime // 
    class Expr : public Node {
    public:
        using Node::Node;
    };

    class IntegerLiteral final : public Expr {
        IntegerLiteral(int64_t value, bool Isbyte = false);
        [[nodiscard]] double value() const;
        [[nodiscard]] bool isByte const;
    };
    class FloatLiteral final : public Expr {
        FloatLiteral(double value, bool isf32 = false);
        [[nodiscard]] double value() const;
        [[nodiscard]] bool isf32 const;
    };
    class StringLiteral final : public Expr {
        StringLiteral(std::string callee, bool regex = false);
        [[nodiscard]] std::string callee() const;
        [[nodiscard]] bool regex const;
    };
    class BooleanLiteral final : public Expr {
        BooleanLiteral(bool boole);
        [[nodiscard]] boole() const;

    private:
        bool boole_;
    };
    class BooleanLiteral final : public Expr {
    public:
        [[nodiscard]] bool value() const;
        BooleanLiteral(value());
    private:
        bool value_; // Holds run time evaulation
    };
    class CallExpr final : public Expr {
    public:
        CallExpr(
            std::unique_ptr<Expr> callee,
            std::vector<std::unique_ptr<Expr>> TypeArgs;
            [[nodiscard]] const std::vector<std::unique_ptr<Expr>>;
            // What if I
            [[maybeunused]] std::unique_ptr<std::unique_ptr<std::unique_ptr>> > MatRunTimeArgs; // special call
            [[nodiscard]] void setCast(bool value); // No idea if I'll use cast as operations are usually implicit anyways
            void setParamNames(std::vector<std::string >> name);
    private:
        std::unique_ptr<Expr> callee_;
        std::vector<std::unique_ptr<TypeExpr>> typeArgs_;
        bool isCast_ = false;
        std::vector<std::string> paramNames_{};
            

        )
    };
    class statement : public Node {
        using Node::Node;
    };
    class Functiondef final : public statement {
        public:
            struct Capture {
                std::string name;
                const Type* type = nullptr;
            };
            Functiondef(std::string name, std::unique_ptr<TypeExpr> returnType,
                        std::vector<std::unique_ptr<statement>> body,
                        std::string externName);
            [[nodiscard]] const std::string& name() const;
            [[nodiscard]] const TypeExpr& returnType() const;
            [[nodiscard]] const std::vector < std::unique_ptr<statement>& body();
            [[nodiscard]] const std::string& externName() const;
            [[nodiscard]] bool isExtern() const;
            [[nodiscard]] TypeExpr& returnType();
            [[nodiscard]] const std::vector<std::unique_ptr<statement>>& body() const;
    private:
        std::string name_;
        std::unique_ptr<TypeExpr> returnType_;
        std::vector<std::unique_ptr<statement>> body_;
        std::string externName_;



    };

    [[nodiscard]] constexpr BinaryDunderNames binaryDunderNames(BinaryOp op) {
        switch (op) {
        case BinaryOp::Add:
            return { "__add__", "__radd__" };
        case BinaryOp::Sub:
            return { "__sub__", "__rsub__" };
        case BinaryOp::Mul:
            return { "__mul__", "__rmul__" };
        case BinaryOp::Div:
            return { "__truediv__", "__rtruediv__" };
        case BinaryOp::FloorDiv:
            return { "__floordiv__", "__rfloordiv__" };
        case BinaryOp::Mod:
            return { "__mod__", "__rmod__" };
        case BinaryOp::Pow:
            return { "__pow__", "__rpow__" };
        case BinaryOp::Eq:
            return { "__eq__", "__eq__" };
        case BinaryOp::Ne:
            return { "__ne__", "__ne__" };
        case BinaryOp::Lt:
            return { "__lt__", "__gt__" };
        case BinaryOp::Le:
            return { "__le__", "__ge__" };
        case BinaryOp::Gt:
            return { "__gt__", "__lt__" };
        case BinaryOp::Ge:
            return { "__ge__", "__le__" };
        case BinaryOp::BitAnd:
            return { "__and__", "__rand__" };
        case BinaryOp::BitOr:
            return { "__or__", "__ror__" };
        case BinaryOp::BitXor:
            return { "__xor__", "__rxor__" };
        case BinaryOp::Shl:
            return { "__lshift__", "__rlshift__" };
        case BinaryOp::Shr:
            return { "__rshift__", "__rrshift__" };
        case BinaryOp::And:
        case BinaryOp::Or:
        case BinaryOp::Is:
        case BinaryOp::IsNot:
        case BinaryOp::In:
        case BinaryOp::NotIn:
            break;
        }
        return {};
    }
    class DeclareVar final : public statement {
    public:
        DeclareVar(std::string Name,
                   std::unique_ptr<TypeExpr> type,
                   std::unique_ptr<Expr> init,
                   bool is_static = false
                   bool is_const = false;)
    private: 
        [[nodiscard]] const std::string& name() const;
        [[nodiscard]] const std::unique_ptr<TypeExpr> _type;
        [[nodiscard]] bool is_static() const;
        [[nodiscard]] bool is_const() const;
        [[nodiscard]] const Expr* init() const;
        void setName(bool value);
    };

    class UnaryExpr final : public Expr {
    public:
        UnaryExpr(UnaryOp op, std::unique_ptr<Expr> operand);
        [[nodiscard]] UnaryOp op() const;
        [[nodiscard]] const Expr& operand() const;
        [[nodiscard]] Expr& operand();

    private:
        UnaryOp op_;
        std::unique_ptr<Expr> operand_;
    };
    class CastExpr final : public Expr {
        public:
            CastExpr(std::unique_ptr<Expr> value, std::unique_ptr<TypeExpr> target);
            [[nodiscard]] const Expr& value() const;
            [[nodiscard]] Expr& value();
            [[nodiscard]] const TypeExpr& target() const;
            [[nodiscard]] TypeExpr& target();

        private:
            std::unique_ptr<Expr> value_;
            std::unique_ptr<TypeExpr> target_;
        };
}
/*
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

// ----------------------------------------------------------------------------------------------------------------//
#endif // EXPRAST_H_

