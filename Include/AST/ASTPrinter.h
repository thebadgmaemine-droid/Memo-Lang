#ifndef ASTPRINTER_H_
#define ASTPRINTER_H_

#include <iosfwd>

#include "AST/AST.h"

namespace MemoLang {

// Prints the tree as indented text, one node per line. Used by --dump-ast and
// handy for checking what the parser produced.
void printAST(const Module& module, std::ostream& out);

}  // namespace MemoLang

#endif  // ASTPRINTER_H_
