// ---------------------------------------------------------------------------
// runtime.cpp: the CPU matrix runtime (see runtime.h)
//
// Matrices are dense, row-major. Multiplication uses the i-k-j loop order,
// which walks memory in order and is several times faster than the textbook
// i-j-k order. That is plenty for measuring memory; real speed comes from
// cuBLAS on a GPU.
//
// History: added 2026-11-12.
// ---------------------------------------------------------------------------
#include "Runtime/runtime.h"

#include <chrono>
#include <cmath>
#include <string>

namespace MemoLang::runtime {
namespace {

using Clock = std::chrono::steady_clock;

double millisecondsSince(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start)
      .count();
}

std::string shape(const Matrix &m) {
  return std::to_string(m.rows()) + "x" + std::to_string(m.cols());
}

template <typename T>
void multiply(const std::vector<T> &a, const std::vector<T> &b,
              std::vector<T> &out, std::int64_t n, std::int64_t k,
              std::int64_t m) {
  for (std::int64_t i = 0; i < n; ++i) {
    T *row = out.data() + i * m;
    for (std::int64_t p = 0; p < k; ++p) {
      const T aip = a[static_cast<std::size_t>(i * k + p)];
      const T *bRow = b.data() + p * m;
      for (std::int64_t j = 0; j < m; ++j)
        row[j] += aip * bRow[j];
    }
  }
}

} // namespace

Matrix::Matrix(std::int64_t rows, std::int64_t cols, ElementType type)
    : rows_(rows), cols_(cols), type_(type) {
  auto count = static_cast<std::size_t>(rows * cols);
  if (type == ElementType::F32)
    f32_.assign(count, 0.0f);
  else
    f64_.assign(count, 0.0);
}

std::uint64_t Matrix::bytes() const noexcept {
  auto count = static_cast<std::uint64_t>(rows_ * cols_);
  return count * (type_ == ElementType::F32 ? sizeof(float) : sizeof(double));
}

double Matrix::at(std::int64_t row, std::int64_t col) const {
  auto i = static_cast<std::size_t>(row * cols_ + col);
  return type_ == ElementType::F32 ? f32_[i] : f64_[i];
}

void Matrix::set(std::int64_t row, std::int64_t col, double value) {
  auto i = static_cast<std::size_t>(row * cols_ + col);
  if (type_ == ElementType::F32)
    f32_[i] = static_cast<float>(value);
  else
    f64_[i] = value;
}

Runtime::Runtime(std::uint32_t seed) : rng_(seed) {}

MatrixRef Runtime::allocate(std::int64_t rows, std::int64_t cols,
                            ElementType type) {
  if (rows <= 0 || cols <= 0)
    throw RuntimeError{"matrix size must be positive, got " +
                       std::to_string(rows) + "x" + std::to_string(cols)};
  MatrixRef m(new Matrix(rows, cols, type),
              [this](Matrix *p) { release(p); });
  ++stats_.allocations;
  ++stats_.liveMatrices;
  stats_.liveBytes += m->bytes();
  stats_.bytesAllocated += m->bytes();
  if (stats_.liveBytes > stats_.peakBytes)
    stats_.peakBytes = stats_.liveBytes;
  if (stats_.liveMatrices > stats_.peakMatrices)
    stats_.peakMatrices = stats_.liveMatrices;
  return m;
}

void Runtime::release(Matrix *m) noexcept {
  ++stats_.frees;
  --stats_.liveMatrices;
  stats_.liveBytes -= m->bytes();
  delete m;
}

MatrixRef Runtime::zeros(std::int64_t rows, std::int64_t cols,
                         ElementType type) {
  return allocate(rows, cols, type);
}

MatrixRef Runtime::random(std::int64_t rows, std::int64_t cols,
                          ElementType type) {
  MatrixRef m = allocate(rows, cols, type);
  // Uniform in [-a, a] with a = sqrt(3 / cols): each entry has variance
  // 1 / cols, so multiplying by such a matrix keeps values about the same size
  // (the usual neural-network initialization). Without this, long chains
  // shrink values into denormal floats, which CPUs handle very slowly and
  // which distorted the timings.
  std::uniform_real_distribution<double> value(-1.0, 1.0);
  double bound = std::sqrt(3.0 / static_cast<double>(cols));
  for (std::int64_t i = 0; i < rows; ++i)
    for (std::int64_t j = 0; j < cols; ++j)
      m->set(i, j, value(rng_) * bound);
  return m;
}

MatrixRef Runtime::identity(std::int64_t n, ElementType type) {
  MatrixRef m = allocate(n, n, type);
  for (std::int64_t i = 0; i < n; ++i)
    m->set(i, i, 1.0);
  return m;
}

MatrixRef Runtime::copy(const Matrix &m) {
  auto start = Clock::now();
  MatrixRef out = allocate(m.rows(), m.cols(), m.type());
  out->f32_ = m.f32_;
  out->f64_ = m.f64_;
  stats_.otherMathMs += millisecondsSince(start);
  return out;
}

MatrixRef Runtime::matmul(const Matrix &a, const Matrix &b) {
  if (a.cols() != b.rows())
    throw RuntimeError{"matrix shapes don't match for '@': " + shape(a) +
                       " @ " + shape(b)};
  MatrixRef out = allocate(a.rows(), b.cols(), a.type());
  auto start = Clock::now();
  if (a.type() == ElementType::F32)
    multiply(a.f32_, b.f32_, out->f32_, a.rows(), a.cols(), b.cols());
  else
    multiply(a.f64_, b.f64_, out->f64_, a.rows(), a.cols(), b.cols());
  stats_.matmulMs += millisecondsSince(start);
  return out;
}

MatrixRef Runtime::add(const Matrix &a, const Matrix &b, bool subtract) {
  if (a.rows() != b.rows() || a.cols() != b.cols())
    throw RuntimeError{"matrix shapes don't match for '" +
                       std::string(subtract ? "-" : "+") + "': " + shape(a) +
                       " and " + shape(b)};
  MatrixRef out = allocate(a.rows(), a.cols(), a.type());
  auto start = Clock::now();
  double sign = subtract ? -1.0 : 1.0;
  for (std::int64_t i = 0; i < a.rows(); ++i)
    for (std::int64_t j = 0; j < a.cols(); ++j)
      out->set(i, j, a.at(i, j) + sign * b.at(i, j));
  stats_.otherMathMs += millisecondsSince(start);
  return out;
}

MatrixRef Runtime::scale(const Matrix &m, double factor) {
  MatrixRef out = allocate(m.rows(), m.cols(), m.type());
  auto start = Clock::now();
  for (std::int64_t i = 0; i < m.rows(); ++i)
    for (std::int64_t j = 0; j < m.cols(); ++j)
      out->set(i, j, m.at(i, j) * factor);
  stats_.otherMathMs += millisecondsSince(start);
  return out;
}

double Runtime::sum(const Matrix &m) const {
  double total = 0;
  for (std::int64_t i = 0; i < m.rows(); ++i)
    for (std::int64_t j = 0; j < m.cols(); ++j)
      total += m.at(i, j);
  return total;
}

} // namespace MemoLang::runtime
