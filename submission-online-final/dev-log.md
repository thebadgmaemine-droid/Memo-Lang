# Dev Log - September 12-16, 2026

## Progress Update

### Week 1: Toolchain Setup & AST Tree & Lexer & Parser
-- [ ExprAST.h, parser.h ]
- [] Right now, I will temporarily use LLVM to setup the front-end first. Then i'll build my own IR and back-end in ASM
- [] The LLVM toolchain was quite tricky to setup, it was original placed in a WSL folder in my machine's Ubuntu subsystem but some compilation errors later and i had to install the full 60GB file.
- [] The first week consisted of learning and applying the AST ( Abstract Syntax Tree ) Classes in the source code, in short it's the data structure which represents the logical structure
- [] LLVM Toolchain provides us with llvm::codegen(), IRBuilder ( Which will be modified later on to fit the project's scope ) letting us create and manage insertion points to generate the IR
to be passed on to the backend.

-- [ main.cpp ]
- [] The main driver is setup.



---

# Dev Log - September 18, 2026

## Progress update

## Week 2: Basic logic statement + getting started on visitors
-- [ ExprAST.h ]
-[] implementing "if" & "for" & "else" statements
-- [ ExprAST.h ] -> [ AST.h ]
-[] Reorganized project structure to contain folders: Driver, Lex, Lsp, Macro, Parser, Semantics, Source, types, Codegen; Revamped ExprAST.h to AST.h, awaiting reprogram 20/9/2026
-[] Added new corresponding source file to the header files above.
Building IRGenerator and visior pattern.
-[] Rebuilding AST.
27/9/2026
Ran a snippet:
"	\subsection{progress}
	Right now the project has only completed a part of the work. This includes the lexer, the parser,
	the AST tree as well as basic driver implmentation. As planning goes, the design has been drawn and 
	finalized, but the coding stage has not begun for features further down the pipeline because of time-constraint
	\subsection{Future work}
	Features futher down the pipeline, such as the main cuBLAS pre-compiled library integration, is set to be finished.
	The current planned features to be added to the language is as followed:
	\begin{itemize}
		\item Implement vector and matrix types
		\item Diagnostic driver and diagnostic engine implementation
		\item Code consumption analysis and handles for GPU kernel buffers/intermediates
		\item cuBLAS library linking to create an executable that can run the GPU kernel
		\item Seperate code generation from main AST tree for cleaner generation
		\item Implement optimizers.
		\item Implement macros 
	\end{itemize} 
" through AI to improve wording.
ran another snippet:
"This project serves both as an experiment to explore potential improvements to GEMM operations for
machine learning and AI algorithms, complementing the already highly-optimized cuBLAS library, and
as a response to a broader problem in the field. Current model training consumes massive amounts of
memory, and demand from AI infrastructure has outpaced global memory chip supply, contributing to a
sustained rise in DRAM and NAND prices that has placed financial pressure on IT firms, scientific
research institutions, and consumers alike. Computing algorithms for GEMM operations are already
optimized to \emph{near theoretical limits} in raw throughput; this research instead targets memory
efficiency, using a novel ownership system to reduce the memory footprint required per workload,
as one contribution toward easing the demand-side pressure driving the current memory shortage."
through claude AI to ask it to criticize the confidence of the conclusion and not to rewrite it.
Ran this snippet through AI to fix errors that made the LaTeX file not compile correct:
"
\begin{table}[htbp]
  \centering
  \caption{Comparison between Dybdahl et al.'s DRAM design and the proposed language design.}
  \label{tab:dram_vs_language}
  \small % Fits better in standard double-column or single-column paper templates
  \begin{tabular}{@{}>{\RaggedRight}p{6.5cm} >{\RaggedRight}p{6.5cm}@{}}
    \toprule
    \textbf{Dybdahl et al.'s DRAM} & \textbf{Language Design} \\
    \midrule
    Data is first read from DRAM into the cache. 
      & No difference. \\ \addlinespace
    
    Data is written back to DRAM when the cache line is replaced, regardless of whether data was modified. 
      & Operands are freed unconditionally under the assumption that the cache is always modified. \\ \addlinespace
    
    Reuses existing cache architecture. 
      & Uses scope-exit allocation for cache management. \\ \addlinespace
    
    Hides write-back latency using multi-bank overlap (bank interleaving). 
      & Hides memory free overhead behind GPU kernel launches. \\
    \bottomrule
  \end{tabular}
\end{table}


Asked AI to summarize the relevant topics detailed in the paper:
Haakon Dybdahl, Per Gunnar Kjeldsberg, Marius Grann{\ae}s, and Lasse Natvig,
		``Destructive-read in embedded DRAM, impact on power consumption,''
		\emph{Journal of Embedded Computing}, vol.~2, no.~1, pp.~83--96, 2006.

```latex
\begin{figure}[H]
		\centering
		\begin{tikzpicture}[
			node distance=5cm,
			box/.style={
				rectangle,
				draw,
				rounded corners,
				minimum width=6.3cm,
				text width=6.3cm,
				minimum height=2.6cm,
				align=left,
				font=\normalsize
			},
			special/.style={
				box,
				fill=gray!20
			},
			arr/.style={
				-{Latex[length=1.5mm]}
			}
			]
			
			\node[box] (src) at (0,0) {
				\centering\textbf{Source}\\
				\small Raw text\\
				Text is written according to the syntax.\\
				\small Any syntax error will be picked up by the diagnostic engine to be shown.
			};
			
			\node[box] (lex) at (10,0) {
				\centering\textbf{Lexer}\\
				\small Produces token stream. Each token is a tuple of
				(type, value, location), which will be used by the parser
				to build the AST.
			};
			
			\node[box] (par) at (10,-5) {
				\centering\textbf{Parser}\\
				\small Builds AST via BinopPrecedence parsing algorithm.\\
				\small The parser also performs semantic checks and reports errors.
				The language uses a recursive-descent parser.
			};
			
			\node[box] (ast) at (0,-5) {
				\centering\textbf{AST}\\
				\small Node / Expr / Statement tree\\[2mm]
				\small The AST is a tree structure that represents the source code
				in a hierarchical manner. Each node in the tree corresponds to a
				construct in the source code, such as an expression or statement.\\
				\small The AST is used by the compiler to generate LLVM IR and
				perform optimizations.
			};
			
			\node[special] (con) at (0,-10) {
				\centering\textbf{Consumption analysis [Destructive-read]}\\
				\small Marks \texttt{consume\_a} / \texttt{consume\_b}.\\
				\small Consumption analysis will be discussed in the next section.
			};
			
			\node[box] (ir) at (10,-10) {
				\centering\textbf{LLVM IR}\\
				\small Emits calls with consume flags.\\
				\small LLVM IR is a low-level intermediate representation of the
				source code used by LLVM to generate machine code. The IR is
				generated from the AST and includes information about types and
				operations. It is then passed to the JIT/AOT compiler for further
				optimization and code generation.
			};
			
			\node[box] (jit) at (10,-15) {
				\centering\textbf{JIT / AOT}\\
				\small Compiles to machine code using LLVM toolchain.
			};
			
			\node[box] (link) at (0,-15) {
				\centering\textbf{Linking}\\
				\small Links language runtime to the precompiled cuBLAS runtime
				library.The linking process combines the compiled language code
				with the precompiled cuBLAS library to create an executable that
				can be run on the GPU. 
			};
			
			\node[special] (gpu) at (0,-20) {
				\centering\textbf{GPU kernel [Destructive-read]}\\
				\small Frees consumed buffers.\\
				\small The language handles end-to-end memory management,
				including the destruction of consumed buffers. The GPU kernel
				handles matrix multiplication while the language handles memory
				management of the buffers used by the kernel.
			};
			
			\draw[arr] (src) -- (lex);
			\draw[arr] (lex) -- (par);
			\draw[arr] (par) -- (ast);
			\draw[arr] (ast) -- (con);
			\draw[arr] (con) -- (ir);
			\draw[arr] (ir) -- (jit);
			\draw[arr] (jit) -- (link);
			\draw[arr] (link) -- (gpu);

aDJUST THE SPACING
Prompt to claude 28/9/2026

---

# Dev Log - October 14, 2026

Prompt to Claude 14/10/2026:
"Can you add the requirements up above (lexer, parser and diagsengine) and suggested ideas I'd think it will be good later on: version control of the code, example program and missing parts of code/files that exists/non-exists"


---

# Dev Log - October 15, 2026

## Build and test results

Toolchain: Visual Studio 2026 Community (18), MSVC 19.51 (14.51.36231), Windows SDK 10.0.26100, CMake 4.3.1, C++20, warning level /W4.

Build:
```
cmake -S . -B build -DMEMOLANG_BUILD_TESTS=ON
cmake --build build --config Debug
cmake --build build --config Release
```
- Debug: 0 errors, 0 warnings on the first build.
- Release: 0 errors, 5 warnings (unused loop variables in assert-only loops in AST.h). Fixed with [[maybe_unused]]; now 0 warnings in both configurations.

Test run (ctest -C Debug, 16 tests):

| Test | Result |
|---|---|
| errors/lexer (10 expected errors) | PASS |
| errors/syntax (21 expected errors, each reported exactly once) | PASS |
| errors/limits (5 expected errors) | PASS |
| fuzz (9000 random / token-soup / mutated inputs) | PASS, 96 s, no crash or hang |
| examples/matmul_chain | PASS |
| examples/errors (must be rejected) | PASS |
| examples/basicadd | not started (Windows blocked the new .exe) |
| ast/* (5) and tokens/* (3) | expected-output files created on first run, then checked by hand line by line: all correct |
| stress | FAILED: stack overflow (0xC00000FD) on "100000 nested parentheses" |

Stress cases that passed before the crash: empty file, only whitespace, only comments, no trailing newline, CRLF line endings, NUL byte, 124 nested parentheses.

Bug found by the stress test: Debug builds use about 4 KB of stack per nesting level, and Windows gives a program 1 MB, so the parser crashed before reaching its own 256-level nesting limit. Fix: link with an 8 MB stack (/STACK:8388608, the Linux default). The new limit also stops parsing after one "nesting is too deep" error instead of crashing.

Not re-run yet: Windows Smart App Control (enforce mode) blocked the rebuilt MemoLang.exe and MemoLangTests.exe ("An Application Control policy has blocked this file"). The stress test after the fix, the expected-output tests, and the Release throughput numbers still need a run where the binaries are allowed.

### CI runs (GitHub Actions, branch before-commit-main)

Smart App Control blocks local runs, so the tests now run on GitHub Actions: Windows (MSVC) and Linux (GCC 13), Debug and Release.

Run 1 (commit 4c49666): Linux test step killed (exit code 143). Cause: the stress cases with 100000 errors on one 200 KB line made the diagnostics engine print the whole line under every error, which is tens of GB of output. Fix (commit 82b9c8f): show at most 120 characters of the line around the error, like clang. The stress test now also checks that each error prints at most 512 bytes.

Run 2 (commit 82b9c8f), ubuntu-latest / Debug: 15 of 16 tests passed.

| Test | Result |
|---|---|
| ast/* (5), tokens/* (3) | PASS |
| errors/lexer, errors/limits, errors/syntax | PASS |
| fuzz (9000 inputs) | PASS, 10.3 s |
| examples/basicadd, examples/matmul_chain, examples/errors | PASS |
| stress | 30 of 31 cases PASS. 100000-deep nesting of every kind now gives 1 error and no crash; the 10000-term sum/matmul chain, 1 MB identifier/string/line and 100000-error cases all pass. |
| stress: 1000-digit integer | FAIL: the error message quoted all 1000 digits (1322 bytes for one error) |

Debug (unoptimized) throughput on Linux: 3.8 MB, 112000 lines, 16000 functions parsed in 1.1 s = 3.4 MB/s.

Fix: source text quoted in an error message (literals, identifiers) is cut to 32 characters plus "...".

Run 3 (commit ee38ffc): all 4 jobs PASS (windows-latest Debug and Release, ubuntu-latest Debug and Release). All 16 tests pass on both compilers, including every stress and fuzz case.
