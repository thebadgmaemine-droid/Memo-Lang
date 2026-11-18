# CUDA matrix runtime and the `MEMOLANG_RUNTIME` switch (design draft)

Status: draft, 2026-11-18. Nothing here is implemented yet.
Owner: whoever picks it up; update `docs/STATUS.md` when work starts.

## 1. Goal

Run MemoLang matrix operations on an NVIDIA GPU (cuBLAS) instead of the CPU,
without changing the language, the analysis, the interpreter or code
generation.

`runtime.h` already describes the CPU runtime as a stand-in: "A cuBLAS /
CUTLASS version, for real GPU timing, needs a machine with an NVIDIA GPU."
This document is the plan for that version.

What we want out of it:

- Real GPU timings for `--run`, `--compare` and `membench`.
- Memory statistics that match the CPU build *exactly*, so the
  destructive-read results are comparable across devices.
- Compiled programs (`MemoLang -c`) that use the GPU just by linking a
  different `libMemoLangRuntime.a`.

Not in scope here: generating GPU kernels from MemoLang code (NVPTX/AMDGPU
codegen, the empty `IR/IRBuilder_GPU.*`). That is the later step that fuses
element-wise chains; see section 9.

## 2. What stays the same

The GPU is selected at **build time**: one build is either CPU or CUDA, never
both. So:

| Component | Changes? |
|---|---|
| `Analysis/destructive.*` | No. |
| `Interp/interpreter.*` | No. It already calls only the public `Runtime` API. |
| `Codegen/*` | No. Generated code calls `memolang_rt_*`, which is unchanged. |
| `Runtime/runtime_abi.*` | No. It wraps `Runtime` and never touches matrix elements. |
| `Runtime/runtime.h` | Public API unchanged; private members change (section 3.1). |
| `Runtime/runtime.cpp` | Gains a destructor definition; otherwise unchanged. |
| `Runtime/runtime_cuda.cu` | **New.** The CUDA implementation of `runtime.h`. |
| `CMakeLists.txt` | New `MEMOLANG_RUNTIME` option (section 5). |

Element access stays inside the runtime: `Matrix::at()`/`set()` are only
called from `runtime.cpp` itself (`random`, `identity`, `add`, `scale`,
`sum`). `print_matrix` prints only the shape. That is what makes a GPU
version a contained change.

## 3. Design

### 3.1 `runtime.h` private members

The header is included by plain C++ files that `nvcc` never sees, so it must
not include any CUDA header. CUDA state is kept behind opaque pointers.

```cpp
class Matrix {
public:
  // ... unchanged ...
private:
  friend class Runtime;
  std::int64_t rows_;
  std::int64_t cols_;
  ElementType type_;
#if MEMOLANG_RUNTIME_CUDA
  void *device_ = nullptr; // bytes() bytes of device memory, row-major.
#else
  std::vector<float> f32_;
  std::vector<double> f64_;
#endif
};

class Runtime {
public:
  explicit Runtime(std::uint32_t seed = 20261112);
  ~Runtime(); // NEW: defined in the .cpp/.cu, where CudaState is complete.
  // ... unchanged ...
private:
  MatrixRef allocate(std::int64_t rows, std::int64_t cols, ElementType type);
  void release(Matrix *m) noexcept;

  MemoryStats stats_;
  std::mt19937 rng_;
#if MEMOLANG_RUNTIME_CUDA
  struct CudaState;                 // stream + cuBLAS handle; runtime_cuda.cu
  std::unique_ptr<CudaState> cuda_;
#endif
};
```

`runtime.cpp` then needs `Runtime::~Runtime() = default;`.

**Important:** `MEMOLANG_RUNTIME_CUDA` changes the layout of `Matrix` and
`Runtime`, so it must be defined the same way in every file that includes
`runtime.h`. The CMake switch makes it a `PUBLIC` compile definition of
`MemoLangRuntime` for that reason (section 5). If it ever ends up `PRIVATE`,
the program breaks silently at run time (an ODR violation).

In the CUDA build, `Matrix::at()`/`set()` still exist but copy one element
to or from the device. They are correct but slow, and the CUDA runtime
itself does not use them.

### 3.2 `CudaState`

```cpp
struct Runtime::CudaState {
  cudaStream_t stream = nullptr;
  cublasHandle_t blas = nullptr;
  void *scratch = nullptr;        // CUB temporary storage for sum()
  std::size_t scratchBytes = 0;
};
```

Created in the constructor (`cudaStreamCreate`, `cublasCreate`,
`cublasSetStream`) and destroyed in `~Runtime()`. All work goes on that one
stream, so operations stay in program order with no extra synchronization.

### 3.3 Memory: allocate and release

- `allocate()`: same shape check and same `stats_` bookkeeping as the CPU
  version, then `cudaMallocAsync(&m->device_, m->bytes(), stream)`.
- `release()`: same bookkeeping, then `cudaFreeAsync(m->device_, stream)`
  and `delete m`.

The statistics count *logical* bytes, exactly as now, so `allocations`,
`frees`, `peakBytes` and the rest are identical between CPU and CUDA builds.
That identity is the test (section 7).

`cudaMallocAsync` uses the device's stream-ordered memory pool, so a buffer
freed by a consumed matrix is immediately reusable by the next allocation
on the stream. That is the GPU behaviour the destructive-read analysis is
meant to exploit. It is worth also reporting what the pool actually held, as
an optional extra field (`devicePeakBytes`, read from
`cudaMemPoolAttrUsedMemHigh`). Keep it out of the CPU-vs-CUDA comparison.

`release()` is `noexcept`. If `cudaFreeAsync` fails, print the CUDA error
and `std::abort()`; it can't throw.

### 3.4 Operations

All buffers are **row-major**, as on the CPU. cuBLAS is column-major, so a
row-major `r x c` buffer looks like a column-major `c x r` matrix (its
transpose). Each operation below takes that into account.

| `Runtime` method | CUDA implementation |
|---|---|
| `zeros` | `allocate` + `cudaMemsetAsync(…, 0, bytes)` |
| `random` | Fill a host buffer with the **same** `mt19937` code as the CPU, then `cudaMemcpyAsync` H2D. Inputs are then bit-identical across builds. Don't use cuRAND. |
| `identity` | Host buffer, then H2D copy (or a tiny kernel; not worth it yet). |
| `copy` | `cudaMemcpyAsync` D2D. Counted in `otherMathMs`, as now. |
| `matmul` | `cublasSgemm` / `cublasDgemm`, see below. Counted in `matmulMs`. |
| `add` | `cublasSgeam` / `cublasDgeam`: `out = 1·a + (±1)·b`. |
| `scale` | `cublas?geam`: `out = factor·m + 0·m`. |
| `sum` | `cub::DeviceReduce::Sum` into a device `double`, then copy back. For f32, read through a transform iterator that converts to `double`, as the CPU loop does. |

**matmul with row-major buffers.** We want `C = A·B` with `A` `n x k`,
`B` `k x m`, `C` `n x m`. Column-major cuBLAS sees our buffers as `Aᵀ`,
`Bᵀ`, `Cᵀ`, and `Cᵀ = Bᵀ·Aᵀ`, so swap the operands and don't transpose:

```cpp
// Cᵀ (m x n) = Bᵀ (m x k) · Aᵀ (k x n), all column-major.
cublasDgemm(blas, CUBLAS_OP_N, CUBLAS_OP_N,
            /*m=*/m, /*n=*/n, /*k=*/k,
            &one,  b, /*ldb=*/m,
                   a, /*lda=*/k,
            &zero, c, /*ldc=*/m);
```

**geam** (`add`/`scale`) is element-wise, so the layout doesn't matter: pass
`m = cols`, `n = rows`, every leading dimension `cols`.

**Dimension limits.** The classic cuBLAS API takes `int`. Check
`rows`, `cols` and `rows*cols` against `INT_MAX` and throw a `RuntimeError`
if they are too large. The CUDA 12 `_64` entry points (`cublasDgemm_64`, ...)
remove the limit, so use them if the toolkit minimum is CUDA 12.

**Element types.** `ElementType::F32` → `S` routines, `F64` → `D`.
Operations use `a.type()` like the CPU version (sema already rejects mixed
element types).

### 3.5 Errors

- Shape checks run on the host **before** any launch, with the same messages
  as `runtime.cpp`, so `tests/run/shape_mismatch.memo` passes unchanged.
- A failed CUDA or cuBLAS call throws
  `RuntimeError{"CUDA: <call>: <cudaGetErrorString>"}`. `runtime_abi`'s
  `guarded()` already turns that into `memolang_rt_panic`, and the
  interpreter reports it the same way.
- One `CUDA_CHECK(expr)` / `CUBLAS_CHECK(expr)` macro pair at the top of
  `runtime_cuda.cu` handles both.
- Errors from asynchronous work show up at the next synchronizing call.
  Since every timed operation synchronizes (3.6), that is the same
  operation in practice.

### 3.6 Timing

GPU calls return before the work finishes, so the current pattern
(`start = now(); op(); ms += since(start)`) would only measure the launch.
For the first version, **synchronize the stream at the end of every timed
operation** (`cudaStreamSynchronize`) before reading the clock. This is
simple, and correct for a tool whose purpose is measurement.

Later, if the synchronizations show up in profiles: record a `cudaEvent`
pair per operation, keep a list of the pending pairs, and add up their
`cudaEventElapsedTime` when `stats()` is read (`stats()` is `const`, so the
pending list must be `mutable`).

### 3.7 Lifetime

Every `MatrixRef`'s deleter captures `this` (the `Runtime`). That is already
true on the CPU, but on the GPU destroying the `Runtime` first also destroys
the stream that the deleters free memory on. `memolang_rt_end()` and the
interpreter already drop every matrix before the runtime goes away. Add an
`assert(stats_.liveMatrices == 0)` to `~Runtime()` in the CUDA build to
catch any change that breaks this.

## 4. Files

```
Include/Runtime/runtime.h          private members + ~Runtime()     (edit)
Include/Runtime/runtime.cpp        Runtime::~Runtime() = default;   (edit)
Include/Runtime/runtime_cuda.cu    CUDA implementation              (new)
CMakeLists.txt                     MEMOLANG_RUNTIME switch          (edit)
docs/STATUS.md                     build instructions for CUDA      (edit)
```

`runtime_cuda.cu` replaces `runtime.cpp` in the CUDA build; the two are never
compiled together. Anything they could share (the `random` fill, the
`shape()` message helper) can move to a small `runtime_common.h` once both
exist. Don't do that before then.

## 5. CMake switch

Replace the current `MemoLangRuntime` block with:

```cmake
# Which device runs matrix operations. "cpu" (default) builds everywhere;
# "cuda" needs the CUDA toolkit (nvcc, cuBLAS) and, to run, an NVIDIA GPU.
#   cmake -S . -B build-cuda -DMEMOLANG_RUNTIME=cuda
set(MEMOLANG_RUNTIME "cpu" CACHE STRING "Matrix runtime: cpu or cuda")
set_property(CACHE MEMOLANG_RUNTIME PROPERTY STRINGS cpu cuda)

if(MEMOLANG_RUNTIME STREQUAL "cuda")
  # Must be set before enable_language(CUDA). "native" needs a GPU at
  # configure time, so list common architectures instead; override with
  # -DCMAKE_CUDA_ARCHITECTURES=86 for a faster build.
  if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
    set(CMAKE_CUDA_ARCHITECTURES 75 80 86 89 90)
  endif()
  enable_language(CUDA)
  find_package(CUDAToolkit 12.0 REQUIRED)
  set(CMAKE_CUDA_STANDARD 20)
  set(CMAKE_CUDA_STANDARD_REQUIRED ON)

  add_library(MemoLangRuntime STATIC
      Include/Runtime/runtime_cuda.cu
      Include/Runtime/runtime_abi.cpp
  )
  target_link_libraries(MemoLangRuntime PUBLIC CUDA::cudart CUDA::cublas)
  # PUBLIC: it changes the layout of Matrix and Runtime (docs/gpu-runtime.md 3.1).
  target_compile_definitions(MemoLangRuntime PUBLIC MEMOLANG_RUNTIME_CUDA=1)
elseif(MEMOLANG_RUNTIME STREQUAL "cpu")
  add_library(MemoLangRuntime STATIC
      Include/Runtime/runtime.cpp
      Include/Runtime/runtime_abi.cpp
  )
else()
  message(FATAL_ERROR "MEMOLANG_RUNTIME must be cpu or cuda, got '${MEMOLANG_RUNTIME}'")
endif()
target_include_directories(MemoLangRuntime PUBLIC ${MEMOLANG_INCLUDE_DIRS})
message(STATUS "MemoLang matrix runtime: ${MEMOLANG_RUNTIME}")
```

The warnings loop at the end of the file must not hand `-Wall -Wextra` or
`/W4` to `nvcc`, which rejects them. Limit them to C++ sources:

```cmake
foreach(target MemoLangRuntime MemoLangFrontend MemoLang)
  if(MSVC)
    target_compile_options(${target} PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:/W4 /permissive- /utf-8>)
  else()
    target_compile_options(${target} PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra>)
  endif()
endforeach()
```

(For warnings in the `.cu` file, add
`$<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=-Wall,-Wextra>` later.)

Notes:

- `MEMOLANG_RUNTIME` is independent of `MEMOLANG_USE_LLVM`. Any of the four
  combinations should configure.
- CI and machines without the toolkit keep the default `cpu`. Nothing
  changes for them.
- Linking a compiled program by hand now needs the CUDA libraries too,
  because a static library doesn't carry its dependencies:
  `c++ prog.o libMemoLangRuntime.a -lcudart -lcublas -o prog`
  (plus `-L$CUDA_HOME/lib64`). Document this next to the existing command
  in the `CMakeLists.txt` comment and in `STATUS.md`.
- Optional: have `MemoLang --version` print the runtime (`runtime: cuda`)
  so benchmark results can't be mixed up. A `MEMOLANG_RUNTIME_NAME`
  definition on `MemoLangFrontend` is enough.

## 6. Order of work

1. **Header refactor, CPU only.** Add `~Runtime()` and the `#if` blocks to
   `runtime.h`, with nothing defined, so the CPU build is unchanged. Run
   `ctest`; everything should still pass.
2. **CMake switch** with a `runtime_cuda.cu` that compiles but throws
   `RuntimeError{"not implemented"}` from every operation. This step works
   **without a GPU**: it only needs the toolkit installed, so the build can
   be checked on any machine.
3. **Allocate/release, zeros, copy, random, identity.** At this point the
   memory statistics should already match the CPU build on every
   benchmark.
4. **matmul** (cuBLAS gemm), then **add/scale** (geam), then **sum** (CUB).
5. **Timing** with per-operation synchronization (3.6).
6. Update `STATUS.md`, the `CMakeLists.txt` comments, and section 12 of
   the codegen internals doc.

Steps 3 to 5 need a machine with an NVIDIA GPU.

## 7. Testing

- **Same tests, both builds.** Every existing `ctest` test must pass in a
  `-DMEMOLANG_RUNTIME=cuda` build. Nothing new is needed for correctness at
  first, because the test programs don't know which device they run on.
- **Memory statistics must be identical.** Run
  `MemoLangTests membench` in both builds and compare the memory columns
  (allocations, frees, peak bytes, peak matrices): they must match exactly.
  Only the time columns may differ. A small script, or a
  `membench --compare-with <file>` mode, can check this automatically.
- **Floating-point sums.** On the GPU, gemm and `sum()` add in a different
  order, so results differ from the CPU in the last digits.
  `tests/run/matrices.memo` already compares with `close()`, so it is fine.
  The benchmarks (`benchmarks/*.memo`) return `matrix_sum(...)` from
  `main()`, so their printed values will differ between CPU and CUDA builds.
  That's expected. Their tests only require destructive and baseline runs
  to agree *within one build*, and they will, because cuBLAS gives the same
  result every time on a single stream. Never compare printed results
  across the two builds.
- **Leaks.** The existing check (exit status 3 when matrices are never
  freed) still applies. Also run once under `compute-sanitizer` to catch
  device-side errors and leaked device memory.

## 8. Open questions

1. **Minimum toolkit version.** CUDA 12 (for the `_64` cuBLAS API and a
   stable `cudaMallocAsync`) is suggested. Is that available on the GPU
   machine?
2. **Which GPU** will produce the published numbers? Its architecture
   should be in the default `CMAKE_CUDA_ARCHITECTURES` list.
3. **In-place reuse.** When an operation consumes its operand (e.g.
   `m * 2.0` where `m` is never read again), the result could reuse the
   operand's buffer instead of allocating. The runtime can't see that today
   because its methods take `const Matrix&`. Supporting it (e.g.
   `scale(MatrixRef m, ...)` reusing `m` when `use_count() == 1`) would
   change the statistics on **both** devices, so it's a language/analysis
   decision, not part of this port.
4. **Results directory.** Should `membench --save` write CUDA results to
   a separate file name in `docs/results/` so CPU and GPU runs are never
   mixed?

## 9. After this: generated GPU kernels

Once the CUDA runtime works, the next step is fusing element-wise chains
(`m * 2.0 + n`) into one generated kernel instead of a temporary matrix per
operation. That means an NVPTX module built by `IR/IRBuilder_GPU.*`, the
NVPTX target initialized in `CodegenAction::prepareTarget()` (which only
initializes the native target now), a GPU model (`sm_XX`) option instead of
the hardcoded `"generic"` CPU, and a new `memolang_rt_launch` entry point that
loads the PTX with the CUDA driver API. It builds on this runtime's stream
and memory pool, so it comes second. It needs its own design doc.
