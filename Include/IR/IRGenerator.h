#pragma once

#include "AST/AST.h"
#include <llvm/IR/IRBuilder.h>
// Need type context here
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace llvm {
class BasicBlock;
class Function;
class LLVMContext;
class Module;
class Type;
class Value;
} // namespace llvm

namespace memolang {
    class DiagnosticEngine;
    class SourceManager;
    class IRGenerator { 
        
    }
}