#ifndef DIAG_H_
#define DIAG_H_

#include <string>
#include <string_view>

#include "Source/SourceLocation.h"
/* Check on 10/16/2026 */
// Remember to call for function afterwards
// Added warning to Diagnostic struct for warning diagnostics ( potentially very useful for a volatile language using destructive read btw)
// No other change
// 2026-11-06: removed that extra `warning` member again. Warnings already work
// through `severity`: DiagnosticEngine::warning() stores Severity::Warning
// there. The extra member shifted the fields, so diags.cpp's
// push_back({severity, range, message}) no longer compiled.

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
  // Don't cut a UTF-8 character in half: back up past continuation bytes.
  std::size_t cut = maxLength;
  while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80)
    --cut;
  return "'" + std::string(text.substr(0, cut)) + "...'";
}

// One message from any compiler stage, attached to a range of source text.
struct Diagnostic {
  Severity severity = Severity::Error;  // Note, Warning or Error.
  SourceRange range;
  std::string message;
}; // Is called using a different function.

} // namespace MemoLang

#endif // DIAG_H_
