# MemoLang results

All measured results in one place. Last updated: 2026-11-23 (commit b28820d).

The raw output of each saved benchmark run is next to this file:
`membench-<date>.md` (the tables as printed) and `membench.csv` (every run's
rows, for comparing runs or making charts). Those files are written by
`MemoLangTests membench --save`; this summary is written by hand.

## Summary

- **Destructive reads cut peak matrix memory in 3 of 4 workloads, by 10–82%.**
  They match C++ in which every matrix is freed by hand right after its last
  use, without the programmer freeing anything.
- **When a matrix has to be reused, they cost memory.** `power_iteration`
  needs a `copy()` in every iteration: peak memory is 33% higher and there
  are 84% more allocations (35 vs 19).
- **The MemoLang baseline uses exactly as much memory as plain C++**, so the
  comparison is fair.
- **The analysis is cheap.** It adds about 12–14% to the time needed to check
  a program.
- **No speed result yet.** CPU times vary up to 3.7× between runs of the
  same code. Speed has to be measured on a GPU, which needs the cuBLAS runtime
  and an NVIDIA GPU (see `docs/gpu-runtime.md`).
- **All 54 automated tests pass** (Release and Debug, Windows MSVC; CI also
  runs Linux GCC).

## 1. Peak matrix memory (MiB)

256×256 `f32` matrices (0.25 MiB each), median of 5 runs. The same on
2026-11-16 and 2026-11-23: memory doesn't depend on timing, so it doesn't
change between runs.

| Workload | MemoLang base | MemoLang destructive | Change | C++ plain | C++ hand-freed |
|---|---:|---:|---:|---:|---:|
| `named_chain` | 4.25 | 0.75 | **−82.4%** | 4.25 | 0.75 |
| `layer_loop` | 1.25 | 0.75 | **−40.0%** | 1.25 | 0.75 |
| `single_expression` | 2.50 | 2.25 | **−10.0%** | 2.50 | 2.25 |
| `power_iteration` | 0.75 | 1.00 | **+33.3%** | 0.75 | 0.75 |

The four ways of running each workload:

- **MemoLang base:** a matrix is freed when its variable goes out of scope or
  gets a new value, like C++ or Rust.
- **MemoLang destructive:** a matrix is freed the moment it is consumed.
- **C++ plain:** the same program in C++, compiled with MSVC, freed at the
  end of scope.
- **C++ hand-freed:** the same, but every matrix is freed by hand right after
  its last use. This is the best a careful programmer can do.

All four use the same matrix runtime and compute the same result, and every
matrix allocated is freed.

The workloads (`benchmarks/`):

- `named_chain`: straight-line code with eight named intermediate results.
- `layer_loop`: 16 iterations, two `@` each, like the layers of a network.
- `single_expression`: eight matrices made first, then multiplied in one
  expression.
- `power_iteration`: one matrix reused in every iteration.

## 2. Matrix allocations

| Workload | MemoLang base | MemoLang destructive | C++ plain | C++ hand-freed |
|---|---:|---:|---:|---:|
| `named_chain` | 17 | 17 | 17 | 17 |
| `layer_loop` | 65 | 65 | 65 | 65 |
| `single_expression` | 15 | 15 | 15 | 15 |
| `power_iteration` | 19 | **35** | 19 | 19 |

Destructive reads free memory *earlier*; they don't allocate less. The one
exception is `power_iteration`, where each `copy()` is an extra allocation.

## 3. Run time on the CPU (ms)

Median of 5 runs. These numbers are **not** a result. They are listed so
anyone can see how much they move between two runs of the same code.

| Workload | Run | MemoLang base | MemoLang destructive | C++ plain | C++ hand-freed |
|---|---|---:|---:|---:|---:|
| `named_chain` | 2026-11-16 | 26.9 | 29.3 | 29.2 | 21.3 |
| `named_chain` | 2026-11-23 | 18.2 | 19.1 | 20.5 | 17.8 |
| `layer_loop` | 2026-11-16 | 103.4 | 112.7 | 84.4 | 116.1 |
| `layer_loop` | 2026-11-23 | 76.5 | 73.2 | 78.2 | 72.6 |
| `single_expression` | 2026-11-16 | 56.4 | 58.4 | 58.7 | 61.5 |
| `single_expression` | 2026-11-23 | 15.7 | 17.7 | 17.3 | 15.1 |
| `power_iteration` | 2026-11-16 | 34.1 | 94.5 | 108.4 | 105.4 |
| `power_iteration` | 2026-11-23 | 29.1 | 38.4 | 36.3 | 34.4 |

The same code took up to 3.7× longer on one day than the other
(`single_expression`: 56–62 ms vs 15–18 ms). The differences between the
four variants are smaller than that noise, so the CPU times show no speed
difference either way.

## 4. Compile-time cost of the analysis

`MemoLangTests bench`: a generated 93,000-line test program, Release build,
median of 10 runs.

| Stage | 2026-11-12 | 2026-11-23 |
|---|---:|---:|
| Lexer + parser | 75.7 ms | 73.7 ms |
| Semantic analysis | 50.0 ms | 57.1 ms |
| Destructive-read analysis | 15.1 ms | 17.8 ms |
| Total | 141.0 ms | 145.5 ms |
| Added by the destructive-read analysis | 12.0% | 13.6% |

Semantic analysis got slower because it now also checks pointers and
unsigned integer types. The destructive-read analysis still adds only a small
part of the total.

## 5. Tests

54 automated tests (`ctest`), all passing on 2026-11-28 (36 on 2026-11-23,
when the benchmarks above were run; the new tests cover language features,
not the matrix runtime).

| Kind | Tests | What they check |
|---|---:|---|
| `ast/` | 9 | The parser builds exactly the expected tree |
| `tokens/` | 5 | The lexer produces exactly the expected tokens |
| `errors/` | 8 | Lexer and syntax errors: 76 deliberate mistakes, each reported once (including macros, `for`/`case`, matrix shapes, f-strings, imports) |
| `check/` | 10 | Semantic and destructive analysis: 82 deliberate errors (types, pointers, constants that don't fit their type, f-string values, 9 use-after-consume cases), and valid programs accepted with no errors |
| `run/` | 13 | Programs give the expected result in both modes, with no leaks: arithmetic, matrices, signed and unsigned integers, inferred return types, pointers (including null and dangling), macros, f-strings, imports, a shape mismatch caught at run time |
| `benchmarks/` | 4 | The benchmark programs give the same result in both modes, with no leaks |
| `stress` | 1 | 36 extreme inputs (100,000-deep nesting, 1 MB lines, 10,000-term chains) |
| `fuzz` | 1 | 9,000 random inputs: no crash, no hang, same result twice |
| `examples/` | 3 | The example programs through the real `MemoLang` program |

## Known limits of these results

- Everything is measured on the CPU runtime. Peak memory and allocation
  counts carry over to a GPU, because they depend only on when matrices are
  freed. Times don't.
- The workloads are small (256×256 matrices, up to 17 live at once). Larger,
  realistic models are still to be measured.
- Code generation (LLVM) isn't measured here: it needs an LLVM build, and
  this machine doesn't have LLVM.
- A matrix whose address is taken (`&m`) is never consumed early, so
  destructive reads give no saving for it. The workloads don't use pointers,
  so the numbers above aren't affected.

## Conditions and how to reproduce

Intel Core i7-1185G7 3.00 GHz, 16 GB RAM, Windows 11, MSVC 19.51.36260,
Release build.

```
cmake -S . -B build -DMEMOLANG_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release
build\Release\MemoLangTests.exe membench --save
build\Release\MemoLangTests.exe bench
```

`membench --save` adds a new `membench-<date>.md` here and appends to
`membench.csv`. Update this summary by hand afterwards.
