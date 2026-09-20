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