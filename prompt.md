## ALL PROMPTS SHOULD BE DUMPED HERE FOR RESEARCH INTEGRITY 


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