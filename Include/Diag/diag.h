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

// One message from any compiler stage, attached to a range of source text.
struct Diagnostic {
    Severity severity = Severity::Error;
    SourceRange range;
    std::string message;
};

}  // namespace MemoLang

#endif  // DIAG_H_
