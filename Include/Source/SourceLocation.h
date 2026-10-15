#ifndef SOURCELOCATION_H_
#define SOURCELOCATION_H_

#include <cstdint>

namespace MemoLang {

// A position in a source buffer. line and column are 1-based; 0 means unknown.
struct SourceLocation {
    std::uint32_t offset = 0;  // Byte offset from the start of the buffer.
    std::uint32_t line = 0;
    std::uint32_t column = 0;

    [[nodiscard]] bool isValid() const noexcept { return line != 0; }
};

// Half-open range [begin, end): end points one past the last character.
struct SourceRange {
    SourceLocation begin;
    SourceLocation end;

    [[nodiscard]] bool isValid() const noexcept { return begin.isValid(); }
};

}  // namespace MemoLang

#endif  // SOURCELOCATION_H_
