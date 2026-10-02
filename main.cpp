#include "AST.h"
#include "parser.h"

#include "llvm/ADT/APFloat.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm_frontend;

// Initialize globals
inline void InitializeModule() {
    TheContext = std::make_unique<llvm::LLVMContext>();
    TheModule = std::make_unique<llvm::Module>("jit tripped", *TheContext);
    Builder = std::make_unique<llvm::IRBuilder<>>(*TheContext);
}

static void HandleDefinition() {
    if (auto FnAST = DefinitionParse()) {
        if (auto* IR = FnAST->codegen()) {
            errs() << "Read function definition:\n";
            IR->print(errs());
            errs() << '\n';
        }
    }
    else getNextToken(); // skip token for error recovery
}

static void HandleExtern() {
    if (auto ProtoAST = ParseExtern()) {
        if (auto* IR = ProtoAST->codegen()) {
            errs() << "Read extern:\n";
            IR->print(errs());
            errs() << '\n';
        }
    }
    else getNextToken();
}

static void HandleTopLevelExpression() {
    if (auto FnAST = ParseTopLevelExpr()) {
        if (auto* IR = FnAST->codegen()) {
            IR->print(errs());
            errs() << '\n';
            IR->eraseFromParent(); // don't let __anon_expr collide next time
        }
    }
    else getNextToken();
}

static void MainLoop() {
    while (true) {
        fprintf(stderr, "ready> ");
        switch (Curtok) {
        case static_cast<int>(Token::tok_eof): return;
        case ';': getNextToken(); break;
        case static_cast<int>(Token::tok_def): HandleDefinition(); break;
        case static_cast<int>(Token::tok_extern): HandleExtern(); break;
        default: HandleTopLevelExpression(); break;
        }
    }
}

int main() {
    BinopPrecedence['<'] = 10;
    BinopPrecedence['+'] = 20;
    BinopPrecedence['-'] = 20;
    BinopPrecedence['*'] = 40;

    fprintf(stderr, "ready> ");
    InitializeModule();
    getNextToken();
    InitializeModule();
    MainLoop();
    TheModule->print(errs(), nullptr);
    return 0;   // 1 signals failure
}