# MemoLang: remaining work

Last updated: 2026-11-28. Done: source/diag, lexer, parser, AST, types, sema,
destructive-read analysis, driver, interpreter, CPU runtime, benchmarks,
codegen core (`--emit-llvm/-bc/-S/-c`, `-O0..3`, `nounwind`, data layout),
integer types `i8`/`i16`/`i32`/`i64` and `u8`/`u16`/`u32`/`u64`, macros (see 4b),
interpolated strings `f"x = {x:.2f}"`, `import`, integer range checks.

## 1. Codegen: unsupported constructs
Files: `Include/Codegen/CodegenFunction.cpp`, `CodegenModule.cpp`

- [ ] Vector / generic / function-typed values (`CodegenModule.cpp:81,97,107,183`)
- [ ] Calls through non-named callees
- [x] Missing int/float/matrix ops (2026-11-28): every operator sema accepts on ints and floats was already emitted; the one gap was matrix `==` / `!=` (sema accepted it, both backends failed). Now `Runtime::matequal` / `memolang_rt_matrix_equal`; comparing consumes neither matrix — tests `check/matrix_equal`, `run/matrix_equal`

## 2. Codegen: quality / features
- [ ] `noalias`/`nonnull` returns on runtime alloc fns; `memory(...)` on pure helpers — `CodegenModule.cpp`, `Include/Runtime/runtime_abi.h`
- [ ] Debug info (DIBuilder) + positioned backend diagnostics — `CodegenModule.cpp`, `CodegenFunction.cpp`, `Diag/BackendDiagnostics.cpp`
- [ ] Host CPU/features (`-march=native`) instead of `"generic"` — `CodegenAction.cpp:120`, `CodegenOptions.h`, `Driver/driver.cpp`
- [ ] JIT path (LLJIT; orcjit already linked) using `runtime_abi` `lastResult()`/`setEchoResult()` — `CodegenAction.{h,cpp}`, `Driver/driver.cpp`
- [ ] Use `Linker` for per-file modules (implemented, never called) — `Codegen/Linker.{h,cpp}`, `ModuleBuilder.cpp`. Not needed for `import`, which merges files at the AST level (`Driver/ModuleLoader.cpp`); only for separate compilation
- [x] Remove legacy `IRGenerator` stub (deleted `Include/IR/IRBuilder.{h,cpp}`)

## 3. Codegen tests / CI
- [ ] `MemoLangTests codegen`: compile `tests/run/*.memo` + benchmarks, result must match interpreter — `tests/runner.cpp`, `CMakeLists.txt`. Done by hand on 2026-11-28 (Windows, LLVM 23.1.6, `-c` + link with `MemoLangRuntime.lib`): 16 of 17 programs give exactly the interpreter's result, runtime errors exit 1, no leaks. The exception is `run/pointer_dangling` (see "Indirect escapes" in 4): compiled code reads the dead variable instead of stopping. `MEMOLANG_USE_LLVM` now defaults to ON when LLVM 23.1 is installed

## 4. Language / front-end gaps
- [ ] Missing-return check on non-void functions — `Semantics/sema.cpp`
- [ ] Matrix shapes in syntax (`matrix<float, R, C>`) + compile-time `@` check — `Parser/parser.cpp`, `Types/Typecontext.cpp`, `Semantics/sema.cpp`
- [ ] Byte / regex strings — `Lex/lexer.cpp`, `Lex/tokenkind.h`
else { } ` (`CaseStmt`); C-style `for (...)` gets its own error — `Parser/parser.cpp`, `AST/AST.h`; tests `ast/for_case`, `errors/for_case`
- [ ] `for` / `case` in sema (now "not supported yet"), destructive analysis, interpreter, codegen. `for` needs something to iterate (a range type or `range(n)`); `case` rules: constant arm values of the subject's type, no duplicates, no matrix subject — `Semantics/sema.cpp`, `Analysis/destructive.cpp`, `Interp/interpreter.cpp`, `Codegen/CodegenFunction.cpp`
- [ ] Imports: macros don't cross files; no namespaces/`import x as y`; in an import cycle the file reached second runs first
- [ ] Parser: `try`/`catch`/`throw`, `none` — `Parser/parser.cpp`, `AST/AST.h`
- [ ] Sema: `is`, `is not`, `in`, `not in` — `Semantics/sema.cpp`
- [ ] `LambdaExpr` — `AST/AST.h:60` (macros are expanded before parsing, so no `MacroDef` node is needed; see 4b)
- [ ] Indirect escapes (`q = &x; return q;`) need a flow pass; the interpreter catches them at run time, compiled code doesn't — `Semantics/sema.{h,cpp}`
- [ ] Null pointer literal (`none`); static locals (re-initialized every call today, so the escape check on them is correct for now)


## 5. GPU runtime (plan: `docs/gpu-runtime.md` §6)
- [ ] `~Runtime()` + `#if` blocks, CPU unchanged — `Runtime/runtime.h`
- [ ] `MEMOLANG_RUNTIME=cuda` CMake switch + throwing `runtime_cuda.cu` stub — `CMakeLists.txt`, `Include/Runtime/runtime_cuda.cu` (new)
- [ ] alloc/release, zeros, copy, random, identity (needs a GPU)
- [ ] matmul (cuBLAS gemm), add/scale (geam), sum (CUB) (needs a GPU)
- [ ] Per-op timing with sync; `membench` CPU vs CUDA memory must match — `tests/runner.cpp`
- [ ] `--version` prints the runtime; separate CUDA results file — `Driver/driver.cpp`, `docs/results/`
- [ ] Later: fused element-wise NVPTX kernels — `Include/IR/IRBuilder_GPU.{h,cpp}` (empty), `CodegenAction::prepareTarget()`
- [ ] `STL/libmat.{h,cc}` (empty) —  'Includes the algorithm discussed in dc'

## 6. Benchmarks / docs / report
- [ ] Bigger, model-like workloads + cuBLAS-only reference — `benchmarks/`, `benchmarks/cpp/`
- [ ] Update the report's "Main Source Files" table to the real file names — `submission-online-final/*.tex`
- [ ] Finish `docs/codegen-internals [ WRITE LATER ].txt`; update `CHANGELOG.md` for codegen (11-17 → 11-20)
- [ ] Link instructions incl. CUDA libs — `CMakeLists.txt` comment, this file
