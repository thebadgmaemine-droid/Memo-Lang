// The benchmark workloads in C++ (see workloads.h). Each function mirrors
// benchmarks/<name>.memo line by line.
#include "workloads.h"

namespace MemoLang::cppbench {
namespace {

using runtime::ElementType;
using runtime::MatrixRef;
using runtime::Runtime;

constexpr int kSize = 256;
constexpr ElementType kF32 = ElementType::F32;

// matrix_sum() returns f32 in the MemoLang versions.
double sumF32(Runtime &rt, const MatrixRef &m) {
  return static_cast<double>(static_cast<float>(rt.sum(*m)));
}

// --- named_chain ------------------------------------------------------------
double namedChainPlain(Runtime &rt) {
  const int n = kSize;
  MatrixRef x = rt.random(n, n, kF32);
  MatrixRef w1 = rt.random(n, n, kF32);
  MatrixRef h1 = rt.matmul(*x, *w1);
  MatrixRef w2 = rt.random(n, n, kF32);
  MatrixRef h2 = rt.matmul(*h1, *w2);
  MatrixRef w3 = rt.random(n, n, kF32);
  MatrixRef h3 = rt.matmul(*h2, *w3);
  MatrixRef w4 = rt.random(n, n, kF32);
  MatrixRef h4 = rt.matmul(*h3, *w4);
  MatrixRef w5 = rt.random(n, n, kF32);
  MatrixRef h5 = rt.matmul(*h4, *w5);
  MatrixRef w6 = rt.random(n, n, kF32);
  MatrixRef h6 = rt.matmul(*h5, *w6);
  MatrixRef w7 = rt.random(n, n, kF32);
  MatrixRef h7 = rt.matmul(*h6, *w7);
  MatrixRef w8 = rt.random(n, n, kF32);
  MatrixRef h8 = rt.matmul(*h7, *w8);
  return sumF32(rt, h8);
} // Everything is freed here.

double namedChainHandFreed(Runtime &rt) {
  const int n = kSize;
  MatrixRef x = rt.random(n, n, kF32);
  MatrixRef w1 = rt.random(n, n, kF32);
  MatrixRef h1 = rt.matmul(*x, *w1);
  x.reset(), w1.reset();
  MatrixRef w2 = rt.random(n, n, kF32);
  MatrixRef h2 = rt.matmul(*h1, *w2);
  h1.reset(), w2.reset();
  MatrixRef w3 = rt.random(n, n, kF32);
  MatrixRef h3 = rt.matmul(*h2, *w3);
  h2.reset(), w3.reset();
  MatrixRef w4 = rt.random(n, n, kF32);
  MatrixRef h4 = rt.matmul(*h3, *w4);
  h3.reset(), w4.reset();
  MatrixRef w5 = rt.random(n, n, kF32);
  MatrixRef h5 = rt.matmul(*h4, *w5);
  h4.reset(), w5.reset();
  MatrixRef w6 = rt.random(n, n, kF32);
  MatrixRef h6 = rt.matmul(*h5, *w6);
  h5.reset(), w6.reset();
  MatrixRef w7 = rt.random(n, n, kF32);
  MatrixRef h7 = rt.matmul(*h6, *w7);
  h6.reset(), w7.reset();
  MatrixRef w8 = rt.random(n, n, kF32);
  MatrixRef h8 = rt.matmul(*h7, *w8);
  h7.reset(), w8.reset();
  return sumF32(rt, h8);
}

// --- layer_loop -------------------------------------------------------------
double layerLoopPlain(Runtime &rt) {
  const int n = kSize, layers = 16;
  MatrixRef x = rt.random(n, n, kF32);
  for (int i = 0; i < layers; ++i) {
    MatrixRef w = rt.random(n, n, kF32);
    MatrixRef h = rt.matmul(*x, *w);
    MatrixRef g = rt.random(n, n, kF32);
    x = rt.matmul(*h, *g); // The old x is freed here.
  } // w, h and g are freed here.
  return sumF32(rt, x);
}

double layerLoopHandFreed(Runtime &rt) {
  const int n = kSize, layers = 16;
  MatrixRef x = rt.random(n, n, kF32);
  for (int i = 0; i < layers; ++i) {
    MatrixRef w = rt.random(n, n, kF32);
    MatrixRef h = rt.matmul(*x, *w);
    x.reset(), w.reset();
    MatrixRef g = rt.random(n, n, kF32);
    x = rt.matmul(*h, *g);
    h.reset(), g.reset();
  }
  return sumF32(rt, x);
}

// --- power_iteration --------------------------------------------------------
// C++ can reuse m without copying it, so the hand-freed version is the same.
double powerIterationPlain(Runtime &rt) {
  const int n = kSize, steps = 16;
  MatrixRef m = rt.random(n, n, kF32);
  MatrixRef v = rt.random(n, n, kF32);
  for (int i = 0; i < steps; ++i)
    v = rt.matmul(*m, *v);
  return sumF32(rt, rt.matmul(*m, *v));
}

// --- single_expression ------------------------------------------------------
double singleExpressionPlain(Runtime &rt) {
  const int n = kSize;
  MatrixRef a = rt.random(n, n, kF32), b = rt.random(n, n, kF32),
            c = rt.random(n, n, kF32), d = rt.random(n, n, kF32),
            e = rt.random(n, n, kF32), f = rt.random(n, n, kF32),
            g = rt.random(n, n, kF32), h = rt.random(n, n, kF32);
  // Each intermediate is replaced (and freed) by the next one.
  MatrixRef r = rt.matmul(*a, *b);
  r = rt.matmul(*r, *c);
  r = rt.matmul(*r, *d);
  r = rt.matmul(*r, *e);
  r = rt.matmul(*r, *f);
  r = rt.matmul(*r, *g);
  r = rt.matmul(*r, *h);
  return sumF32(rt, r);
}

double singleExpressionHandFreed(Runtime &rt) {
  const int n = kSize;
  MatrixRef a = rt.random(n, n, kF32), b = rt.random(n, n, kF32),
            c = rt.random(n, n, kF32), d = rt.random(n, n, kF32),
            e = rt.random(n, n, kF32), f = rt.random(n, n, kF32),
            g = rt.random(n, n, kF32), h = rt.random(n, n, kF32);
  MatrixRef r = rt.matmul(*a, *b);
  a.reset(), b.reset();
  r = rt.matmul(*r, *c);
  c.reset();
  r = rt.matmul(*r, *d);
  d.reset();
  r = rt.matmul(*r, *e);
  e.reset();
  r = rt.matmul(*r, *f);
  f.reset();
  r = rt.matmul(*r, *g);
  g.reset();
  r = rt.matmul(*r, *h);
  h.reset();
  return sumF32(rt, r);
}

} // namespace

std::span<const Workload> workloads() {
  // constexpr: built at compile time, so nothing is destroyed at exit.
  static constexpr std::array<Workload, 4> all = {{
      {"layer_loop", layerLoopPlain, layerLoopHandFreed},
      {"named_chain", namedChainPlain, namedChainHandFreed},
      {"power_iteration", powerIterationPlain, powerIterationPlain},
      {"single_expression", singleExpressionPlain, singleExpressionHandFreed},
  }};
  return all;
}

} // namespace MemoLang::cppbench
