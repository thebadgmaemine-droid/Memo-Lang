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

