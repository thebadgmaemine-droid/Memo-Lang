// ---------------------------------------------------------------------------
// ModuleLoader.cpp: loads imported files and merges them (ModuleLoader.h)
// ---------------------------------------------------------------------------
#include "Driver/ModuleLoader.h"

#include <filesystem>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Diag/diag.h"
#include "Diag/diagengine.h"
#include "Lex/lexer.h"
#include "Macro/MacroExpander.h"
#include "Parser/parser.h"
#include "Source/SourceManager.h"

namespace MemoLang {
namespace {

namespace fs = std::filesystem;

// A chain of imports this deep is almost certainly generated; stop before
// the recursion below gets deep.
constexpr int kMaxImportDepth = 64;

class Loader {
public:
  Loader(SourceManager &sources, DiagnosticEngine &diags)
      : sources_(sources), diags_(diags) {
    loaded_.insert(SourceManager::mainFileID().id);
  }

  // Loads the files `importer` (parsed from `importerFile`) imports, their
  // imports first, and appends their statements to merged_.
  void load(const Module &importer, FileID importerFile, int depth) {
    for (const Import &import : importer.imports) {
      if (depth >= kMaxImportDepth) {
        diags_.error(import.range, "imports are nested more than " +
                                       std::to_string(kMaxImportDepth) +
                                       " files deep");
        ok_ = false;
        return;
      }
      fs::path path(import.path);
      if (path.is_relative())
        path = fs::path(sources_.fileName(importerFile)).parent_path() / path;
      std::error_code ec;
      if (!fs::is_regular_file(path, ec)) {
        diags_.error(import.range,
                     "can't import " + quoteForMessage(import.path) +
                         ": no file '" + path.lexically_normal().string() +
                         "'");
        ok_ = false;
        continue;
      }
      std::string readError;
      FileID file = sources_.loadFile(path.string(), readError);
      if (!file.isValid()) {
        diags_.error(import.range, "can't import " +
                                       quoteForMessage(import.path) + ": " +
                                       readError);
        ok_ = false;
        continue;
      }
      if (!loaded_.insert(file.id).second)
        continue; // Already merged, or it's on the way here (a cycle).

      Lexer lexer(sources_, diags_, file);
      MacroExpander expander(lexer, diags_);
      Parser parser(expander, diags_);
      Module imported = parser.parseModule(path.stem().string());
      load(imported, file, depth + 1); // Its dependencies come first.
      for (auto &stmt : imported.body)
        merged_.push_back(std::move(stmt));
    }
  }

  std::vector<std::unique_ptr<Statement>> takeMerged() {
    return std::move(merged_);
  }
  [[nodiscard]] bool ok() const noexcept { return ok_; }

private:
  SourceManager &sources_;
  DiagnosticEngine &diags_;
  std::unordered_set<std::uint32_t> loaded_; // FileIDs already merged.
  std::vector<std::unique_ptr<Statement>> merged_;
  bool ok_ = true;
};

} // namespace

bool loadImports(Module &module, SourceManager &sources,
                 DiagnosticEngine &diags) {
  if (module.imports.empty())
    return true;
  Loader loader(sources, diags);
  loader.load(module, SourceManager::mainFileID(), 0);
  auto body = loader.takeMerged();
  for (auto &stmt : module.body)
    body.push_back(std::move(stmt));
  module.body = std::move(body);
  return loader.ok();
}

} // namespace MemoLang
