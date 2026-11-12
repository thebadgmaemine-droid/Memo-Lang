// ---------------------------------------------------------------------------
// Typecontext.cpp: creates and interns semantic types
//
// Every type is built once and kept in interned_ under a key that describes it
// exactly ("I64s", "M:<element>:2x?" ...). Asking for a type that already
// exists returns the same object, so types can be compared by address.
// Component types inside a key are written as their address, since they are
// interned too.
//
// History: added 2026-11-12 (Typecontext.h had only declarations).
// ---------------------------------------------------------------------------
#include "Types/Typecontext.h"

#include <cassert>
#include <utility>

namespace MemoLang {
namespace {

void appendShape(std::string &key, std::optional<std::int64_t> n) {
  if (n)
    key += std::to_string(*n);
  else
    key += '?';
}

} // namespace

std::string BasicType::display() const {
  switch (kind()) {
  case Kind::Error:
    return "<error>";
  case Kind::Auto:
    return "auto";
  case Kind::Void:
    return "void";
  case Kind::Bool:
    return "bool";
  case Kind::Str:
    return "str";
  default:
    return "?";
  }
}

std::string IntType::display() const {
  if (bits_ == 64 && signed_)
    return "int";
  if (bits_ == 8 && !signed_)
    return "byte";
  return (signed_ ? "i" : "u") + std::to_string(bits_);
}

std::string FloatType::display() const {
  return bits_ == 64 ? "float" : "f" + std::to_string(bits_);
}

std::string MatrixType::display() const {
  std::string text = "matrix<" + Detail::displayOrUnknown(element_) + ">";
  if (rows_ || cols_) {
    text += '[';
    text += rows_ ? std::to_string(*rows_) : "?";
    text += 'x';
    text += cols_ ? std::to_string(*cols_) : "?";
    text += ']';
  }
  return text;
}

std::string FunctionType::display() const {
  std::string text = "def(";
  for (std::size_t i = 0; i < params_.size(); ++i) {
    if (i != 0)
      text += ", ";
    text += Detail::displayOrUnknown(params_[i]);
  }
  return text + ") -> " + Detail::displayOrUnknown(returnType_);
}

template <typename T, typename... Args>
const T *TypeContext::intern(std::string key, Args &&...args) {
  auto it = interned_.find(key);
  if (it != interned_.end())
    return static_cast<const T *>(it->second.get());
  auto type = std::make_unique<T>(std::forward<Args>(args)...);
  const T *result = type.get();
  interned_.emplace(std::move(key), std::move(type));
  return result;
}

TypeContext::TypeContext() {
  error_ = intern<BasicType>("Error", Type::Kind::Error);
  auto_ = intern<BasicType>("Auto", Type::Kind::Auto);
  void_ = intern<BasicType>("Void", Type::Kind::Void);
  bool_ = intern<BasicType>("Bool", Type::Kind::Bool);
  str_ = intern<BasicType>("Str", Type::Kind::Str);
}

const IntType *TypeContext::intType(unsigned bits, bool isSigned) {
  return intern<IntType>("I" + std::to_string(bits) + (isSigned ? "s" : "u"),
                         bits, isSigned);
}

const FloatType *TypeContext::floatType(unsigned bits) {
  return intern<FloatType>("F" + std::to_string(bits), bits);
}

const MatrixType *TypeContext::matrixType(const Type *element,
                                          std::optional<std::int64_t> rows,
                                          std::optional<std::int64_t> cols) {
  std::string key = "M:";
  Detail::appendPtr(key, element);
  key += ':';
  appendShape(key, rows);
  key += 'x';
  appendShape(key, cols);
  return intern<MatrixType>(std::move(key), element, rows, cols);
}

const FunctionType *
TypeContext::functionType(const std::vector<const Type *> &params,
                          const Type *returnType) {
  std::string key = "Fn(";
  for (std::size_t i = 0; i < params.size(); ++i) {
    if (i != 0)
      key += ',';
    Detail::appendPtr(key, params[i]);
  }
  key += ")->";
  Detail::appendPtr(key, returnType);
  return intern<FunctionType>(std::move(key), params, returnType);
}

const PointerType *TypeContext::pointerType(const Type *pointee) {
  std::string key = "P:";
  Detail::appendPtr(key, pointee);
  return intern<PointerType>(std::move(key), pointee);
}

const GenericType *
TypeContext::genericType(std::string_view ctor,
                         const std::vector<const Type *> &args) {
  return intern<GenericType>(Detail::GenericKey(ctor, args), std::string(ctor),
                             args);
}

const VectorType *TypeContext::vectorType(const Type *element, unsigned lanes,
                                          bool scalable) {
  std::string key = "V:";
  Detail::appendPtr(key, element);
  key += ':' + std::to_string(lanes) + (scalable ? "s" : "");
  return intern<VectorType>(std::move(key), element, lanes, scalable);
}

const Type *TypeContext::primitive(std::string_view name) {
  if (name == "int" || name == "i64")
    return intType(64, true);
  if (name == "i32")
    return intType(32, true);
  if (name == "byte")
    return byteType();
  if (name == "float" || name == "f64")
    return floatType(64);
  if (name == "f32")
    return floatType(32);
  if (name == "bool")
    return bool_;
  if (name == "str")
    return str_;
  if (name == "void")
    return void_;
  if (name == "auto")
    return auto_;
  return nullptr;
}

} // namespace MemoLang
