#pragma once
#include "AST/AST.h"
#include "Source/SourceLocation.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
namespace MemoLang {
    // Macro use blueprint if Macros are added ( May or may not be added )
    // 2026-10-14: `_NODISCARD` (MSVC only) changed to standard [[nodiscard]],
    // and SourceLocation.h included for SourceRange. Not implemented yet.

    [[nodiscard]] bool rangecontainer(SourceRange range, std::uint32_t offset);
    [[nodiscard]] SourceRange Indentifier_Range(SourceRange start, std::string_view name);

}