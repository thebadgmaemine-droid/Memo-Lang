#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/Allocator.h"
#include "llvm/IR/DerivedTypes.h"

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
            llvm::BumpPtrAllocator Alloc;
            // Pre allocated member types so that no look-up is needed
            Type VoidTy, LabelTy, FloatTy, DoubleTy /*...*/;
            IntegerType Int1Ty, Int8Ty, Int16Ty, Int32Ty, Int64Ty, Int128Ty;
            
            


        private:


        }


    


namespace Detail {
            std::string GenericKey(std::string_view ctor, const std::vector<const Type*>& args) { 
                std::string key = "G:"; 
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

                inline void appendPtr(std::string &key, const Type *t) {
                    key += std::to_string(reinterpret_cast<std::uintptr_t>(t));
                }
        }


        
        
            



        }







#endif // typecontext.h