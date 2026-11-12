# MemoLang: project status

Last updated: 2026-11-12

This file tracks what exists in the code, what is a stub, and what is still
missing, so anyone picking up the project knows where to start. Update it
whenever a component changes state.

## Build and run

```sh
cmake -S . -B build -DMEMOLANG_BUILD_TESTS=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug

build/Debug/MemoLang examples/matmul_chain.memo          # full check
build/Debug/MemoLang --dump-ast examples/matmul_chain.memo
build/Debug/MemoLang --dump-symbols examples/basicadd.memo
build/Debug/MemoLang --dump-tokens examples/basicadd.memo
build/Debug/MemoLang --parse-only examples/basicadd.memo # syntax only
build/Debug/MemoLang examples/errors.memo                # shows diagnostics
```

This needs a C++20 compiler: MSVC 2022+, GCC 11+ or Clang 14+. LLVM isn't
needed until code generation exists. To build with it, configure with
`-DMEMOLANG_USE_LLVM=ON`, plus `-DLLVM_DIR=<path to LLVMConfig.cmake>` if
CMake doesn't find it. Put machine-specific settings like `LLVM_DIR` in your
own `CMakeUserPresets.json` (ignored by git), not in `CMakePresets.json`.

If Windows Smart App Control is on, it can block the freshly built,
unsigned `.exe` files ("An Application Control policy has blocked this file").
In that case, run the tests in WSL, on another machine, or in CI.

## Pipeline status

The stages follow the pipeline figure in the report.

| Stage | Status | Files |
|---|---|---|
| Source / locations | Done | `Include/Source/SourceLocation.h`, `SourceManager.h/.cpp` |
| Diagnostics | Done | `Include/Diag/diag.h`, `diagengine.h`, `diags.cpp` |
| Lexer | Done | `Include/Lex/tokenkind.h`, `token.h`, `token.cpp`, `lexer.h`, `lexer.cpp` |
| Parser | Done, for the grammar below | `Include/Parser/parser.h`, `parser.cpp` |
| AST | Done; every node has a source range | `Include/AST/AST.h`, `AST.cpp`, `ASTPrinter.h/.cpp`, `query.h` |
| Types | Done for MemoLang's types | `Include/Types/Typecontext.h`, `Typecontext.cpp` |
| Semantic analysis | Done (names, types, `auto` return types) | `Include/Semantics/sema.h`, `sema.cpp`, `Scope.h`, `Scope.cpp` |
| Consumption analysis (destructive read) | Done (rules below) | `Include/Analysis/destructive.h`, `destructive.cpp` |
| Driver | Done (front-end) | `Include/Driver/driver.h`, `driver.cpp`, `main.cpp` |
| LLVM IR generation | **Interface only**: `IRGenerator` returns a null module | `Include/IR/IRBuilder.h`, `IRBuilder.cpp`, `IRGenerator.h` |
| Optimization passes | **Missing** | none |
| JIT / AOT | **Missing** | none |
| cuBLAS runtime | **Missing** (the report lists `CuBLASRuntime.cpp`, which doesn't exist yet) | none |
| CUTLASS GEMM kernel | **Missing** | none |
| Macros | **Notes only** | `Include/Macro/read.md` |
| Tests | Front-end, semantic and consumption analysis | `tests/` (see "Tests" below), `examples/*.memo` |

### Known gaps

- Code generation: nothing turns the checked program into something that runs
  yet. `Destructive::consumeFlags()` already gives each `@` its consume_a /
  consume_b flags for when it does.
- Matrix shapes: `matrix<float>` has no shape in the syntax, so shapes are
  unknown and `@` can't check them yet. `MatrixType` and the `@` check already
  support known shapes.
- Semantic analysis doesn't check that every path of a function returns a
  value.
- `InterpolatedString` and the `isByte` / `isRegex` flags exist in the AST, but
  the lexer has no syntax for them yet.
- The report's "Main Source Files" table (`Lexer.h/.cpp`, `ExprAST.h`,
  `CodeGen.cpp`, `CuBLASRuntime.cpp`) doesn't match the real file names. Update
  it before the next submission.
- Credit: `Include/Types/Typecontext.h` is based on the Sere compiler's
  TypeContext. Check Sere's license and credit it in the report.

## Semantic rules

- Names must be declared before use; an inner block may shadow an outer name.
- Integers convert to any integer or float type; floats convert to floats.
- `/` on integers gives a float; `//` keeps integers.
- `a @ b` needs two matrices with the same element type (and matching inner
  dimensions, once shapes are known). A matrix may be multiplied or divided by
  a number, and added to or subtracted from a matrix of the same type.
- `if` and `while` conditions must be `bool`. `break` must be inside a loop.
- A `def` without `-> type` gets its return type from its `return`
  statements. A function that calls itself needs an explicit `-> type`.
- Arguments may be named (`f(x = 1)`) after the positional ones.
- Functions can only be defined at the top level.

## Consumption rules (destructive read)

A matrix variable is **consumed** when it is an operand of `@`, an argument of
a function call, moved into another variable (`b = a`), or returned. After that
it can't be used until it gets a new value. Reading it in `m * 2.0`, `-m` or
`m + n` doesn't consume it. `copy(m)` gives a fresh matrix without consuming
`m`. A matrix consumed on only one branch of an `if`, or in an earlier loop
iteration, counts as "maybe consumed", and using it is an error too. See
`Include/Analysis/destructive.h` for the details. These rules are a first
version; change them there as the design evolves.

## Tests

`ctest` runs `build/<Config>/MemoLangTests.exe` once per file:

| Folder | What it checks |
|---|---|
| `tests/ast/*.memo` | Parses with no errors; `--dump-ast` output must equal the `.ast` file next to it |
| `tests/tokens/*.memo` | Lexes with no errors; token list must equal the `.tokens` file |
| `tests/errors/*.memo` | Parse errors: every error matches an `# expect-error: <col>: <text>` comment on its line (or `# expect-error@+N:` for N lines below), and there are no other errors |
| `tests/check/*.memo` | The whole front-end (parse, semantic and consumption analysis), with the same `# expect-error` comments. A file with none must come through with no errors |
| `stress` (built in) | Empty/huge/1 MB inputs, NUL bytes, CRLF, 100000-deep nesting of every kind, 10000-term chains (also through the analyses), 100 nested loops, throughput |
| `fuzz` (built in) | 9000 random, token-soup and mutated inputs through every stage: no crash, no hang, same result every run |

To add a test, drop a `.memo` file in the right folder and re-run CMake. For
`ast`/`tokens`, the first run creates the expected-output file and fails until
you have reviewed it. After an intended output change, regenerate it with
`MemoLangTests ast tests/ast/<name>.memo --update` and check the diff.

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
  `int` is `i64`, `float` is `f64`, `byte` is an unsigned 8-bit integer.
- Comments start with `#`, or are `/* ... */`. `//` is floor division.
- A `def` without `-> type` gets return type `auto`, inferred by semantic
  analysis. An `extern` without one gets `void`.
- `if` and `while` conditions don't need parentheses.
- Nesting deeper than 256 levels (brackets, blocks, prefix and postfix
  operators, type arguments) is an error, and parsing stops there.

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
`for`, `import`, `try`, `catch`, `throw`, `case`, `none`. Semantic analysis
also rejects `is`, `is not`, `in` and `not in` as not supported yet.

## Project layout

| Folder | Contents |
|---|---|
| `Include/` | The compiler, one folder per stage (headers and `.cpp` side by side) |
| `tests/`, `examples/` | Tests and example programs |
| `docs/` | This file, dev logs (`dev-log.md`, dated `.docx` logs), `promptdump.md` |
| `docs/reports/drafts/` | Earlier report drafts (`.tex` / `.pdf`) |
| `docs/legacy/` | Notes and code from the first, Kaleidoscope-style front-end |
| `submission-online-final/` | The submission package: final reports, `.docx` files, its own README and dev log |

## Suggested next steps

1. **Matrix runtime**: a small C++/CUDA library wrapping cuBLAS (and later the
   CUTLASS kernel) with `memo_matmul(a, b, consume_a, consume_b)`.
2. **Codegen**: lower the checked AST to LLVM IR that calls the runtime, using
   `Destructive::consumeFlags()` for the consume flags.
3. **Benchmarks** from the report: peak GPU memory and allocations with and
   without destructive reads.
4. **Shapes in the syntax** (e.g. `matrix<float, 4, 4>`), so `@` can check
   dimensions at compile time.
