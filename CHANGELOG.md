# Changelog

All notable changes to MemoLang. Versions follow `project(VERSION ...)` in
`CMakeLists.txt`; each release gets a git tag (`v0.1.0`, ...).

## [Unreleased]

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
