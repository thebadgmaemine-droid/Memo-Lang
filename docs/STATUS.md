# MemoLang: remaining work

Last updated: 2026-11-20. Done: source/diag, lexer, parser, AST, types, sema,
destructive-read analysis, driver, interpreter, CPU runtime, benchmarks,
codegen core (`--emit-llvm/-bc/-S/-c`, `-O0..3`, `nounwind`, data layout).

## 0. Fix now (build broken)
- [ ] `#include "Codegen/BackendDiagnostics.h"` → `"Diag/BackendDiagnostics.h"`, because the file was moved to `Include/Diag/` — `Include/Codegen/CodegenAction.h:24`
- [ ] Commit the move (old `Include/Codegen/BackendDiagnostics.*` deleted, new `Include/Diag/` files untracked) + `CMakeLists.txt`
- [ ] SourceLocation ↔ LLVM 32-bit loc conversion for backend diagnostics (20-11 log) — `Include/Source/SourceLocation.h`, `Include/Diag/BackendDiagnostics.{h,cpp}`

## 1. Codegen: unsupported constructs
Files: `Include/Codegen/CodegenFunction.cpp`, `CodegenModule.cpp`
- [ ] Interpolated strings (`CodegenFunction.cpp:493`)
- [ ] Nested function definitions (`CodegenFunction.cpp:307`)
- [ ] Pointer / vector / generic / function-typed values (`CodegenModule.cpp:81,97,107,183`)
- [ ] Assignment to non-variable targets
- [ ] Calls through non-named callees
- [ ] Missing int/float/matrix ops (`CodegenFunction.cpp:691,771,802,839`)

## 2. Codegen: quality / features
- [ ] `noalias`/`nonnull` returns on runtime alloc fns; `memory(...)` on pure helpers — `CodegenModule.cpp`, `Include/Runtime/runtime_abi.h`
- [ ] Debug info (DIBuilder) + positioned backend diagnostics — `CodegenModule.cpp`, `CodegenFunction.cpp`, `Diag/BackendDiagnostics.cpp`
- [ ] Host CPU/features (`-march=native`) instead of `"generic"` — `CodegenAction.cpp:120`, `CodegenOptions.h`, `Driver/driver.cpp`
- [ ] JIT path (LLJIT; orcjit already linked) using `runtime_abi` `lastResult()`/`setEchoResult()` — `CodegenAction.{h,cpp}`, `Driver/driver.cpp`
- [ ] Use `Linker` for per-file modules (implemented, never called) — `Codegen/Linker.{h,cpp}`, `ModuleBuilder.cpp`
- [ ] Remove or finish legacy `IRGenerator` stub — `Include/IR/IRBuilder.{h,cpp}`

## 3. Codegen tests / CI
- [ ] `MemoLangTests codegen`: compile `tests/run/*.memo` + benchmarks, result must match interpreter — `tests/runner.cpp`, `CMakeLists.txt`
- [ ] CI job with `-DMEMOLANG_USE_LLVM=ON` — `.github/workflows/ci.yml`

## 4. Language / front-end gaps
- [ ] Missing-return check on non-void functions — `Semantics/sema.cpp`
- [ ] Matrix shapes in syntax (`matrix<float, R, C>`) + compile-time `@` check — `Parser/parser.cpp`, `Types/Typecontext.cpp`, `Semantics/sema.cpp`
- [ ] Lexer syntax for interpolated / byte / regex strings — `Lex/lexer.cpp`, `Lex/tokenkind.h`
- [ ] Parser: `for`, `import`, `try`/`catch`/`throw`, `case`, `none` — `Parser/parser.cpp`, `AST/AST.h`
- [ ] Sema: `is`, `is not`, `in`, `not in` — `Semantics/sema.cpp`
- [ ] `LambdaExpr`, `MacroDef` — `AST/AST.h:59`, `Include/Macro/` (notes only)
- [ ] Remove the empty `Include/AST/ASTContext.h` or fill it in

## 5. GPU runtime (plan: `docs/gpu-runtime.md` §6)
- [ ] `~Runtime()` + `#if` blocks, CPU unchanged — `Runtime/runtime.h`
- [ ] `MEMOLANG_RUNTIME=cuda` CMake switch + throwing `runtime_cuda.cu` stub — `CMakeLists.txt`, `Include/Runtime/runtime_cuda.cu` (new)
- [ ] alloc/release, zeros, copy, random, identity (needs a GPU)
- [ ] matmul (cuBLAS gemm), add/scale (geam), sum (CUB) (needs a GPU)
- [ ] Per-op timing with sync; `membench` CPU vs CUDA memory must match — `tests/runner.cpp`
- [ ] `--version` prints the runtime; separate CUDA results file — `Driver/driver.cpp`, `docs/results/`
- [ ] Later: fused element-wise NVPTX kernels — `Include/IR/IRBuilder_GPU.{h,cpp}` (empty), `CodegenAction::prepareTarget()`
- [ ] `STL/libmat.{h,cc}` (empty)

## 6. Benchmarks / docs / report
- [ ] Bigger, model-like workloads + cuBLAS-only reference — `benchmarks/`, `benchmarks/cpp/`
- [ ] Update the report's "Main Source Files" table to the real file names — `submission-online-final/*.tex`
- [ ] Credit Sere's TypeContext (check its license) — report, `Types/Typecontext.h`
- [ ] Finish `docs/codegen-internals [ WRITE LATER ].txt`; update `CHANGELOG.md` for codegen (11-17 → 11-20)
- [ ] Link instructions incl. CUDA libs — `CMakeLists.txt` comment, this file
