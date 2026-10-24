#include "IR/-IRBuilder.h"

namespace MemoLang {

IRGenerator::IRGenerator(
    llvm::LLVMContext&,
    TypeContext&,
    DiagnosticEngine&) {
}

llvm::Module* IRGenerator::getmodule() const {
    return nullptr;
}

bool IRGenerator::emit(
    const Module&,
    const std::string&,
    const std::vector<const Module*>*) {
    return false;
}

void IRGenerator::SetModuleSources(
    std::unordered_map<const Module*, const SourceManager*>) {
}

} // namespace MemoLang