#ifndef DIAG_H_
#define _DIAG_H_
#pragma once
#include <memory>
#include "SourceManager.h" // Two placeholders
#include "SourceRange.h" // Two place holders
class SourceManager;
enum class DiagsHolder {
    DiagsSeverity,
    DiagsColor,
    DiagsInfo,
    Holder,
    SourceRange,
};
enum class DiagsInfo final : DiagsHolder {
    RangeError,
    SyntaxError,
    FileError,
    Warning,
    RuntimeError,
    Exception,
    HandleError,
    NotFoundError,
    OutofReachError,
    IdentationError, // nvm
    RecursionError, 
    Implement_timeError,
    TypeError,
    ValueError,
    ImportError,
    CUTLASSTemplateViolation, // Rule violation! 
}
enum class DiagsColor : DiagsHolder {
    protected: 
        Auto,
        Always,
        Never,
}
enum class DiagsSeverity : DiagsHolder {
    // Empty for now
}

struct DiagsCodeInfo {
    DiagnosticCode code = DiagnosticCode::TypeError;
    std::string_view name; 
    std::string_view description;
};
/* Special thanks to sere! I did not know what to write here because it isn't included in any docs */
[[nodiscard]] std::string_view diagnosticCodeName(DiagnosticCode code);

[[nodiscard]] std::string_view diagnosticCodeDescription(DiagnosticCode code);

[[nodiscard]] std::optional<DiagnosticCode> parseDiagnosticCode(std::string_view name);

[[nodiscard]] const std::vector<DiagnosticCodeInfo>& diagnosticCodeCatalog();

[[nodiscard]] std::string diagnosticCodeCatalogText();

[[nodiscard]] DiagnosticCode inferDiagnosticCode(std::string_view message);



#endif // diag.h