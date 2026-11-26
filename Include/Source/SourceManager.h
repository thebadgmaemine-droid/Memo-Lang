#ifndef SOURCEMANAGER_H_
#define SOURCEMANAGER_H_

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Source/SourceLocation.h"
#include "Support/Compiler.h"

namespace MemoLang {
class SourceManager {
public:
  SourceManager(std::string fileName, std::string text);

  [[nodiscard]] static std::optional<SourceManager>

  fromFile(const std::string &path, std::string &error);

  FileID addFile(std::string fileName, std::string text);

  FileID loadFile(const std::string &path, std::string &error);

  [[nodiscard]] FileID getFileID(std::string_view fileName) const;
  [[nodiscard]] static constexpr FileID mainFileID() noexcept { return {1}; }
  [[nodiscard]] std::size_t fileCount() const noexcept { return files_.size(); }

  [[nodiscard]] const std::string &
  fileName(FileID file) const MEMOLANG_LIFETIMEBOUND;
  [[nodiscard]] std::string_view text(FileID file) const MEMOLANG_LIFETIMEBOUND;

  [[nodiscard]] SourceLocation locationAt(FileID file,
                                          std::uint32_t offset) const;

  [[nodiscard]] SourceLocation locationFor(FileID file, std::uint32_t line,
                                           std::uint32_t column) const;

  [[nodiscard]] std::string_view lineText(FileID file,
                                          std::uint32_t line) const;

  [[nodiscard]] const std::string &
  fileName() const noexcept MEMOLANG_LIFETIMEBOUND {
    return files_.front().name;
  }
  [[nodiscard]] std::string_view text() const noexcept MEMOLANG_LIFETIMEBOUND {
    return files_.front().text;
  }
  [[nodiscard]] SourceLocation locationAt(std::uint32_t offset) const {
    return locationAt(mainFileID(), offset);
  }
  [[nodiscard]] SourceLocation locationFor(std::uint32_t line,
                                           std::uint32_t column) const {
    return locationFor(mainFileID(), line, column);
  }
  [[nodiscard]] std::string_view lineText(std::uint32_t line) const {
    return lineText(mainFileID(), line);
  }

private:
  struct File {
    std::string name;
    std::string text;
    std::vector<std::uint32_t>
        lineStarts; // Offset of each line's first character.
  };

  [[nodiscard]] const File &file(FileID id) const MEMOLANG_LIFETIMEBOUND;

  std::deque<File> files_;

  std::unordered_map<std::string, FileID> byName_;
};

} // namespace MemoLang

#endif // SOURCEMANAGER_H_
