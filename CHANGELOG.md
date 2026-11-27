# Changelog

All notable changes to MemoLang. Versions follow `project(VERSION ...)` in
`CMakeLists.txt`; each release gets a git tag (`v0.1.0`, ...).

## [Unreleased]

### Fixed (2026-11-27)
- An integer constant stored in a variable must fit its type, as a returned
  constant already had to: `i8 x = 1000;` and `x = 1000;` are errors instead
  of silently storing -24. `300 as i8` still asks for the wrap, and `+=` and
  other arithmetic still wrap. Test: `tests/check/constant_range`.
- Code generation for interpolated strings compiles again (the LLVM CI job
  failed): machine-specific `<llvm-21/...>` / `<llvm-23/...>` includes
  replaced, declarations and definitions match, and `emitToString` is
  implemented for `str` values (other types report "not supported yet").
- `tests/check/sema_errors` matches the shorter "can't be used as a value"
  message.

### Added (2026-11-25)
- Macros: `macro name(params) { body }` and `macro name { body }`, used as
  `name!(args)` / `name!`. A new token stage, `MacroExpander`
  (`Include/Macro`), sits between the lexer and the parser, which now reads
  from a `TokenSource`. Arguments expand before substitution; self-use,
  nesting past 256 levels and expansions over 1M tokens are errors, with an
  "in the expansion of" note for each enclosing macro.
- `--dump-expanded` prints the tokens after macro expansion.
- `!` lexes as `TOK_BANG`; a `!` outside a macro use is still an error.
- Tests: `tests/run/macros`, `tests/errors/macros`.

### Added (2026-11-23)
- Pointers run: the interpreter and code generation support `&x`, `*p`,
  `*p = v`, `*p op= v`, `(*p)++` / `--(*p)` and pointer `==` / `!=`.
  Following a pointer that was never given a value is a run-time error in
  both; following one to a variable that no longer exists is a run-time
  error in the interpreter.
- `sizeof(T)` (new keyword and `SizeOfExpr` node): the size in bytes as an
  `int`, the same in sema, the interpreter and compiled code.
- Tests: `tests/run/pointers`, `pointer_null`, `pointer_dangling`; more cases
  in the pointer `ast`, `tokens`, `errors` and `check` tests.

### Changed (2026-11-23)
- Destructive reads never consume a variable whose address is taken, or
  `*p`: a pointer may still read the matrix. Benchmark numbers are unchanged.
- Interpreter variables live in scope-owned cells (`Interpreter::Cell`).
- Removed the placeholder `UnaryOp::SizeOf` and the unused `NodeKind::Pointer`.

### Added (2026-11-21)
- Pointers, parsing and type checking only: `T*` types (`int*`, `float**`,
  `matrix<float>*`), `&x` and `*p` / `**pp` in expressions, and assignment
  through `*p`. Pointer prefixes bind tighter than `**`, so `*p ** 2` is
  `(*p) ** 2`. Casts take no `*` suffix, so `x as int * 2` is unchanged.
- Semantic checks: `&` needs a non-const variable or `*p`; `*` needs a
  pointer; pointers allow only `==` / `!=`; `void*` is an error; returning
  `&local` or storing it in a global is an error.
- Tests: `tests/{ast,tokens,errors}/pointers`, `tests/check/pointers_ok`,
  `tests/check/pointer_errors`.

### Changed (2026-11-21)
- The assignment error for an invalid left side now says "must be a variable
  or '*pointer'".
- The interpreter stops with an error, not a crash, on assignment through a
  pointer. It doesn't run pointer programs yet; neither does codegen.

### Added (2026-11-12, later)
- Programs can run: an interpreter (`Include/Interp/`) and a CPU matrix
  runtime (`Include/Runtime/`) that counts allocations, frees and peak memory.
  `MemoLang --run`, `--run --no-destructive` and `--compare` run `main()` with
  destructive reads, without them (C++/Rust-style freeing at scope end), or
  both side by side.
- `benchmarks/`: four workloads, and `MemoLangTests membench`, which prints
  their peak memory, allocations and time in both modes.
- `MemoLangTests bench`: compile time per stage.
- `membench --save` records the results in `docs/results/` (a dated Markdown
  report, and a line per measurement in `membench.csv`) with the CPU,
  compiler and build type.
- `benchmarks/cpp/`: the four workloads in native C++ (plain, and with every
  matrix freed by hand), measured by `membench` next to MemoLang.
- `tests/run/`: interpreter tests; each program runs in both modes and must
  give the same result and free every matrix. The benchmarks are checked the
  same way. 27 tests in total.

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
