#ifndef MEMOLANG_MODULE_LOADER_H_
#define MEMOLANG_MODULE_LOADER_H_
// ---------------------------------------------------------------------------
// ModuleLoader.h: `import`
//
//   main.memo -> Parser -> [loadImports] -> one Module -> sema -> ...
//
// `import "lib.memo";` (or `import lib;`) makes lib.memo's top-level
// functions, externs and variables part of the importing program. The loader
// reads and parses every imported file, then moves its top-level statements
// in front of the importing module's own, so semantic analysis, destructive
// analysis, the interpreter and code generation all see one module and need
// nothing special for imports.
//
// Rules:
//   - The path is relative to the folder of the file that imports it.
//   - Each file is loaded once, however many files import it, so an import
//     cycle (a imports b, b imports a) is harmless.
//   - Code runs dependencies first: a file's module-level statements run
//     before those of every file that imports it, in import order.
//   - Everything is in one namespace: two files defining the same function
//     name is a "redefinition" error, pointing into the second file.
//   - Macros stay in their own file (expansion happens per file).
// Errors in an imported file name that file (every location carries its
// FileID).
//
// 2026-11-28: added.
// ---------------------------------------------------------------------------
#include "AST/AST.h"

namespace MemoLang {
class DiagnosticEngine;
class SourceManager;

// Loads everything `module` (the main file of `sources`) imports, directly or
// not, and merges it into `module.body`. Returns false if an import failed;
// the problem is reported to `diags`.
bool loadImports(Module &module, SourceManager &sources,
                 DiagnosticEngine &diags);

} // namespace MemoLang

#endif // MEMOLANG_MODULE_LOADER_H_
