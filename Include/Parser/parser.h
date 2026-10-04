#pragma once
#include "AST/AST.h"
#include <memory>
#include <vector>
#include <string>
namespace MemoLang {
    class DiagnosticEngine;
    class SourceManager;

    struct ParsedArgs { 
        std::vector<std::unique_ptr<Expr>> Position;
        // std::vector<> // vector for argument hasn't been constructed in tok;

    };
    
}