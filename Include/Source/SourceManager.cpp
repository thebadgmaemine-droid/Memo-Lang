// ---------------------------------------------------------------------------
// SourceManager.cpp: holds the text of the file being compiled
//
// The lexer works with byte offsets (position 0, 1, 2, ... in the file), but
// people read line:column. The constructor records where each line starts;
// locationAt() turns an offset into line:column with a binary search, and
// lineText() returns one line so error messages can show it.
//
// History: added 2026-10-14.
// ---------------------------------------------------------------------------
#include "Source/SourceManager.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <utility>

namespace MemoLang {

SourceManager::SourceManager(std::string fileName, std::string text)
    : fileName_(std::move(fileName)), text_(std::move(text)) {
    // Drop a UTF-8 byte order mark so the lexer doesn't report it as an unknown character.
    if (text_.size() >= 3 && text_.compare(0, 3, "\xEF\xBB\xBF") == 0) text_.erase(0, 3);

    lineStarts_.push_back(0);
    for (std::size_t i = 0; i < text_.size(); ++i)
        if (text_[i] == '\n') lineStarts_.push_back(static_cast<std::uint32_t>(i + 1));
}

std::optional<SourceManager> SourceManager::fromFile(const std::string& path, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open file '" + path + "'";
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return SourceManager(path, buffer.str());
}

SourceLocation SourceManager::locationAt(std::uint32_t offset) const {
    // The first line starting after `offset` is one past the line that contains it.
    auto it = std::upper_bound(lineStarts_.begin(), lineStarts_.end(), offset);
    auto line = static_cast<std::uint32_t>(it - lineStarts_.begin());
    return {offset, line, offset - lineStarts_[line - 1] + 1};
}

std::string_view SourceManager::lineText(std::uint32_t line) const {
    if (line == 0 || line > lineStarts_.size()) return {};
    std::uint32_t begin = lineStarts_[line - 1];
    std::uint32_t end = line < lineStarts_.size() ? lineStarts_[line] - 1  // Stop before '\n'.
                                                  : static_cast<std::uint32_t>(text_.size());
    std::string_view view(text_.data() + begin, end - begin);
    if (!view.empty() && view.back() == '\r') view.remove_suffix(1);
    return view;
}

}  // namespace MemoLang
