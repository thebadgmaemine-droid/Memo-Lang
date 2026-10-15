// ---------------------------------------------------------------------------
// diags.cpp: the DiagnosticEngine, which every stage uses to report problems
//
// Each error is printed as soon as it is reported, in the same style as clang:
//   examples/errors.memo:6:18: error: expected ';' after variable declaration
//    6 |     int b = a * 2
//      |                  ^
// The caret (^) marks the column and `~` underlines the rest of the token.
// Every diagnostic is also stored (diagnostics()), so the tests can check them.
//
// History: written 2026-10-14 (diag.h, diagengine.h and diags.cpp were empty).
// 2026-10-15: long lines are shown as a 120-character window around the caret.
// Before that, a 200 KB line with 100000 errors printed tens of GB and got the
// CI job killed.
// ---------------------------------------------------------------------------
#include "Diag/diagengine.h"

#include <algorithm>
#include <ostream>
#include <string>
#include <string_view>

#include "Source/SourceManager.h"

namespace MemoLang {

DiagnosticEngine::DiagnosticEngine(const SourceManager& sources, std::ostream& out)
    : sources_(sources), out_(out) {}

void DiagnosticEngine::report(Severity severity, SourceRange range, std::string message) {
    if (severity == Severity::Error) ++errorCount_;
    else if (severity == Severity::Warning) ++warningCount_;
    diagnostics_.push_back({severity, range, std::move(message)});

    if (limitReached_) return;
    if (errorLimit_ != 0 && severity == Severity::Error && errorCount_ > errorLimit_) {
        limitReached_ = true;
        out_ << sources_.fileName() << ": error: too many errors (limit " << errorLimit_
             << "); the rest are not shown\n";
        return;
    }
    print(diagnostics_.back());
}

void DiagnosticEngine::print(const Diagnostic& diagnostic) const {
    const SourceLocation& loc = diagnostic.range.begin;
    out_ << sources_.fileName();
    if (loc.isValid()) out_ << ':' << loc.line << ':' << loc.column;
    out_ << ": " << severityName(diagnostic.severity) << ": " << diagnostic.message << '\n';
    if (!loc.isValid()) return;

    std::string_view line = sources_.lineText(loc.line);
    std::size_t caret = loc.column - 1;  // 0-based; may be just past the end of the line.

    // Show at most kMaxShown characters around the caret, so one very long line
    // (minified or generated code) doesn't print megabytes per error.
    constexpr std::size_t kMaxShown = 120;
    std::size_t first = 0;
    if (line.size() > kMaxShown) {
        first = std::min(caret > kMaxShown / 2 ? caret - kMaxShown / 2 : 0, line.size() - kMaxShown);
        line = line.substr(first, kMaxShown);
    }
    bool cutBefore = first != 0;
    bool cutAfter = first + line.size() < sources_.lineText(loc.line).size();
    caret -= first;

    std::string lineNumber = std::to_string(loc.line);
    out_ << ' ' << lineNumber << " | " << (cutBefore ? "..." : "") << line << (cutAfter ? "..." : "") << '\n';
    out_ << ' ' << std::string(lineNumber.size(), ' ') << " | " << (cutBefore ? "   " : "");

    // Copy tabs from the source line so the caret lines up under tab-indented code.
    for (std::size_t i = 0; i < caret; ++i) out_ << (i < line.size() && line[i] == '\t' ? '\t' : ' ');
    out_ << '^';

    // Underline the rest of the range when it stays on the same line.
    const SourceLocation& end = diagnostic.range.end;
    if (end.line == loc.line && end.column > loc.column + 1) {
        std::size_t last = std::min<std::size_t>(end.column - 1 - first, line.size());
        if (last > caret + 1) out_ << std::string(last - caret - 1, '~');
    }
    out_ << '\n';
}

void DiagnosticEngine::printSummary() const {
    if (errorCount_ == 0 && warningCount_ == 0) return;
    auto count = [](std::size_t n, const char* word) {
        return std::to_string(n) + ' ' + word + (n == 1 ? "" : "s");
    };
    if (errorCount_ != 0 && warningCount_ != 0)
        out_ << count(errorCount_, "error") << " and " << count(warningCount_, "warning");
    else if (errorCount_ != 0)
        out_ << count(errorCount_, "error");
    else
        out_ << count(warningCount_, "warning");
    out_ << " generated.\n";
}

}  // namespace MemoLang
