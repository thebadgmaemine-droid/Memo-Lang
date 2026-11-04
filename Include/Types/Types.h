#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>    
#include <vector>
#include <cassert>
#include "llvm/IR/DerivedTypes.h"

namespace MemoLang {
    enum class TypeKind {
        Primitive,
        Generic,
        Record,
        Function,
        Alias,
        TypeParam,
        Module,
        Vector,
};
    class Type {
        public:
        [[nodiscard]] TypeKind Kind() const;
        [[nodiscard]] isInteger() const; // stray ahh
        [[nodiscard]] const std::string& name() const;
        [[nodiscard]] const std::string& qualifier() const;
        [[nodiscard]] const std::vector<const Type*>& args() const;
        [[nodiscard]] const std::vector<const Type*>& paramTypes() const;
        [[nodiscard]] const Type* returnType() const;
        [[nodiscard]] std::string display() const;
        [[nodiscard]] bool isNamed(std::string_view name) const;
        [[nodiscard]] bool isStrLayout() const;
        [[nodiscard]] bool isClass() const;
        // for subclass
        [[nodiscard]] const Type* valueType() const;
        // for like integer but not really
        [[nodiscard]] bool isScalarInteger() const;
        [[nodiscard]] bool isUnsignedInteger() const;
        [[nodiscard]] bool isFloat() const;
        [[nodiscard]] int integerBitWidth() const;
        [[nodiscard]] bool isGenericCtor(std::string_view name) const;
        [[nodiscard]] const Type* genericArg(std::size_t index) const;
        // for objects
        [[nodiscard]] bool isTypeObject() const;
        [[nodiscard]] const Type* typeObjectInstance() const;
    }

}

