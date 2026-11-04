#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <cassert>
#include <shared_mutex>

#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/Allocator.h"
using namespace llvm;
namespace MemoLang {
        // add here when needed
        // Generic key to build a "string key" for a generic type
        class Type {
        public:
        // subclass for RTTI class. These stores subclasses that will be declare later.
        // It can then be used for downcasting, so we can instrinsically cast
        enum class Kind { Int, Pointer, Generic, Vector };


        virtual ~Type() = default;
        Kind kind() const { return kind_; }
        // for diagnostics
        virtual std::string display() const = 0; 

        protected:
        explicit Type(Kind k) : kind_(k) {}

        private:
        Kind kind_;
        };
        class IntType final : public Type {
        public:
            IntType(unsigned bits, bool isSigned)
                : Type(Kind::Int), bits_(bits), signed_(isSigned) {}
            unsigned bits() const { return bits_; }
            bool isSigned() const { return signed_; }
            std::string display() const override {
                return (signed_ ? "i" : "u") + std::to_string(bits_);
            }
            static bool classof(const Type *t) { return t->kind() == Kind::Int; }
        private:
            unsigned bits_;
            bool signed_;
        };

        class PointerType final : public Type {
        public:
            explicit PointerType(const Type *pointee)
                : Type(Kind::Pointer), pointee_(pointee) {}
            const Type *pointee() const { return pointee_; }
            std::string display() const override { return pointee_->display() + "*"; }
            static bool classof(const Type *t) { return t->kind() == Kind::Pointer; }
        private:
            const Type *pointee_;
        };

        // An instantiated generic, e.g. List<i32>
        class GenericType final : public Type {
        public:
            GenericType(std::string ctor, std::vector<const Type *> args)
                : Type(Kind::Generic), ctor_(std::move(ctor)), args_(std::move(args)) {}
            const std::string &ctor() const { return ctor_; }
            const std::vector<const Type *> &args() const { return args_; }
            std::string display() const override {
                std::string s = ctor_ + "<";
                for (size_t i = 0; i < args_.size(); ++i) {
                    if (i) s += ',';
                    s += args_[i] ? args_[i]->display() : "?";
                }
                return s + ">";
            }
            static bool classof(const Type *t) { return t->kind() == Kind::Generic; }
        private:
            std::string ctor_;
            std::vector<const Type *> args_;
        };
        class VectorType final : public Type {
            public:
            VectorType(const Type *elem, unsigned lanes, bool scalable = false)
            : Type(Kind::Vector), elem_(elem), lanes_(lanes), scalable_(scalable) {}

            const Type *elem() const { return elem_; }
            unsigned lanes() const { return lanes_; }
            bool scalable() const { return scalable_; }

            std::string display() const override {
                return std::string("vec<") + (scalable_ ? "vscale x " : "") +
                    std::to_string(lanes_) + " x " + elem_->display() + ">";
            }
            static bool classof(const Type *t) { return t->kind() == Kind::Vector; }

            private:
            const Type *elem_;
            unsigned lanes_;
            bool scalable_;
    };
-}
    class Typecontext {
        public:
           // Allocator region for all types
           // Can be called for all nodes to be destroyed using DestroyAll()
           // 11/3/2026 Deciding if the Allocator should use malloc ( default type ) or any other.
           // Alloc.Allocate(args) allocates pointers, to be destroyed using DestroyAll()
           Typecontext();
           

    
            llvm::BumpPtrAllocator Alloc;
            // Pre allocated member types so that no look-up is needed
            Type VoidTy, LabelTy, FloatTy, DoubleTy /*...*/;
            IntegerType Int1Ty, Int8Ty, Int16Ty, Int32Ty, Int64Ty, Int128Ty;
            [[nodiscard]] const Type* primitive(std::string_view name) const;
            [[nodiscard]] const Type* generic(std::string_view ctor, const std::vector<const Type*>& args);
            [[nodiscard]] const Type* functionType(const std::vector<const Type*>& params,
                                                    const Type* returnType);
            [[nodiscard]] const Type* defineRecord(const std::string& name,
                                                    std::vector<RecordField> fields,
                                                    const std::string& qualifier = "");
            void setRecordFlags(const Type* record, bool isFlags);
            void setRecordFlags(const Type* record, bool isFlags);
            void replaceRecordMethod(const Type* record, RecordMethod method);
            [[nodiscard]] const Type* defineAlias(const std::string& name, const Type* underlying);
            [[nodiscard]] const Type* defineGenericAlias(const std::string& name,
                                               const Type* underlying,
                                               std::vector<std::string> typeParams,
                                               std::vector<const Type*> typeConstraints);
            [[nodiscard]] const Type* defineTypeParam(const std::string& name);
            [[nodiscard]] const Type* defineModule(const std::string& name, std::vector<RecordField> fields);
            [[nodiscard]] const Type* instantiate(const Type* generic, const std::vector<const Type*>& args);
            [[nodiscard]] const FunctionInstantiation*
            instantiateFunction(const std::string& name,
                      const std::vector<std::string>& typeParams,
                      const Type* genericType,
                      const std::vector<const Type*>& args,
                      bool isMethod = false);
            [[nodiscard]] const std::vector<std::pair<const Type*, const Type*>>& instantiations() const;
            [[nodiscard]] const std::vector<FunctionInstantiation>& functionInstantiations() const;   
            [[nodiscard]] const Type* neverType() const;
            [[nodiscard]] const Type* voidType() const;
            [[nodiscard]] const Type* noneType() const;
            [[nodiscard]] const Type* anyType() const;
            [[nodiscard]] const Type* boolType() const;
            [[nodiscard]] const Type* i8Type() const;
            [[nodiscard]] const Type* i32Type() const;
            [[nodiscard]] const Type* i64Type() const;
            [[nodiscard]] const Type* f32Type() const;
            [[nodiscard]] const Type* f64Type() const;
            [[nodiscard]] const Type* ptrType(const Type* pointee);
            [[nodiscard]] const Type* listType(const Type* element, std::int64_t size = -1);
            [[nodiscard]] const Type* arrayType(const Type* element);
            [[nodiscard]] const Type* strType() const;
            [[nodiscard]] const Type* vecType(const Type* element);
             private:
            // private instanced attribute for domain pointer regions.
            llvm:SpecificBumpPtrAllocator<llvm::DomainValue> DomainValue_;
            // For call during testing, least overhead
            [[maybe_unused]] void llvm::detail::printBumpPtrAllocatorStats (unsigned NumSlabs, size_t TotalMemory);
            // On start hashes
              std::unordered_map<std::string, std::unique_ptr<Type>> interned_{};
                std::unordered_map<std::string, const Type*> primitives_{};
                std::unordered_map<std::string, const Type*> records_{};
                std::unordered_map<std::string, const Type*> aliases_{};
                std::unordered_map<std::string, const Type*> typeParams_{};
                std::vector<std::pair<const Type*, const Type*>> instantiations_{};
                std::vector<FunctionInstantiation> functionInstantiations_{};  
                //add more methods here soon




        }


    


namespace Detail {
            std::string GenericKey(std::string_view ctor, const std::vector<const Type*>& args) { 
                // Forward declaration of key to reserve the memory needed to prevent overflow
                std::string key;
                key.reserve(32 + args.size() * 16);
                // key def
                key = "G:"; 
                key += ctor; // adds the generic name "ctor"
                key += '<'; // connector
                for (std::size_t i = 0; i < /* length */args.size() ; ++i) 
                {
                    if (i != 0) {
                        key += ',';
                        }
                    key += args[i] == nullptr ? "?" : args[i]->display();
                }
                key += '>'; // deconnector
                return key;
            }
                inline void appendPtr(std::string &key, const Type *t) {
                    key += std::to_string(reinterpret_cast<std::uintptr_t>(t));
                }
                // test this on trial.
                /* auto appendPtr = [&key](const Type* t) {
                key += (t == nullptr ? "?" : t->display());
                };*/
                class TypeRegistry {
private:
    std::unordered_map<std::string, const Type*> keyMap_;
    mutable std::shared_mutex mutex_; // Protects keyMap_ and allows concurrent reads

public:
    // Takes a factory/compute function so we only compute the type if it's missing
    template <typename ComputeFunc>
    const Type* GetOrCompute(std::string_view ctor, 
                             const std::vector<const Type*>& args, 
                             ComputeFunc&& compute) 
    {
        // 1. Generate the key (done outside the lock to minimize contention)
        std::string key = Detail::GenericKey(ctor, args);

        // 2. Fast Path: Check if it exists using a Shared (Read) Lock
        {
            std::shared_lock<std::shared_mutex> readLock(mutex_);
            auto it = keyMap_.find(key);
            if (it != keyMap_.end()) {
                return it->second; // Found! Return immediately without waiting for a write lock
            }
        }

        // 3. Slow Path: Acquire Exclusive (Write) Lock to insert
            std::unique_lock<std::shared_mutex> writeLock(mutex_);

            // Double-check: Another thread might have inserted it between dropping the read lock 
            // and acquiring the write lock.
            auto it = keyMap_.find(key);
            if (it != keyMap_.end()) {
                return it->second;
            }

        // 4. Compute the new type and insert using try_emplace
            const Type* computedType = compute();
            
        // try_emplace moves the key only if insertion happens, avoiding redundant copies
            auto [insertedIt, success] = keyMap_.try_emplace(std::move(key), computedType);
            return insertedIt->second;

        //5. Use template:
        /*
        TypeRegistry registry;
        std::vector<const Type*> args = { args }; 
        const Type* myType = registry.GetOrCompute("MyClass", args, [&]() {
        return createExpensiveTypeImplementation(args);
});
        */

    }
};
}
                




        



        
        
            



    







#endif // typecontext.h