// ---------------------------------------------------------------------------
// runtime_abi.cpp: the C interface compiled programs call (runtime_abi.h)
//
// A handle owns a runtime::MatrixRef plus a reference count. The buffer is
// freed (and the statistics updated) when the count reaches zero, which is
// what the interpreter does with std::shared_ptr. Values are formatted like
// the interpreter's toText(), so `main() returned ...` lines match.
// ---------------------------------------------------------------------------
#include "Runtime/runtime_abi.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>

using MemoLang::runtime::ElementType;
using MemoLang::runtime::MatrixRef;
using MemoLang::runtime::Runtime;
using MemoLang::runtime::RuntimeError;

struct MemoMatrix {
  MatrixRef matrix;
  std::int64_t refs = 1;
};

namespace {

struct State {
  std::unique_ptr<Runtime> runtime;
  std::deque<std::string> strings; // Concatenation results; stable addresses.
  std::string lastResult;
  bool echo = true;
};

State &state() {
  static State s;
  return s;
}

Runtime &rt() {
  State &s = state();
  if (!s.runtime) // A host may call in without memolang_rt_begin().
    s.runtime = std::make_unique<Runtime>();
  return *s.runtime;
}

ElementType elementType(std::int32_t f32) {
  return f32 ? ElementType::F32 : ElementType::F64;
}

MemoMatrix *wrap(MatrixRef m) { return new MemoMatrix{std::move(m), 1}; }

const MemoLang::runtime::Matrix &get(MemoMatrix *m) {
  if (!m || !m->matrix)
    memolang_rt_panic("expected a matrix");
  return *m->matrix;
}

// Runs a runtime operation, turning its RuntimeError into a panic.
template <typename F> auto guarded(F &&f) {
  try {
    return f();
  } catch (const RuntimeError &error) {
    memolang_rt_panic(error.message.c_str());
  } 


}

void result(std::string text) {
  State &s = state();
  s.lastResult = std::move(text);
  if (s.echo) {
    std::printf("main() returned %s\n", s.lastResult.c_str());
    std::fflush(stdout);
  }
}

} // namespace

extern "C" {

void memolang_rt_begin(void) {
  State &s = state();
  s.runtime = std::make_unique<Runtime>();
  s.strings.clear();
  s.lastResult.clear();
}

int memolang_rt_end(void) {
  const auto stats = rt().stats();
  state().strings.clear();
  if (stats.frees != stats.allocations) {
    std::fprintf(stderr, "runtime error: %llu matrices were never freed\n",
                 static_cast<unsigned long long>(stats.allocations -
                                                 stats.frees));
    return 3;
  }
  return 0;
}

MemoMatrix *memolang_rt_random(std::int64_t rows, std::int64_t cols,
                               std::int32_t f32) {
  return guarded([&] { return wrap(rt().random(rows, cols, elementType(f32))); });
}

MemoMatrix *memolang_rt_zeros(std::int64_t rows, std::int64_t cols,
                              std::int32_t f32) {
  return guarded([&] { return wrap(rt().zeros(rows, cols, elementType(f32))); });
}

MemoMatrix *memolang_rt_identity(std::int64_t n, std::int32_t f32) {
  return guarded([&] { return wrap(rt().identity(n, elementType(f32))); });
}

MemoMatrix *memolang_rt_copy(MemoMatrix *m) {
  return guarded([&] { return wrap(rt().copy(get(m))); });
}

MemoMatrix *memolang_rt_matmul(MemoMatrix *a, MemoMatrix *b) {
  return guarded([&] { return wrap(rt().matmul(get(a), get(b))); });
}

MemoMatrix *memolang_rt_add(MemoMatrix *a, MemoMatrix *b,
                            std::int32_t subtract) {
  return guarded([&] { return wrap(rt().add(get(a), get(b), subtract != 0)); });
}

MemoMatrix *memolang_rt_scale(MemoMatrix *m, double factor) {
  return guarded([&] { return wrap(rt().scale(get(m), factor)); });
}

double memolang_rt_sum(MemoMatrix *m) {
  return guarded([&] { return rt().sum(get(m)); });
}

void memolang_rt_retain(MemoMatrix *m) {
  if (m)
    ++m->refs;
}

void memolang_rt_release(MemoMatrix *m) {
  if (m && --m->refs == 0)
    delete m; // Drops the MatrixRef: the runtime frees the buffer.
}

const char *memolang_rt_str_concat(const char *a, const char *b) {
  State &s = state();
  s.strings.push_back(std::string(a) + b);
  return s.strings.back().c_str();
}

std::int32_t memolang_rt_str_compare(const char *a, const char *b) {
  int c = std::strcmp(a, b);
  return c < 0 ? -1 : (c > 0 ? 1 : 0);
}

std::int64_t memolang_rt_ipow(std::int64_t base, std::int64_t exponent) {
  if (exponent < 0)
    memolang_rt_panic("negative exponent for an integer power; use a float");
  std::uint64_t result = 1;
  std::uint64_t b = static_cast<std::uint64_t>(base);
  while (exponent > 0) {
    if (exponent & 1)
      result *= b;
    b *= b;
    exponent >>= 1;
  }
  return static_cast<std::int64_t>(result);
}

std::uint64_t memolang_rt_upow(std::uint64_t base, std::uint64_t exponent) {
  std::uint64_t result = 1;
  while (exponent > 0) {
    if (exponent & 1)
      result *= base;
    base *= base;
    exponent >>= 1;
  }
  return result;
}

void memolang_rt_panic(const char *message) {
  std::fflush(stdout);
  std::fprintf(stderr, "runtime error: %s\n", message);
  std::exit(1);
}

void memolang_rt_shift_error(std::int64_t amount) {
  std::string message =
      "shift amount " + std::to_string(amount) + " is out of range";
  memolang_rt_panic(message.c_str());
}

void memolang_rt_print_int(std::int64_t value) { result(std::to_string(value)); }
void memolang_rt_print_uint(std::uint64_t value) {
  result(std::to_string(value));
}

void memolang_rt_print_float(double value) {
  char buffer[64];
  std::snprintf(buffer, sizeof buffer, "%.10g", value);
  result(buffer);
}

void memolang_rt_print_bool(std::int32_t value) {
  result(value ? "true" : "false");
}

void memolang_rt_print_str(const char *value) {
  result(std::string("\"") + value + "\"");
}

void memolang_rt_print_matrix(MemoMatrix *value) {
  if (!value || !value->matrix) {
    result("nothing");
    return;
  }
  result("matrix " + std::to_string(value->matrix->rows()) + "x" +
         std::to_string(value->matrix->cols()));
}

void memolang_rt_print_nothing(void) { result("nothing"); }

} // extern "C"

namespace MemoLang::runtime_abi {

const std::string &lastResult() { return state().lastResult; }

runtime::MemoryStats stats() { return rt().stats(); }

void setEchoResult(bool echo) { state().echo = echo; }

} // namespace MemoLang::runtime_abi
