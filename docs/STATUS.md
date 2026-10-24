# MemoLang: project status

Last updated: 2026-10-14 (v0.1.0)

This file tracks what exists in the code, what is a stub, and what is still
missing, so anyone picking up the project knows where to start. Update it
whenever a component changes state.

## Build and run

```sh
cmake -S . -B build -DMEMOLANG_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build -C Debug          # runs the examples

build/Debug/MemoLang examples/basicadd.memo              # check syntax
build/Debug/MemoLang --dump-ast examples/matmul_chain.memo
build/Debug/MemoLang --dump-tokens examples/basicadd.memo
build/Debug/MemoLang examples/errors.memo                # shows diagnostics
```

This needs a C++20 compiler: MSVC 2022+, GCC 11+ or Clang 14+. LLVM isn't
needed until code generation exists (`-DMEMOLANG_USE_LLVM=ON` turns it back on).

If Windows Smart App Control is on, it can block the freshly built,
unsigned `.exe` files ("An Application Control policy has blocked this file").
In that case, run the tests in WSL, on another machine, or in CI.

## Tests

`ctest` runs `build/<Config>/MemoLangTests.exe` once per file:

| Folder | What it checks |
|---|---|
| `tests/ast/*.memo` | Parses with no errors; `--dump-ast` output must equal the `.ast` file next to it |
| `tests/tokens/*.memo` | Lexes with no errors; token list must equal the `.tokens` file |
| `tests/errors/*.memo` | Every error matches an `# expect-error: <col>: <text>` comment on its line (or `# expect-error@+N:` for N lines below), and there are no other errors |
| `stress` (built in) | Empty/huge/1 MB inputs, NUL bytes, CRLF, 100000-deep nesting of every kind, 10000-term chains, throughput |
| `fuzz` (built in) | 9000 random, token-soup and mutated inputs: no crash, no hang, same result every run |

To add a test, drop a `.memo` file in the right folder and re-run CMake. For
`ast`/`tokens`, the first run creates the expected-output file and fails until
you have reviewed it. After an intended output change, regenerate it with
`MemoLangTests ast tests/ast/<name>.memo --update` and check the diff.

## Pipeline status

The stages follow the pipeline figure in the report.

| Stage | Status | Files |
|---|---|---|
| Source / locations | Done | `Include/Source/SourceLocation.h`, `SourceManager.h/.cpp` |
| Diagnostics | Done | `Include/Diag/diag.h`, `diagengine.h`, `Include/Source/diags.cpp` |
| Lexer | Done | `Include/Lex/tokenkind.h`, `token.h`, `lexer.h`, `Include/Source/lexer.cpp`, `token.cpp` |
| Parser | Done, for the grammar below | `Include/Parser/parser.h`, `Include/Source/parser.cpp` |
| AST | Done, for the grammar below | `Include/AST/AST.h`, `ASTPrinter.h`, `Include/Source/ASTPrinter.cpp` |
| Driver | Done (front-end only) | `Include/Driver/driver.h`, `Include/Source/driver.cpp`, `main.cpp` |
| Semantic analysis / types | **Empty file** | `Include/Semantics/sema.h`, `Include/Source/type.cpp` |
| Consumption analysis (destructive read) | **Missing**, no file yet | Uses `State { Live, Consumed, MaybeConsumed }` in `AST.h` |
| LLVM IR generation | **Empty file / interface only** | `Include/Codegen/codegen.h`, `Include/Source/codegen.cpp`, `Include/IR/-IRBuilder.h` |
| JIT / AOT | **Missing** | none |
| cuBLAS runtime | **Missing** (the report lists `CuBLASRuntime.cpp`, which was never created) | none |
| CUTLASS GEMM kernel | **Missing** | none |
| Macros | **Notes only** | `Include/Macro/read.md`, `Include/AST/query.h` (declared, not implemented) |
| Tests | Front-end covered | `tests/` (see "Tests" below), `examples/*.memo` |

### Known problems in existing files

- `Include/IR/-IRBuilder.h`: compiles now (fixed 2026-10-17) but is only an
  interface, `IRGenerator`, with no implementation, and `TypeContext` is only
  forward-declared. The leading `-` in the file name is unusual; consider
  renaming it to `IRGenerator.h` when codegen starts.
- `Include/AST/query.h`: declares `rangecontainer` and `Indentifier_Range`,
  which are never defined. Fine while unused; the second name has a typo.
- `AST.h`: `Visitor` has a nested `State` enum that duplicates the
  namespace-level `State`. Keep one.
- `AST.h`: `InterpolatedString` and the `isByte`/`isRegex` flags exist in the
  AST, but the lexer has no syntax for them yet.
- The report's "Main Source Files" table (`Lexer.h/.cpp`, `ExprAST.h`,
  `CodeGen.cpp`, `CuBLASRuntime.cpp`) doesn't match the real file names. Update
  it before the next submission.

## Grammar (what the parser accepts today)

```
module         := { statement }
statement      := function_def | extern_def | var_decl | return_stmt
                | if_stmt | while_stmt | 'break' ';' | expr_or_assign | ';'
function_def   := 'def' IDENT '(' params ')' [ '->' type ] block
extern_def     := 'extern' 'def' IDENT '(' params ')' [ '->' type ] ';'
params         := [ type IDENT { ',' type IDENT } ]
type           := TYPE_NAME [ '<' type { ',' type } '>' ]
block          := '{' { statement } '}'
var_decl       := [ 'static' ] [ 'const' ] [ type ] IDENT [ '=' expr ] ';'
return_stmt    := 'return' [ expr ] ';'
if_stmt        := 'if' expr block [ 'else' ( if_stmt | block ) ]
while_stmt     := 'while' expr block
expr_or_assign := expr [ ( '=' | '+=' | '-=' | '*=' | '/=' | '%=' ) expr ] ';'

expr           := 'not' expr | unary { binary_op unary }
unary          := ( '-' | '+' | '~' | '++' | '--' ) unary | power
power          := postfix [ '**' unary ]
postfix        := primary { '(' args ')' | 'as' type | '++' | '--' }
args           := [ [ IDENT '=' ] expr { ',' [ IDENT '=' ] expr } ]
primary        := INTEGER | FLOAT | STRING | 'true' | 'false' | IDENT
                | '(' expr ')' | type '(' expr ')'
```

- `TYPE_NAME` is one of `int i32 i64 float f32 f64 bool str byte void matrix`.
- Comments start with `#`. `//` is floor division.
- A `def` without `-> type` gets return type `auto` (to be inferred by sema).
  An `extern` without one gets `void`.
- `if` and `while` conditions don't need parentheses.

Binary operator precedence, lowest first. All are left-associative except `**`.

| Level | Operators |
|---|---|
| 1 | `or` |
| 2 | `and` |
| 3 | `not` (prefix) |
| 4 | `==  !=  <  <=  >  >=  is  is not  in  not in` |
| 5 | `\|` |
| 6 | `^` |
| 7 | `&` |
| 8 | `<<  >>` |
| 9 | `+  -` |
| 10 | `*  /  //  %  @` |
| 11 | unary `-  +  ~  ++  --` |
| 12 | `**` (right-associative) |
| 13 | call `f(x)`, cast `x as float`, postfix `++ --` |

Keywords the lexer recognizes but the parser rejects with "not supported yet":
`for`, `import`, `try`, `catch`, `throw`, `case`, `none`.

## Suggested next steps

1. **Semantic analysis** (`sema.h`): name resolution, type checking, and
   inferring `auto` return types. It reports through `DiagnosticEngine`, like the parser.
2. **Consumption analysis**: a `Visitor` that marks each matrix operand of `@`
   as `Consumed`, and reports use-after-consume as an error. This is the core of
   the research.
3. **Matrix runtime**: a small C++ library wrapping cuBLAS (and later the
   CUTLASS kernel) with `memo_matmul(a, b, consume_a, consume_b)`.
4. **Codegen**: lower the AST to LLVM IR that calls the runtime with consume flags.
5. **More tests**: one `.memo` file per feature, compared against expected
   `--dump-ast` output. The CTest setup is already in `CMakeLists.txt`.

## Version control workflow

Suggested conventions for the team:

- **Branches:** keep `master` building. Do each feature on its own branch
  (`feature/sema`, `fix/lexer-hex`), then merge with a pull request so someone
  else looks at it first.
- **Commit messages:** one line saying what changed, e.g. "Add consumption
  analysis for @ operands". Messages like "s" or "sync" make it hard to find
  when something broke, and that history is the evidence for the report.
- **Versions:** bump `VERSION` in `CMakeLists.txt`, add a section to
  `CHANGELOG.md`, and tag the commit (`git tag v0.2.0`) for every milestone
  or report submission. `MemoLang --version` prints the version.
- **Generated files:** LaTeX build files (`*.aux`, `*.toc`, `*.synctex.gz`, ...)
  are now in `.gitignore`. Some of these files were committed earlier, and
  `.gitignore` doesn't affect files already in git. Stop tracking them with
  `git rm --cached <file>`.
- **Dev log:** keep logging AI prompts in `submission-online-final/dev-log.md`,
  as done so far.
