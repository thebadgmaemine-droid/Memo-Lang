#pragma once

#include <functional>
#include <llvm/IR/IRBuilder.h>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// Need type context here
#include "AST/AST.h"

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
class IRGenerator {}
}