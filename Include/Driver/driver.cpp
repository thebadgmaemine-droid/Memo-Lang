// ---------------------------------------------------------------------------
// driver.cpp: the command-line program
//
// Reads the options, then runs the pipeline:
//   SourceManager (read the file) -> Lexer -> Parser -> semantic analysis
//   -> destructive analysis -> print the AST, symbols or errors
// Each analysis runs only if the stage before it found no errors.
// Exit code: 0 = no errors, 1 = errors in the source or an unreadable input
// file, 2 = bad command line.
//
// History: added 2026-10-14; the old REPL that was in main.cpp is at git
// commit dae18ee. 2026-10-15: token printing moved into the lexer so the tests
// can share it, and the module is named after the file (basicadd.memo -> basicadd).
// 2026-11-12: runs semantic and destructive analysis; --parse-only and
// --dump-symbols added.
// ---------------------------------------------------------------------------
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#include "AST/ASTPrinter.h"
#include "Analysis/destructive.h"
#include "Diag/diagengine.h"
#include "Driver/driver.h"
#include "Interp/interpreter.h"
#include "Lex/lexer.h"
#include "Parser/parser.h"
#include "Semantics/sema.h"
#include "Source/SourceManager.h"
#include "Types/Typecontext.h"

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
  bool dumpSymbols = false;
  bool parseOnly = false;
  bool run = false;
  bool noDestructive = false;
  bool compare = false;
};

std::string mib(std::uint64_t bytes) {
  char buffer[32];
  std::snprintf(buffer, sizeof buffer, "%.2f MiB",
                static_cast<double>(bytes) / (1024.0 * 1024.0));
  return buffer;
}

std::string percentChange(double before, double after) {
  if (before == 0)
    return "";
  char buffer[32];
  std::snprintf(buffer, sizeof buffer, "%+.1f%%",
                100.0 * (after - before) / before);
  return buffer;
}

void printRun(const RunReport &report, std::ostream &out) {
  const auto &s = report.stats;
  char line[160];
  std::snprintf(line, sizeof line,
                "memory: peak %s (%llu matrices), %llu allocations, %llu "
                "frees, %s allocated in total\n",
                mib(s.peakBytes).c_str(),
                static_cast<unsigned long long>(s.peakMatrices),
                static_cast<unsigned long long>(s.allocations),
                static_cast<unsigned long long>(s.frees),
                mib(s.bytesAllocated).c_str());
  out << line;
  std::snprintf(line, sizeof line,
                "time:   %.1f ms in total, %.1f ms in matrix multiplication\n",
                report.totalMs, s.matmulMs);
  out << line;
}

void printComparison(const RunReport &base, const RunReport &destr,
                     std::ostream &out) {
  char line[160];
  auto row = [&](const char *what, const std::string &a, const std::string &b,
                 double x, double y) {
    std::snprintf(line, sizeof line, "  %-28s %16s %16s %10s\n", what,
                  a.c_str(), b.c_str(), percentChange(x, y).c_str());
    out << line;
  };
  auto count = [](std::uint64_t n) { return std::to_string(n); };
  auto msText = [](double ms) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.1f ms", ms);
    return std::string(buffer);
  };
  const auto &b = base.stats;
  const auto &d = destr.stats;
  std::snprintf(line, sizeof line, "  %-28s %16s %16s %10s\n", "",
                "without destr.", "with destr.", "change");
  out << line;
  row("peak memory", mib(b.peakBytes), mib(d.peakBytes),
      static_cast<double>(b.peakBytes), static_cast<double>(d.peakBytes));
  row("peak live matrices", count(b.peakMatrices), count(d.peakMatrices),
      static_cast<double>(b.peakMatrices), static_cast<double>(d.peakMatrices));
  row("allocations", count(b.allocations), count(d.allocations),
      static_cast<double>(b.allocations), static_cast<double>(d.allocations));
  row("memory allocated in total", mib(b.bytesAllocated),
      mib(d.bytesAllocated), static_cast<double>(b.bytesAllocated),
      static_cast<double>(d.bytesAllocated));
  row("total time (CPU)", msText(base.totalMs), msText(destr.totalMs),
      base.totalMs, destr.totalMs);
  row("  in matrix multiplication", msText(b.matmulMs), msText(d.matmulMs),
      b.matmulMs, d.matmulMs);
}

void printUsage(std::ostream &out) {
  out << "Usage: MemoLang [options] <file.memo>\n"
         "\n"
         "Checks a MemoLang source file: syntax, types, and that no matrix is\n"
         "used after it was consumed.\n"
         "\n"
         "Options:\n"
         "  --dump-tokens   Print the token stream instead of parsing\n"
         "  --dump-ast      Print the syntax tree\n"
         "  --dump-symbols  Print every declared name and its type\n"
         "  --parse-only    Check syntax only (skip type and consumption checks)\n"
         "  --run           Run main() with destructive reads; print memory use\n"
         "  --no-destructive  With --run: free matrices at the end of their\n"
         "                  scope instead (the baseline)\n"
         "  --compare       Run main() both ways and compare memory and time\n"
         "  --version       Print the compiler version\n"
         "  --help          Show this message\n";
}

} // namespace

int runDriver(int argc, char **argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    std::string_view arg = argv[i];
    if (arg == "--dump-tokens") {
      options.dumpTokens = true;
    } else if (arg == "--dump-ast") {
      options.dumpAst = true;
    } else if (arg == "--dump-symbols") {
      options.dumpSymbols = true;
    } else if (arg == "--parse-only") {
      options.parseOnly = true;
    } else if (arg == "--run") {
      options.run = true;
    } else if (arg == "--no-destructive") {
      options.noDestructive = true;
    } else if (arg == "--compare") {
      options.compare = true;
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
    Module module = parser.parseModule(
        std::filesystem::path(options.inputPath).stem().string());

    TypeContext types;
    sema::SemanticAnalyzer sema(types, diags);
    Destructive destructive(sema, diags);
    if (!options.parseOnly && !diags.hasErrors() &&
        sema.analyzeModule(module))
      destructive.analyzeModule(module);

    if (options.dumpAst)
      printAST(module, std::cout);
    if (options.dumpSymbols)
      std::cout << sema.dumpSymbolTable();

    bool running = options.run || options.compare;
    if (running && !options.parseOnly && !diags.hasErrors()) {
      if (options.compare) {
        RunReport base = runProgram(module, sema, destructive,
                                    Interpreter::Mode::Baseline);
        RunReport destr = runProgram(module, sema, destructive,
                                     Interpreter::Mode::Destructive);
        if (!base.result.ok || !destr.result.ok) {
          std::cerr << "runtime error: "
                    << (base.result.ok ? destr.result.error
                                       : base.result.error)
                    << '\n';
          return 1;
        }
        std::cout << options.inputPath << ": main() returned "
                  << destr.result.value;
        if (base.result.value != destr.result.value)
          std::cout << " (" << base.result.value
                    << " without destructive reads: the results differ!)";
        std::cout << "\n\n";
        printComparison(base, destr, std::cout);
      } else {
        RunReport report = runProgram(module, sema, destructive,
                                      options.noDestructive
                                          ? Interpreter::Mode::Baseline
                                          : Interpreter::Mode::Destructive);
        if (!report.result.ok) {
          std::cerr << "runtime error: " << report.result.error << '\n';
          return 1;
        }
        std::cout << "main() returned " << report.result.value << '\n';
        printRun(report, std::cout);
      }
    } else if (!options.dumpAst && !options.dumpSymbols &&
               !diags.hasErrors()) {
      std::cout << options.inputPath << ": OK (" << module.body.size()
                << " top-level statements)\n";
    }
  }
  diags.printSummary();
  return diags.hasErrors() ? 1 : 0;
}

} // namespace MemoLang
