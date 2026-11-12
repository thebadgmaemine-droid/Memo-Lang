# Changelog

All notable changes to MemoLang. Versions follow `project(VERSION ...)` in
`CMakeLists.txt`; each release gets a git tag (`v0.1.0`, ...).

## [Unreleased]

### Added (2026-11-12)
- Semantic analysis (`Include/Semantics/sema.cpp`): name resolution, type
  checking, `auto` return types inferred from `return`, named arguments, and
  the built-in `copy(x)`. Runs after parsing in the compiler.
- Types (`Include/Types/Typecontext.cpp`): Error, Auto, Void, Bool, Int, Float,
  Str, Matrix (element type and optional shape) and Function, interned by
  TypeContext.
- Destructive-read analysis (`Include/Analysis/destructive.cpp`): matrices are
  consumed by `@`, calls, moves and `return`; use after consume is an error,
  including after an `if` branch or an earlier loop iteration. Gives the
  consume flags for each `@`.
- Every AST node has a full source range, so errors underline the expression.
- Driver options `--dump-symbols` and `--parse-only`.
- Tests in `tests/check/` through the whole front-end; stress cases through
  the analyses; fuzzing through every stage.

### Changed (2026-11-12)
- Merged master's 2026-11-09 commits (Clang 23 presets, `.clangd`).
- One scope design (`Semantics/Scope.h`); the second `sema::Scope` class was
  removed.
- Types no longer depend on LLVM (one allocator: `std::unique_ptr`). The
  declared-but-unimplemented records, aliases, modules and generic
  instantiation API was removed; it's in git history (5281e16).
- Postfix chains (`x as int as int ...`) count toward the nesting limit.
- No hard-coded `C:/dev/llvm` in `CMakeLists.txt` or `CMakePresets.json`; put
  `LLVM_DIR` in your own `CMakeUserPresets.json` (now ignored by git).
- Analysis code moved to `Include/Analysis/` (the empty `Include/Analyzer/`
  files were removed). Report drafts moved to `docs/reports/drafts/` and
  legacy notes to `docs/legacy/`.
- `examples/matmul_chain.memo` uses `copy(m)` in its loop: the new analysis
  found it used `m` after consuming it.

### Fixed (2026-11-12)
- Long `a + b + c + ...` chains overflowed the stack in the analyses; they
  are now walked with a loop.
- Loop analysis was exponential in the nesting depth; results are cached.

### Added
- Test suite (`tests/`): expected-output tests for the parser and lexer,
  `# expect-error` tests for diagnostics, stress and fuzz tests. See docs/STATUS.md.
- Nesting limit (`Parser::kMaxNestingDepth` = 256). Deeper input gets one error
  and parsing stops, instead of the program crashing with a stack overflow.
- The front-end builds as the `MemoLangFrontend` library, shared by the
  compiler and the test runner.

### Changed
- Executables get an 8 MB stack on Windows (the default is 1 MB, which Debug
  builds used up before reaching the nesting limit).
- The module is named after the input file's stem instead of its full path.

### Fixed
- Unused-variable warnings in Release builds from assert-only loops in `AST.h`.
- Mismatches left over from the old LLVM/Kaleidoscope driver:
  - `CMakePresets.json` used the "Visual Studio 17 2022" generator, so opening
    the project in Visual Studio 2026 failed (MSB8020, v143 toolset not found).
    It now uses Ninja, which works with Visual Studio 2022 and 2026.
  - `Include/IR/-IRBuilder.h` (LLVM codegen interface) didn't compile:
    `namespace memo-lang`, constructor name not matching the class, no return
    type on `emit`, missing `;`, undeclared types. Now valid C++ in `MemoLang`.
  - Removed `Include/CMakeLists.txt`, an unused old build file that defined a
    second `MemoLang` target from the old `main.cpp` and required LLVM.
  - The include path now gets only folders under `Include/`, not files.

## [0.1.0] - 2026-10-14

First version of the rewritten front-end (namespace `MemoLang`).

### Added
- Source locations and `SourceManager` (offset → line/column, line text).
- Diagnostics: `Diagnostic` and `DiagnosticEngine`. Prints clang-style messages
  with the source line and a caret, an error limit, and a summary count.
- Lexer: every token in `tokenkind.h`, `#` comments, decimal/hex/binary/float
  literals with the `f` suffix, string escapes, and error recovery.
- Parser: functions, `extern` declarations, variables, assignment, `return`,
  `if`/`else if`/`else`, `while`, `break`, calls with named arguments, casts,
  and every binary operator, including `@` for matrix multiplication.
  The parser recovers from errors, so one run reports all of them.
- AST nodes: `NameExpr`, `AssignStmt`, `ReturnStmt`, `IfStmt`, `WhileStmt`,
  `BreakStmt`, `Module`; `BinaryOp::MatMul`, `UnaryOp::Not`, `AssignOp::Mod`.
- AST printer and command-line driver (`--dump-tokens`, `--dump-ast`, `--version`).
- Example programs in `examples/` and CTest tests that run them.

### Fixed
- `FunctionDef::name_` was declared `static`, so every function shared one name.
- Compile errors: missing `;` in `token.h`, `#endif` text in `tokenkind.h`,
  namespace typo in `AST.cpp`, MSVC-only `_NODISCARD` in `query.h`.

### Changed
- LLVM is now optional (`MEMOLANG_USE_LLVM`, default `OFF`) until code generation exists.
- `main.cpp` runs the new driver instead of the old Kaleidoscope REPL. The old
  front-end is still in git history at commit `dae18ee`.
