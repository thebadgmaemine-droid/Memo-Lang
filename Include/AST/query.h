#ifndef QUERY_H_
#define QUERY_H_
#include <cstdint>
#include <string_view>

#include "Source/SourceLocation.h"
namespace MemoLang {
// Macro use blueprint if Macros are added ( May or may not be added )
// 2026-10-14: `_NODISCARD` (MSVC only) changed to standard [[nodiscard]],
// and SourceLocation.h included for SourceRange.
// 2026-11-12: implemented. Renamed rangecontainer -> rangeContains and
// Indentifier_Range -> identifierRange (nothing used the old names).

// True if `offset` is inside the half-open range [begin, end).
[[nodiscard]] inline bool rangeContains(SourceRange range,
                                        std::uint32_t offset) noexcept {
  return range.isValid() && range.begin.offset <= offset &&
         offset < range.end.offset;
}

// The range covered by identifier `name` starting at `start`. Identifiers
// never span lines, so the end is on the same line.
[[nodiscard]] inline SourceRange identifierRange(SourceLocation start,
                                                 std::string_view name) noexcept {
  auto length = static_cast<std::uint32_t>(name.size());
  SourceLocation end = start;
  end.offset += length;
  end.column += length;
  return {start, end};
}

} // namespace MemoLang
#endif // QUERY_H_
