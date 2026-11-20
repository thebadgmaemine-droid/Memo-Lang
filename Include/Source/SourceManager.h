#ifndef SOURCEMANAGER_H_
#define SOURCEMANAGER_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Source/SourceLocation.h"

namespace MemoLang {

// Owns the text of one source file and maps byte offsets to line/column.
class SourceManager {
public:
  SourceManager(std::string fileName, std::string text);

  // Reads a whole file. Returns nullopt and fills `error` on failure.
  [[nodiscard]] static std::optional<SourceManager>
  fromFile(const std::string &path, std::string &error);

  [[nodiscard]] const std::string &fileName() const noexcept {
    return fileName_;
  }
  [[nodiscard]] std::string_view text() const noexcept { return text_; }

  [[nodiscard]] SourceLocation locationAt(std::uint32_t offset) const;
  // The reverse: a 1-based line:column to a full location, offset included.
  // Returns an invalid location if `line` is out of range. `column` 0 means
  // the start of the line; past the end, it is clamped to just after the last
  // character.
  [[nodiscard]] SourceLocation locationFor(std::uint32_t line,
                                           std::uint32_t column) const;
  // Text of a 1-based line, without the line break.
  [[nodiscard]] std::string_view lineText(std::uint32_t line) const;

private:
  std::string fileName_;
  std::string text_;
  std::vector<std::uint32_t>
      lineStarts_; // Offset of the first character of each line.
};

} // namespace MemoLang

#endif // SOURCEMANAGER_H_
