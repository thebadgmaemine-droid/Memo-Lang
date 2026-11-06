#include "AST/AST.h"

// 2026-10-14: namespace fixed. It was `Memolang` (lower-case l), which C++
// treats as a different namespace from `MemoLang`.
//
// 2026-11-06: restored this stub to fix the build. The 2026-11-02 version
// (commit eb56fa0) started moving node definitions out of AST.h, but AST.h
// still defines them inline, so every definition here was a duplicate; it also
// used the `Memolang` namespace again and members that don't exist (regex(),
// isCast()). To finish that move later, take the code from eb56fa0 and remove
// each definition from AST.h at the same time as adding it here.
namespace MemoLang {

// All AST nodes are currently defined inline in AST.h.
// Out-of-line definitions go here.

}  // namespace MemoLang
