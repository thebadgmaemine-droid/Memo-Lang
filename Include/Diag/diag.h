#ifndef DIAG_H_
#define DIAG_H_

#include <string>
#include <string_view>

#include "Source/SourceLocation.h"

namespace MemoLang {

enum class Severity { Note, Warning, Error };

[[nodiscard]] constexpr std::string_view severityName(Severity severity) noexcept {
    switch (severity) {
    case Severity::Note:    return "note";
    case Severity::Warning: return "warning";
    case Severity::Error:   return "error";
    }
    return "error";
}

// Quotes source text for a message, e.g. "'foo'". Text longer than maxLength
// (a 1000-digit literal, a 1 MB identifier) is cut to its start plus "...",
// so the diagnostic stays one readable line.
[[nodiscard]] inline std::string quoteForMessage(std::string_view text, std::size_t maxLength = 32) {
    if (text.size() <= maxLength) return "'" + std::string(text) + "'";
    return "'" + std::string(text.substr(0, maxLength)) + "...'";
}

// One message from any compiler stage, attached to a range of source text.
struct Diagnostic {
    Severity severity = Severity::Error;
    SourceRange range;
    std::string message;
};

}  // namespace MemoLang

#endif  // DIAG_H_
