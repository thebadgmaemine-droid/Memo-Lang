#ifndef MEMOLANG_RUNTIME_ABI_H_
#define MEMOLANG_RUNTIME_ABI_H_
// The C interface compiled MemoLang programs call (Codegen/ emits calls to
// these functions). It wraps the same runtime::Runtime the interpreter uses,
// so compiled programs allocate, compute and free matrices exactly like
// interpreted ones, and the memory statistics are comparable.
//
// Matrices are passed as opaque, reference-counted handles. Every function
// that returns a matrix returns a new reference the caller owns; every
// function that takes a matrix only borrows it. memolang_rt_release(nullptr)
// does nothing.
//
// Errors only visible at run time (shape mismatches, division by zero, ...)
// call memolang_rt_panic(), which prints "runtime error: <message>" and exits
// with status 1, as `MemoLang --run` does.
//
// History: added 2026-11-17 with code generation.
#include <cstdint>
#include <string>

#include "Runtime/runtime.h"

extern "C" {

struct MemoMatrix; // Opaque handle.

// Program start and end. memolang_rt_end() returns the process exit code:
// 0, or 3 (with a message) if matrices were never freed.
void memolang_rt_begin(void);
int memolang_rt_end(void);

// New matrices. `f32` is 1 for matrix<f32>, 0 for matrix<float> (f64).
MemoMatrix *memolang_rt_random(std::int64_t rows, std::int64_t cols,
                               std::int32_t f32);
MemoMatrix *memolang_rt_zeros(std::int64_t rows, std::int64_t cols,
                              std::int32_t f32);
MemoMatrix *memolang_rt_identity(std::int64_t n, std::int32_t f32);
MemoMatrix *memolang_rt_copy(MemoMatrix *m);

// Operations; each returns a new matrix.
MemoMatrix *memolang_rt_matmul(MemoMatrix *a, MemoMatrix *b);
MemoMatrix *memolang_rt_add(MemoMatrix *a, MemoMatrix *b,
                            std::int32_t subtract);
MemoMatrix *memolang_rt_scale(MemoMatrix *m, double factor);
double memolang_rt_sum(MemoMatrix *m);

// Reference counting.
void memolang_rt_retain(MemoMatrix *m);
void memolang_rt_release(MemoMatrix *m);

// Strings are immutable, NUL-terminated and never freed before the program
// ends (concatenation results are kept until memolang_rt_end()).
const char *memolang_rt_str_concat(const char *a, const char *b);
std::int32_t memolang_rt_str_compare(const char *a, const char *b);

// Integer power with wrap-around; panics on a negative exponent.
std::int64_t memolang_rt_ipow(std::int64_t base, std::int64_t exponent);

[[noreturn]] void memolang_rt_panic(const char *message);
[[noreturn]] void memolang_rt_shift_error(std::int64_t amount);

// Printing the value main() returned: "main() returned <value>".
void memolang_rt_print_int(std::int64_t value);
void memolang_rt_print_float(double value);
void memolang_rt_print_bool(std::int32_t value);
void memolang_rt_print_str(const char *value);
void memolang_rt_print_matrix(MemoMatrix *value);
void memolang_rt_print_nothing(void);

} // extern "C"

namespace MemoLang::runtime_abi {

// For hosts that run compiled code in-process (tests, a JIT): the text the
// last memolang_rt_print_* call produced (e.g. "20"), and the statistics of
// the current or last run.
[[nodiscard]] const std::string &lastResult();
[[nodiscard]] runtime::MemoryStats stats();
// Print "main() returned ..." to stdout (default) or only record it.
void setEchoResult(bool echo);

} // namespace MemoLang::runtime_abi

#endif // MEMOLANG_RUNTIME_ABI_H_
