#pragma once
#include "AST/AST.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
namespace MemoLang {
    // Macro use blueprint if Macros are added ( May or may not be added )

    _NODISCARD bool rangecontainer(SourceRange range, std::uint32_t offset);
    _NODISCARD SourceRange Indentifier_Range(SourceRange start, std::string_view name);

}