#ifndef DIAG_H_
#define DIAG_H_

#include <string>
#include <string_view>

#include "Source/SourceLocation.h"
/* Check on 10/16/2026 */
// Remember to call for function afterwards
// Added warning to Diagnostic struct for warning diagnostics ( potentially very useful for a volatile language using destructive read btw)
// No other change

namespace MemoLang {

enum class Severity { Note, Warning, Error };

[[nodiscard]] constexpr std::string_view
severityName(Severity severity) noexcept {
  switch (severity) {
  case Severity::Note:
    return "note";
  case Severity::Warning:
    return "warning";
  case Severity::Error:
    return "error";
  }
  return "error";
}

// Quotes source text for a message, e.g. "'foo'". Text longer than maxLength
// (a 1000-digit literal, a 1 MB identifier) is cut to its start plus "...",
// so the diagnostic stays one readable line.
[[nodiscard]] inline std::string quoteForMessage(std::string_view text,
                                                 std::size_t maxLength = 32) {
  if (text.size() <= maxLength)
    return "'" + std::string(text) + "'";
  return "'" + std::string(text.substr(0, maxLength)) + "...'";
  if (text.size() == 0)
    return "Not quote";
}

// One message from any compiler stage, attached to a range of source text.
struct Diagnostic {
  Severity severity = Severity::Error;
  Severity warning = Severity::Warning;
  SourceRange range;
  std::string message;
}; // Is called using a different function.

} // namespace MemoLang

#endif // DIAG_H_
