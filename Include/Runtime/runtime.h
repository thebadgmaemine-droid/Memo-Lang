#ifndef MEMOLANG_RUNTIME_H_
#define MEMOLANG_RUNTIME_H_

#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <vector>
#include "Support/Compiler.h"

// The matrix runtime: allocates, computes and frees matrices, and measures
// memory while it does. The interpreter (Interp/interpreter.h) calls it when a
// MemoLang program runs.
//
// This is the CPU version. The memory numbers (allocations, frees, peak
// memory) don't depend on the device, so they carry over to a GPU. A cuBLAS /
// CUTLASS version, for real GPU timing, needs a machine with an NVIDIA GPU.
//
// Every matrix lives in a std::shared_ptr. When the last reference goes away
// the buffer is freed and the statistics are updated, so "when is memory
// freed" is decided entirely by who holds references: the interpreter drops a
// variable's reference when the variable is consumed (destructive reads) or
// when its scope ends (the baseline).
//
// History: added 2026-11-12.

namespace MemoLang::runtime {

enum class ElementType { F32, F64 };

struct MemoryStats {
  std::uint64_t allocations = 0;
  std::uint64_t frees = 0;
  std::uint64_t liveBytes = 0;
  std::uint64_t peakBytes = 0;
  std::uint64_t liveMatrices = 0;
  std::uint64_t peakMatrices = 0;
  std::uint64_t bytesAllocated = 0; // Total over the whole run.
  double matmulMs = 0;              // Time inside matrix multiplication.
  double otherMathMs = 0;           // Time in the other matrix operations.
};

class Runtime;

class Matrix {
public:
  Matrix(std::int64_t rows, std::int64_t cols, ElementType type);
  [[nodiscard]] std::int64_t rows() const noexcept { return rows_; }
  [[nodiscard]] std::int64_t cols() const noexcept { return cols_; }
  [[nodiscard]] ElementType type() const noexcept { return type_; }
  [[nodiscard]] std::uint64_t bytes() const noexcept;
  [[nodiscard]] double at(std::int64_t row, std::int64_t col) const;
  void set(std::int64_t row, std::int64_t col, double value);

private:
  friend class Runtime;
  std::int64_t rows_;
  std::int64_t cols_;
  ElementType type_;
  std::vector<float> f32_;
  std::vector<double> f64_;
};

using MatrixRef = std::shared_ptr<Matrix>;

// Thrown for errors only visible while running (shape mismatches, ...).
struct RuntimeError {
  std::string message;
};

class Runtime {
public:
  explicit Runtime(std::uint32_t seed = 20261112);
  Runtime(const Runtime &) = delete;
  Runtime &operator=(const Runtime &) = delete;

  // New matrices.
  MatrixRef zeros(std::int64_t rows, std::int64_t cols, ElementType type);
  MatrixRef random(std::int64_t rows, std::int64_t cols, ElementType type);
  MatrixRef identity(std::int64_t n, ElementType type);
  MatrixRef copy(const Matrix &m);

  // Operations; each returns a new matrix.
  MatrixRef matmul(const Matrix &a, const Matrix &b);
  MatrixRef add(const Matrix &a, const Matrix &b, bool subtract);
  MatrixRef scale(const Matrix &m, double factor);

  [[nodiscard]] double sum(const Matrix &m) const;

  [[nodiscard]] const MemoryStats &stats() const noexcept MEMOLANG_LIFETIMEBOUND { return stats_; }

private:
  MatrixRef allocate(std::int64_t rows, std::int64_t cols, ElementType type);
  void release(Matrix *m) noexcept;

  MemoryStats stats_;
  std::mt19937 rng_;
};

// Text of a value inside an interpolated string (f"x = {x}"). The
// interpreter and compiled code (runtime_abi.h) both call these, so a program
// prints the same text either way.
//   - Integers: decimal; `isUnsigned` reads the bits as a u64.
//   - Floats: `precision` < 0 gives the shortest form with up to 10
//     significant digits, as "main() returned" prints (2.5, 0.1, 1e+20);
//     `precision` >= 0 gives that many decimals ({x:.2f} -> "2.50").
//   - Bools: "true" / "false".
[[nodiscard]] std::string formatInt(std::int64_t value, bool isUnsigned);
[[nodiscard]] std::string formatFloat(double value, int precision = -1);
[[nodiscard]] std::string formatBool(bool value);

} // namespace MemoLang::runtime

#endif // MEMOLANG_RUNTIME_H_
