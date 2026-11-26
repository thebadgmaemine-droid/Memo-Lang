#ifndef MEMOLANG_CPP_WORKLOADS_H_
#define MEMOLANG_CPP_WORKLOADS_H_

#include <array>
#include <span>

#include "Runtime/runtime.h"

// The four benchmark programs from benchmarks/*.memo, written directly in C++
// and compiled to native code (MSVC, GCC or Clang), using the same matrix
// runtime and memory counters as MemoLang. Each comes in two versions:
//   - plain:       how C++ is usually written; every matrix is freed when its
//                  variable goes out of scope.
//   - hand-freed:  an expert frees each matrix by hand right after its last
//                  use (reset()). This is the best C++ can do, and what
//                  destructive reads do automatically.
// They make the same random matrices in the same order as the MemoLang
// versions, so they compute the same results.
//
// History: added 2026-11-12.

namespace MemoLang::cppbench {

struct Workload {
  const char *name; // Matches benchmarks/<name>.memo.
  double (*plain)(runtime::Runtime &);
  double (*handFreed)(runtime::Runtime &);
};

std::span<const Workload> workloads();

} // namespace MemoLang::cppbench

#endif // MEMOLANG_CPP_WORKLOADS_H_
