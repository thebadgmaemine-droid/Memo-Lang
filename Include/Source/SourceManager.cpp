// ---------------------------------------------------------------------------
// SourceManager.cpp: holds the text of the files being compiled
//
// The lexer works with byte offsets (position 0, 1, 2, ... in a file), but
// people read line:column. addFile() records where each line starts;
// locationAt() turns an offset into line:column with a binary search, and
// lineText() returns one line so error messages can show it.
//
// History: added 2026-10-14. 2026-11-26: more than one file, each with a
// FileID (for `import`); the single-file functions use the main file.
// ---------------------------------------------------------------------------
#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

#include "Source/SourceManager.h"

namespace MemoLang {
namespace {
bool readFile(const std::string &path, std::string &text, std::string &error) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    error = "cannot open file '" + path + "'";
    return false;
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  text = buffer.str();
  return true;
}
std::string resolvedPath(const std::string &path) {
  std::error_code ec;
  auto resolved = std::filesystem::weakly_canonical(path, ec);
  return ec ? path : resolved.string();
}

} // namespace

SourceManager::SourceManager(std::string fileName, std::string text) {
  addFile(std::move(fileName), std::move(text));
}

std::optional<SourceManager> SourceManager::fromFile(const std::string &path,
                                                     std::string &error) {
  std::string text;
  if (!readFile(path, text, error))
    return std::nullopt;
  SourceManager sources(path, std::move(text));
  sources.byName_.emplace(resolvedPath(path), mainFileID());
  return sources;
}

FileID SourceManager::addFile(std::string fileName, std::string text) {
  File &added = files_.emplace_back();
  added.name = std::move(fileName);
  added.text = std::move(text);
  // Drop a UTF-8 byte order mark
  if (added.text.size() >= 3 && added.text.compare(0, 3, "\xEF\xBB\xBF") == 0)
    added.text.erase(0, 3);

  added.lineStarts.push_back(0);
  for (std::size_t i = 0; i < added.text.size(); ++i)
    if (added.text[i] == '\n')
      added.lineStarts.push_back(static_cast<std::uint32_t>(i + 1));

  FileID id{static_cast<std::uint32_t>(files_.size())};
  byName_.emplace(added.name, id); // Keeps the first file of a name.
  return id;
}

FileID SourceManager::loadFile(const std::string &path, std::string &error) {
  std::string key = resolvedPath(path);
  if (auto it = byName_.find(key); it != byName_.end())
    return it->second;
  std::string text;
  if (!readFile(path, text, error))
    return {};
  FileID id = addFile(path, std::move(text));
  byName_.emplace(std::move(key), id);
  return id;
}

FileID SourceManager::getFileID(std::string_view fileName) const {
  if (auto it = byName_.find(std::string(fileName)); it != byName_.end())
    return it->second;
  // Not added under this exact name: maybe the same file by another path.
  if (auto it = byName_.find(resolvedPath(std::string(fileName)));
      it != byName_.end())
    return it->second;
  return {};
}

const SourceManager::File &SourceManager::file(FileID id) const {
  assert(id.isValid() && id.id <= files_.size() &&
         "FileID doesn't belong to this SourceManager");
  return files_[id.id - 1];
}

const std::string &SourceManager::fileName(FileID id) const {
  return file(id).name;
}

std::string_view SourceManager::text(FileID id) const { return file(id).text; }

SourceLocation SourceManager::locationAt(FileID id,
                                         std::uint32_t offset) const {
  const File &f = file(id);
  // The first line starting after `offset` is one past the line that
  // contains it.
  auto it = std::upper_bound(f.lineStarts.begin(), f.lineStarts.end(), offset);
  auto line = static_cast<std::uint32_t>(it - f.lineStarts.begin());
  return {offset, line, offset - f.lineStarts[line - 1] + 1, id};
}

SourceLocation SourceManager::locationFor(FileID id, std::uint32_t line,
                                          std::uint32_t column) const {
  const File &f = file(id);
  if (line == 0 || line > f.lineStarts.size())
    return {};
  auto length = static_cast<std::uint32_t>(lineText(id, line).size());
  column = std::clamp<std::uint32_t>(column, 1, length + 1);
  return {f.lineStarts[line - 1] + column - 1, line, column, id};
}

std::string_view SourceManager::lineText(FileID id, std::uint32_t line) const {
  const File &f = file(id);
  if (line == 0 || line > f.lineStarts.size())
    return {};
  std::uint32_t begin = f.lineStarts[line - 1];
  std::uint32_t end = line < f.lineStarts.size()
                          ? f.lineStarts[line] - 1 // Stop before '\n'.
                          : static_cast<std::uint32_t>(f.text.size());
  std::string_view view = std::string_view(f.text).substr(begin, end - begin);
  if (!view.empty() && view.back() == '\r')
    view.remove_suffix(1);
  return view;
}

} // namespace MemoLang
