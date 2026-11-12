#ifndef TYPECONTEXT_H_
#define TYPECONTEXT_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Semantic types, and the TypeContext that creates and owns them.
//
// 2026-11-10: made valid C++. Removed the stray `-}` (it closed MemoLang early),
// the unmatched `#endif`, `using namespace llvm;` (it made Type, PointerType and
// VectorType ambiguous with llvm::), the by-value members of the abstract Type
// and of llvm::IntegerType, the duplicate setRecordFlags, the misspelled
// `llvm:SpecificBumpPtrAllocator<llvm::DomainValue>` and the qualified
// printBumpPtrAllocatorStats member. Renamed Typecontext -> TypeContext to match
// IR/IRBuilder.h. GenericKey is inline (it was an ODR violation) and keys on
// type identity.
//
// 2026-11-12: reworked for semantic analysis (Semantics/sema.cpp):
//  - Type::Kind now covers every type MemoLang can write or infer: Error,
//    Auto, Void, Bool, Int, Float, Str, Matrix and Function. Pointer, Generic
//    and Vector are kept for code generation.
//  - One allocator: TypeContext owns every type through std::unique_ptr in one
//    interning table. llvm::BumpPtrAllocator is gone, so types and semantic
//    analysis build and are tested without LLVM.
//  - Removed declarations that had no implementation and no syntax in the
//    language yet: records, aliases, generic aliases, type parameters,
//    modules, function instantiations and the thread-safe TypeRegistry (the
//    compiler is single-threaded). They're in git history (commit 5281e16) if
//    the language grows them.
//  - Credit: the interned-type design and the original API follow the Sere
//    compiler's TypeContext (Sere is cited in the report); classof() follows
//    LLVM's isa<>/dyn_cast<> convention.

namespace MemoLang {

// Semantic type. Instances are interned: two equal types are the same object,
// so `const Type *` can be compared by address.
class Type {
public:
  enum class Kind {
    Error,    // A type that couldn't be worked out; suppresses follow-on errors.
    Auto,     // Not inferred yet (a `def` without `-> type`).
    Void,
    Bool,
    Int,
    Float,
    Str,
    Matrix,   // matrix<element>, optionally with a known shape.
    Function,
    Pointer,  // Code generation only for now.
    Generic,  // Code generation only for now.
    Vector,   // Code generation only for now.
  };

  virtual ~Type() = default;
  Type(const Type &) = delete;
  Type &operator=(const Type &) = delete;

  [[nodiscard]] Kind kind() const noexcept { return kind_; }
  // As MemoLang writes it, for diagnostics: "int", "matrix<float>", ...
  [[nodiscard]] virtual std::string display() const = 0;

  [[nodiscard]] bool isError() const noexcept { return kind_ == Kind::Error; }
  [[nodiscard]] bool isNumeric() const noexcept {
    return kind_ == Kind::Int || kind_ == Kind::Float;
  }
  // Destructive reads: only values that own a GPU buffer are tracked by
  // consumption analysis. Today that is matrices.
  [[nodiscard]] bool isConsumable() const noexcept {
    return kind_ == Kind::Matrix;
  }

protected:
  explicit Type(Kind k) noexcept : kind_(k) {}

private:
  Kind kind_;
};

// isa<>/dyn_cast<> in the LLVM style, using each class's classof().
template <typename T> [[nodiscard]] bool isa(const Type *t) noexcept {
  return t != nullptr && T::classof(t);
}
template <typename T> [[nodiscard]] const T *dynCast(const Type *t) noexcept {
  return isa<T>(t) ? static_cast<const T *>(t) : nullptr;
}

namespace Detail {
inline std::string displayOrUnknown(const Type *t) {
  return t ? t->display() : "?";
}
} // namespace Detail

// Error, Auto, Void, Bool and Str: types with no extra data.
class BasicType final : public Type {
public:
  explicit BasicType(Kind k) noexcept : Type(k) {}
  [[nodiscard]] std::string display() const override;
  static bool classof(const Type *t) {
    switch (t->kind()) {
    case Kind::Error:
    case Kind::Auto:
    case Kind::Void:
    case Kind::Bool:
    case Kind::Str:
      return true;
    default:
      return false;
    }
  }
};

class IntType final : public Type {
public:
  IntType(unsigned bits, bool isSigned)
      : Type(Kind::Int), bits_(bits), signed_(isSigned) {}
  [[nodiscard]] unsigned bits() const noexcept { return bits_; }
  [[nodiscard]] bool isSigned() const noexcept { return signed_; }
  // "int" (i64), "byte" (u8), otherwise "i32", "u16", ...
  [[nodiscard]] std::string display() const override;
  static bool classof(const Type *t) { return t->kind() == Kind::Int; }

private:
  unsigned bits_;
  bool signed_;
};

class FloatType final : public Type {
public:
  explicit FloatType(unsigned bits) : Type(Kind::Float), bits_(bits) {}
  [[nodiscard]] unsigned bits() const noexcept { return bits_; }
  // "float" (f64) or "f32".
  [[nodiscard]] std::string display() const override;
  static bool classof(const Type *t) { return t->kind() == Kind::Float; }

private:
  unsigned bits_;
};

// matrix<element>. The shape is optional: `matrix<float>` in source has no
// shape, so rows() and cols() are empty until something gives them a value.
class MatrixType final : public Type {
public:
  MatrixType(const Type *element, std::optional<std::int64_t> rows,
             std::optional<std::int64_t> cols)
      : Type(Kind::Matrix), element_(element), rows_(rows), cols_(cols) {}
  [[nodiscard]] const Type *element() const noexcept { return element_; }
  [[nodiscard]] std::optional<std::int64_t> rows() const noexcept {
    return rows_;
  }
  [[nodiscard]] std::optional<std::int64_t> cols() const noexcept {
    return cols_;
  }
  [[nodiscard]] std::string display() const override;
  static bool classof(const Type *t) { return t->kind() == Kind::Matrix; }

private:
  const Type *element_;
  std::optional<std::int64_t> rows_;
  std::optional<std::int64_t> cols_;
};

class FunctionType final : public Type {
public:
  FunctionType(std::vector<const Type *> params, const Type *returnType)
      : Type(Kind::Function), params_(std::move(params)),
        returnType_(returnType) {}
  [[nodiscard]] const std::vector<const Type *> &params() const noexcept {
    return params_;
  }
  [[nodiscard]] const Type *returnType() const noexcept { return returnType_; }
  // "def(int, float) -> int"
  [[nodiscard]] std::string display() const override;
  static bool classof(const Type *t) { return t->kind() == Kind::Function; }

private:
  std::vector<const Type *> params_;
  const Type *returnType_;
};

class PointerType final : public Type {
public:
  explicit PointerType(const Type *pointee)
      : Type(Kind::Pointer), pointee_(pointee) {}
  [[nodiscard]] const Type *pointee() const noexcept { return pointee_; }
  [[nodiscard]] std::string display() const override {
    return Detail::displayOrUnknown(pointee_) + "*";
  }
  static bool classof(const Type *t) { return t->kind() == Kind::Pointer; }

private:
  const Type *pointee_;
};

// An instantiated generic, e.g. List<i32>.
class GenericType final : public Type {
public:
  GenericType(std::string ctor, std::vector<const Type *> args)
      : Type(Kind::Generic), ctor_(std::move(ctor)), args_(std::move(args)) {}
  [[nodiscard]] const std::string &ctor() const noexcept { return ctor_; }
  [[nodiscard]] const std::vector<const Type *> &args() const noexcept {
    return args_;
  }
  [[nodiscard]] std::string display() const override {
    std::string s = ctor_ + "<";
    for (std::size_t i = 0; i < args_.size(); ++i) {
      if (i != 0)
        s += ',';
      s += Detail::displayOrUnknown(args_[i]);
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

  [[nodiscard]] const Type *elem() const noexcept { return elem_; }
  [[nodiscard]] unsigned lanes() const noexcept { return lanes_; }
  [[nodiscard]] bool scalable() const noexcept { return scalable_; }

  [[nodiscard]] std::string display() const override {
    return std::string("vec<") + (scalable_ ? "vscale x " : "") +
           std::to_string(lanes_) + " x " + Detail::displayOrUnknown(elem_) +
           ">";
  }
  static bool classof(const Type *t) { return t->kind() == Kind::Vector; }

private:
  const Type *elem_;
  unsigned lanes_;
  bool scalable_;
};

// Creates, interns and owns every semantic type. Asking twice for the same
// type returns the same object. Types live as long as the context.
class TypeContext {
public:
  TypeContext();
  TypeContext(const TypeContext &) = delete;
  TypeContext &operator=(const TypeContext &) = delete;

  [[nodiscard]] const Type *errorType() const noexcept { return error_; }
  [[nodiscard]] const Type *autoType() const noexcept { return auto_; }
  [[nodiscard]] const Type *voidType() const noexcept { return void_; }
  [[nodiscard]] const Type *boolType() const noexcept { return bool_; }
  [[nodiscard]] const Type *strType() const noexcept { return str_; }

  // `int` is i64; `byte` is u8.
  [[nodiscard]] const IntType *intType(unsigned bits = 64,
                                       bool isSigned = true);
  [[nodiscard]] const IntType *byteType() { return intType(8, false); }
  // `float` is f64.
  [[nodiscard]] const FloatType *floatType(unsigned bits = 64);
  [[nodiscard]] const MatrixType *
  matrixType(const Type *element,
             std::optional<std::int64_t> rows = std::nullopt,
             std::optional<std::int64_t> cols = std::nullopt);
  [[nodiscard]] const FunctionType *
  functionType(const std::vector<const Type *> &params,
               const Type *returnType);
  [[nodiscard]] const PointerType *pointerType(const Type *pointee);
  [[nodiscard]] const GenericType *
  genericType(std::string_view ctor, const std::vector<const Type *> &args);
  [[nodiscard]] const VectorType *vectorType(const Type *element,
                                             unsigned lanes,
                                             bool scalable = false);

  // A built-in type name as written in source ("int", "i32", "f32", "bool",
  // "str", "void", ...). Returns null for unknown names, and for "matrix",
  // which needs an element type (see matrixType()).
  [[nodiscard]] const Type *primitive(std::string_view name);

  // How many distinct types exist (for tests).
  [[nodiscard]] std::size_t size() const noexcept { return interned_.size(); }

private:
  template <typename T, typename... Args>
  const T *intern(std::string key, Args &&...args);

  std::unordered_map<std::string, std::unique_ptr<Type>> interned_{};
  const Type *error_ = nullptr;
  const Type *auto_ = nullptr;
  const Type *void_ = nullptr;
  const Type *bool_ = nullptr;
  const Type *str_ = nullptr;
};

namespace Detail {

inline void appendPtr(std::string &key, const Type *t) {
  if (t == nullptr)
    key += '?';
  else
    key += std::to_string(reinterpret_cast<std::uintptr_t>(t));
}

// Interning key for a generic instantiation, e.g. "G:List<140234...>".
// Arguments are keyed by address, not display(): types are interned, so the
// address is their identity, while two distinct types can display the same.
inline std::string GenericKey(std::string_view ctor,
                              const std::vector<const Type *> &args) {
  std::string key;
  key.reserve(4 + ctor.size() + args.size() * 21);
  key += "G:";
  key += ctor;
  key += '<';
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (i != 0)
      key += ',';
    appendPtr(key, args[i]);
  }
  key += '>';
  return key;
}

} // namespace Detail
} // namespace MemoLang

#endif // TYPECONTEXT_H_
