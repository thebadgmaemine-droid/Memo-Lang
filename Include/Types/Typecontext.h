#ifndef TYPECONTEXT_H_
#define TYPECONTEXT_H_

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "llvm/Support/Allocator.h"

// 2026-11-10: made valid C++. Removed the stray `-}` (it closed MemoLang early),
// the unmatched `#endif`, `using namespace llvm;` (it made Type, PointerType and
// VectorType ambiguous with llvm::), the by-value members of the abstract Type
// and of llvm::IntegerType, the duplicate setRecordFlags, the misspelled
// `llvm:SpecificBumpPtrAllocator<llvm::DomainValue>` and the qualified
// printBumpPtrAllocatorStats member. Renamed Typecontext -> TypeContext to match
// IR/IRBuilder.h. Declared RecordField, RecordMethod and FunctionInstantiation.
// GenericKey is inline (it was an ODR violation) and keys on type identity.
// TypeRegistry no longer runs compute() under its lock (nested generics
// deadlocked).

namespace MemoLang {

// Semantic type. Instances are interned: two equal types are the same object,
// so `const Type *` can be compared by address.
class Type {
public:
  // Discriminator for LLVM-style isa<>/dyn_cast<> (see each classof()).
  enum class Kind { Int, Pointer, Generic, Vector };

  virtual ~Type() = default;
  Type(const Type &) = delete;
  Type &operator=(const Type &) = delete;

  [[nodiscard]] Kind kind() const noexcept { return kind_; }
  // For diagnostics.
  [[nodiscard]] virtual std::string display() const = 0;

protected:
  explicit Type(Kind k) noexcept : kind_(k) {}

private:
  Kind kind_;
};

namespace Detail {
inline std::string displayOrUnknown(const Type *t) {
  return t ? t->display() : "?";
}
} // namespace Detail

class IntType final : public Type {
public:
  IntType(unsigned bits, bool isSigned)
      : Type(Kind::Int), bits_(bits), signed_(isSigned) {}
  [[nodiscard]] unsigned bits() const noexcept { return bits_; }
  [[nodiscard]] bool isSigned() const noexcept { return signed_; }
  [[nodiscard]] std::string display() const override {
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

// A named field of a record or module.
struct RecordField {
  std::string name;
  const Type *type = nullptr;
};

// A method attached to a record; `type` is its function type.
struct RecordMethod {
  std::string name;
  const Type *type = nullptr;
};

// One concrete instantiation of a generic function.
struct FunctionInstantiation {
  std::string name;
  std::vector<const Type *> typeArgs;
  const Type *type = nullptr; // The instantiated function type.
  bool isMethod = false;
};

// Owns every semantic type and interns them.
class TypeContext {
public:
  TypeContext();
  TypeContext(const TypeContext &) = delete;
  TypeContext &operator=(const TypeContext &) = delete;

  // Arena for type nodes. 11/3/2026: still deciding between this and malloc.
  // Memory is released when the context is destroyed (or Alloc.Reset()).
  llvm::BumpPtrAllocator Alloc;

  [[nodiscard]] const Type *primitive(std::string_view name) const;
  [[nodiscard]] const Type *generic(std::string_view ctor,
                                    const std::vector<const Type *> &args);
  [[nodiscard]] const Type *
  functionType(const std::vector<const Type *> &params,
               const Type *returnType);
  [[nodiscard]] const Type *defineRecord(const std::string &name,
                                         std::vector<RecordField> fields,
                                         const std::string &qualifier = "");
  void setRecordFlags(const Type *record, bool isFlags);
  void replaceRecordMethod(const Type *record, RecordMethod method);
  [[nodiscard]] const Type *defineAlias(const std::string &name,
                                        const Type *underlying);
  [[nodiscard]] const Type *
  defineGenericAlias(const std::string &name, const Type *underlying,
                     std::vector<std::string> typeParams,
                     std::vector<const Type *> typeConstraints);
  [[nodiscard]] const Type *defineTypeParam(const std::string &name);
  [[nodiscard]] const Type *defineModule(const std::string &name,
                                         std::vector<RecordField> fields);
  [[nodiscard]] const Type *instantiate(const Type *generic,
                                        const std::vector<const Type *> &args);
  [[nodiscard]] const FunctionInstantiation *
  instantiateFunction(const std::string &name,
                      const std::vector<std::string> &typeParams,
                      const Type *genericType,
                      const std::vector<const Type *> &args,
                      bool isMethod = false);
  [[nodiscard]] const std::vector<std::pair<const Type *, const Type *>> &
  instantiations() const;
  [[nodiscard]] const std::vector<FunctionInstantiation> &
  functionInstantiations() const;

  [[nodiscard]] const Type *neverType() const;
  [[nodiscard]] const Type *voidType() const;
  [[nodiscard]] const Type *noneType() const;
  [[nodiscard]] const Type *anyType() const;
  [[nodiscard]] const Type *boolType() const;
  [[nodiscard]] const Type *i8Type() const;
  [[nodiscard]] const Type *i32Type() const;
  [[nodiscard]] const Type *i64Type() const;
  [[nodiscard]] const Type *f32Type() const;
  [[nodiscard]] const Type *f64Type() const;
  [[nodiscard]] const Type *strType() const;
  [[nodiscard]] const Type *ptrType(const Type *pointee);
  [[nodiscard]] const Type *listType(const Type *element,
                                     std::int64_t size = -1);
  [[nodiscard]] const Type *arrayType(const Type *element);
  [[nodiscard]] const Type *vecType(const Type *element);

private:
  std::unordered_map<std::string, std::unique_ptr<Type>> interned_{};
  std::unordered_map<std::string, const Type *> primitives_{};
  std::unordered_map<std::string, const Type *> records_{};
  std::unordered_map<std::string, const Type *> aliases_{};
  std::unordered_map<std::string, const Type *> typeParams_{};
  std::vector<std::pair<const Type *, const Type *>> instantiations_{};
  std::vector<FunctionInstantiation> functionInstantiations_{};
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
// address is their identity, while two distinct types (records with the same
// name in different modules, type params) can display the same.
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

// Thread-safe cache from generic key to type.
class TypeRegistry {
public:
  // Returns the cached type for (ctor, args), or calls compute() and caches
  // its result. compute() runs without the lock held, so it may itself call
  // GetOrCompute (nested generics such as List<List<i32>>). If two threads
  // race, both may compute; the first insert wins and both get that one, so
  // compute() must not hand out ownership (allocate from the TypeContext).
  // A null result is returned but not cached.
  template <typename ComputeFunc>
  const Type *GetOrCompute(std::string_view ctor,
                           const std::vector<const Type *> &args,
                           ComputeFunc &&compute) {
    std::string key = GenericKey(ctor, args);
    {
      std::shared_lock<std::shared_mutex> readLock(mutex_);
      auto it = keyMap_.find(key);
      if (it != keyMap_.end())
        return it->second;
    }

    const Type *computed = std::forward<ComputeFunc>(compute)();
    if (computed == nullptr)
      return nullptr;

    std::unique_lock<std::shared_mutex> writeLock(mutex_);
    auto [it, inserted] = keyMap_.try_emplace(std::move(key), computed);
    return it->second;
  }

private:
  std::unordered_map<std::string, const Type *> keyMap_;
  mutable std::shared_mutex mutex_; // Guards keyMap_; readers share it.
};

} // namespace Detail
} // namespace MemoLang

#endif // TYPECONTEXT_H_
