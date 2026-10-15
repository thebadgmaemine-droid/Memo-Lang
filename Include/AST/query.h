#pragma once
#include "AST/AST.h"
#include "Source/SourceLocation.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
namespace MemoLang {
    // Macro use blueprint if Macros are added ( May or may not be added )

    [[nodiscard]] bool rangecontainer(SourceRange range, std::uint32_t offset);
    [[nodiscard]] SourceRange Indentifier_Range(SourceRange start, std::string_view name);

}