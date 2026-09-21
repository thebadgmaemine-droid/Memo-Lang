#pragma once
#ifndef IR_BUILDER_H
#define IR_BUILDER_H
namespace llvm {
    class BasicBlock;
    class Function;
    class LLVMContext;
    class Module;
    class Type;
    class Value;
} // namespace llvm
namespace memo-lang {
    class DiagnosticEngine;
    class SourceManager;
    class IRBuilder {
        public:
        IRGenerator(llvm::LLVMContext& theContext, TypeContext& types, DiagnosticEngine& diagnostics);
        [[nodiscard]] llvm::Module* getmodule() const; // module placeholder
        emit(const Module& ast,
            const std::string& moduleName,
            const std::vector<const Module*>* imported = nullptr );
        void SetModuleSources(std::unordered_map<const Module*, const SourceManager*> sources);
        // Gets the file each module is read from, the dianosticengine uses this.
        private:
        // Empty for now

    }
}




#endif // IR_BUILDER_H