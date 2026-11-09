# MemoLang: project status

Last updated: 2026-11-09 (v0.1.0)

This checklist is based on the current source tree. “Done” means implemented
and connected to the executable or test suite, not merely declared in a header.

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

This needs a C++20 compiler: MSVC 2022+, GCC 11+ or Clang 14+. LLVM is
optional for the syntax-only front end; enabling `MEMOLANG_USE_LLVM` currently
selects an unfinished code-generation path.

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
| `fuzz` (built in) | 9000 random-byte, token-soup and mutated inputs: no crash, no hang, same result every run |

To add a test, drop a `.memo` file in the right folder and re-run CMake. For
`ast`/`tokens`, the first run creates the expected-output file and fails until
you have reviewed it. After an intended output change, regenerate it with
`MemoLangTests ast tests/ast/<name>.memo --update` and check the diff.

## Functional compiler checklist

The current executable is a syntax-checking front end. It does not yet perform
semantic analysis or produce runnable output.

### Implemented and verified

- [x] C++20 front-end executable and test runner build with GCC 13.2
  (`MEMOLANG_USE_LLVM=OFF`).
- [x] Source file loading, source ranges, lexer, parser, AST, and source-aware
  diagnostics.
- [x] CLI syntax checking plus `--dump-tokens`, `--dump-ast`, `--help`, and
  `--version`.
- [x] Golden lexer, AST, and diagnostic tests; stress and deterministic fuzz
  tests; example programs. The current suite passes all 16 CTest tests.

### Required for a functional compiler

- [ ] Resolve the conflicting `//` handling: the grammar documents it as
  floor division, but the lexer also has an unreachable `//` comment branch.
  The `/* ... */` comment checks also compare a single `char` to
  multi-character constants. Specify supported comment syntax and test it.
- [ ] Implement and test the type system. `Include/Types/Typecontext.h` is an
  unfinished draft with undefined types and malformed declarations;
  `Include/Source/type.cpp` is empty. The current front-end build does not
  compile this code.
- [ ] Complete scope support. `Include/Semantics/Scope.h` declares operations,
  but `Include/Semantics/Scope.cpp` is empty.
- [ ] Implement semantic analysis: name and scope resolution, type checking,
  assignment/call/operator validation, and `auto` return-type inference.
  `Include/Semantics/sema.h` contains declarations only; there is no
  implementation or driver integration.
- [ ] Implement destructive/consumption analysis. `Analysis/destructive.h`
  currently walks the AST and records declarations as live; it does not mark
  consumed matrix values, detect invalid use, model control-flow joins, or
  report diagnostics. It is not part of the build or driver pipeline.
- [ ] Implement a valid LLVM IR lowering path and integrate it after analysis.
  `Include/IR/IRBuilder.h` is not a compilable interface, and
  `Include/IR/IRBuilder.cpp` is a stub (`getmodule()` returns null and `emit()`
  returns false). LLVM-enabled builds have not been verified.
- [ ] Add end-to-end tests that compile valid programs to output and execute
  them, and reject semantic errors before code generation.
- [ ] Choose and implement a runtime/output strategy (AOT or JIT), then add
  linking and execution tests.

### Optional or language-specific follow-ups

- [ ] Add optimization passes if needed.
- [ ] Implement the matrix runtime (cuBLAS) and CUTLASS GEMM kernel if those
  remain project requirements; neither exists in the source tree.
- [ ] Implement macros; `Include/Macro/read.md` is notes only and
  `Include/AST/query.h` contains declarations without implementations.
- [ ] Add syntax and tests for AST features not currently lexed or parsed, such
  as interpolated strings and byte/regex string flags.

### Current validation notes

- The verified command used a separate Ninja build directory and GCC because
  the available Visual Studio CMake generator could not locate an MSVC
  compiler. This does not verify the MSVC or LLVM-enabled configurations.
- GCC reports multi-character character-constant warnings in
  `Include/Lex/lexer.cpp` for the ineffective comment checks above.

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
- A `def` without `-> type` gets return type `auto` (inference is not
  implemented yet). An `extern` without one gets `void`.
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
