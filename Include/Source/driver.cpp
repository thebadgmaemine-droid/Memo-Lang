// ---------------------------------------------------------------------------
// driver.cpp: the command-line program
//
// Reads the options, then runs the pipeline:
//   SourceManager (read the file) -> Lexer -> Parser -> print the AST or errors
// Exit code: 0 = no errors, 1 = errors in the source, 2 = bad command line.
//
// History: added 2026-10-14; the old REPL that was in main.cpp is at git
// commit dae18ee. 2026-10-15: token printing moved into the lexer so the tests
// can share it, and the module is named after the file (basicadd.memo -> basicadd).
// ---------------------------------------------------------------------------
#include "Driver/driver.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#include "AST/ASTPrinter.h"
#include "Diag/diagengine.h"
#include "Lex/lexer.h"
#include "Parser/parser.h"
#include "Source/SourceManager.h"

// Set by CMake from project(VERSION ...).
#ifndef MEMOLANG_VERSION
#define MEMOLANG_VERSION "unknown"
#endif

namespace MemoLang {
namespace {

struct Options {
    std::string inputPath;
    bool dumpTokens = false;
    bool dumpAst = false;
};

void printUsage(std::ostream& out) {
    out << "Usage: MemoLang [options] <file.memo>\n"
           "\n"
           "Checks a MemoLang source file for syntax errors.\n"
           "\n"
           "Options:\n"
           "  --dump-tokens   Print the token stream instead of parsing\n"
           "  --dump-ast      Print the syntax tree after parsing\n"
           "  --version       Print the compiler version\n"
           "  --help          Show this message\n";
}

}  // namespace

int runDriver(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--dump-tokens") {
            options.dumpTokens = true;
        } else if (arg == "--dump-ast") {
            options.dumpAst = true;
        } else if (arg == "--version") {
            std::cout << "MemoLang " MEMOLANG_VERSION "\n";
            return 0;
        } else if (arg == "--help" || arg == "-h") {
            printUsage(std::cout);
            return 0;
        } else if (arg.starts_with('-')) {
            std::cerr << "error: unknown option '" << arg << "'\n";
            printUsage(std::cerr);
            return 2;
        } else if (options.inputPath.empty()) {
            options.inputPath = arg;
        } else {
            std::cerr << "error: only one input file is supported\n";
            return 2;
        }
    }
    if (options.inputPath.empty()) {
        printUsage(std::cerr);
        return 2;
    }

    std::string readError;
    auto sources = SourceManager::fromFile(options.inputPath, readError);
    if (!sources) {
        std::cerr << "error: " << readError << '\n';
        return 1;
    }

    DiagnosticEngine diags(*sources, std::cerr);
    Lexer lexer(*sources, diags);
    if (options.dumpTokens) {
        printTokens(lexer.tokenize(), std::cout);
    } else {
        Parser parser(lexer, diags);
        // The module is named after the file: examples/basicadd.memo -> basicadd.
        Module module = parser.parseModule(std::filesystem::path(options.inputPath).stem().string());
        if (options.dumpAst)
            printAST(module, std::cout);
        else if (!diags.hasErrors())
            std::cout << options.inputPath << ": OK (" << module.body.size() << " top-level statements)\n";
    }
    diags.printSummary();
    return diags.hasErrors() ? 1 : 0;
}

}  // namespace MemoLang
