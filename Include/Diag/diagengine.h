#ifndef DIAGENGINE_H_
#define DIAGENGINE_H_

#include <cstddef>
#include <iosfwd>
#include <string>
#include <utility>
#include <vector>

#include "Diag/diag.h"

namespace MemoLang {

class SourceManager;

// Collects diagnostics from every stage and prints each one as it arrives:
//
//   examples/errors.memo:6:18: error: expected ';' after variable declaration
//    6 |     int b = a * 2
//      |                  ^
class DiagnosticEngine {
public:
    DiagnosticEngine(const SourceManager& sources, std::ostream& out);

    void report(Severity severity, SourceRange range, std::string message);
    void error(SourceRange range, std::string message) { report(Severity::Error, range, std::move(message)); }
    void warning(SourceRange range, std::string message) { report(Severity::Warning, range, std::move(message)); }
    void note(SourceRange range, std::string message) { report(Severity::Note, range, std::move(message)); }

    [[nodiscard]] std::size_t errorCount() const noexcept { return errorCount_; }
    [[nodiscard]] std::size_t warningCount() const noexcept { return warningCount_; }
    [[nodiscard]] bool hasErrors() const noexcept { return errorCount_ != 0; }
    [[nodiscard]] const std::vector<Diagnostic>& diagnostics() const noexcept { return diagnostics_; }

    // Stop printing after this many errors (0 = no limit). Counting continues.
    void setErrorLimit(std::size_t limit) noexcept { errorLimit_ = limit; }
    // Prints e.g. "2 errors and 1 warning generated." Prints nothing if there were none.
    void printSummary() const;

private:
    void print(const Diagnostic& diagnostic) const;

    const SourceManager& sources_;
    std::ostream& out_;
    std::vector<Diagnostic> diagnostics_;
    std::size_t errorCount_ = 0;
    std::size_t warningCount_ = 0;
    std::size_t errorLimit_ = 20;
    bool limitReached_ = false;
};

}  // namespace MemoLang

#endif  // DIAGENGINE_H_
