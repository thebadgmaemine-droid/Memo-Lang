// Test runner for the MemoLang front-end. CTest runs it once per test:
//
//   MemoLangTests ast    <file.memo> [--update]  parse; output must match <file>.ast
//   MemoLangTests tokens <file.memo> [--update]  lex; output must match <file>.tokens
//   MemoLangTests errors <file.memo>             errors must match the file's
//                                                "# expect-error" comments
//   MemoLangTests check  <file.memo>             like errors, through the whole
//                                                front-end (types, consumption);
//                                                no comments = must be clean
//   MemoLangTests stress                         huge and deeply nested input
//   MemoLangTests fuzz                           random input must not crash or hang
//   MemoLangTests bench [file.memo] [runs]       time each stage (not a test; run
//                                                it by hand, on a Release build)
//   MemoLangTests run <file.memo>                run main() with and without
//                                                destructive reads: same result,
//                                                "# expect-result: <value>", no leaks
//   MemoLangTests membench [files] [runs] [--save]  memory/time tables for
//                                                benchmarks/; --save writes them
//                                                to docs/results/
//
// --update rewrites the expected-output file instead of comparing. Review the
// diff before committing it.
//
// Error expectations are comments in the test file:
//   int a = 3 $ 4;   # expect-error: 15: unexpected character '$'
//   # expect-error@+1: 13: unterminated string literal
// The number is the column. "@+N" / "@-N" point N lines below / above the
// comment; otherwise the error is expected on the comment's own line. The text
// must appear in the message. Every error must be expected, and every
// expectation must be met.
//
// History: added 2026-10-15. Its stress cases found three real bugs: a stack
// overflow on deep nesting, gigabytes of output for long lines, and
// 1000-digit literals quoted in full in error messages.

#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>
#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
#include <cpuid.h>
#endif
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "AST/ASTPrinter.h"
#include "Analysis/destructive.h"
#include "Interp/interpreter.h"
#include "workloads.h" // benchmarks/cpp: the benchmarks in native C++
#include "Diag/diagengine.h"
#include "Lex/lexer.h"
#include "Macro/MacroExpander.h"
#include "Parser/parser.h"
#include "Semantics/sema.h"
#include "Source/SourceManager.h"
#include "Types/Typecontext.h"

using namespace MemoLang;

namespace {

using Clock = std::chrono::steady_clock;

double millisecondsSince(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start)
      .count();
}

struct ParseResult {
  std::vector<Diagnostic> diagnostics;
  std::string diagnosticText; // Exactly what the compiler would print.
  std::string ast;
  std::size_t statements = 0;
  double milliseconds = 0;
};

// Parses `text`. With `analyze`, a clean parse also goes through semantic and
// destructive analysis, as in the compiler.
ParseResult parseSource(const std::string &moduleName, std::string text,
                        bool printTree = true, bool analyze = false) {
  SourceManager sources(moduleName + ".memo", std::move(text));
  std::ostringstream diagOut;
  DiagnosticEngine diags(sources, diagOut);
  diags.setErrorLimit(0);

  ParseResult result;
  auto start = Clock::now();
  {
    Lexer lexer(sources, diags);
    MacroExpander expander(lexer, diags);
    Parser parser(expander, diags);
    Module module = parser.parseModule(moduleName);
    if (analyze && !diags.hasErrors()) {
      TypeContext types;
      sema::SemanticAnalyzer sema(types, diags);
      if (sema.analyzeModule(module)) {
        Destructive destructive(sema, diags);
        destructive.analyzeModule(module);
      }
    }
    result.milliseconds = millisecondsSince(start);
    result.statements = module.body.size();
    if (printTree) {
      std::ostringstream astOut;
      printAST(module, astOut);
      result.ast = astOut.str();
    }
  } // The module is destroyed here, inside the timed run's scope.
  result.diagnostics = diags.diagnostics();
  result.diagnosticText = diagOut.str();
  return result;
}

std::string readFile(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in)
    throw std::runtime_error("cannot read " + path.string());
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

std::string withoutCarriageReturns(std::string text) {
  text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
  return text;
}

std::vector<std::string> splitLines(const std::string &text) {
  std::vector<std::string> lines;
  std::string line;
  std::istringstream in(text);
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    lines.push_back(line);
  }
  return lines;
}

// Compares `actual` with the expected-output file, or rewrites the file with --update.
int compareWithGolden(const std::filesystem::path &goldenPath,
                      const std::string &actual, bool update) {
  if (update || !std::filesystem::exists(goldenPath)) {
    std::ofstream(goldenPath, std::ios::binary) << actual;
    std::cout << (update ? "updated " : "created ") << goldenPath.string()
              << '\n';
    return update ? 0 : 1; // A newly created file must be reviewed first.
  }
  auto expected = splitLines(withoutCarriageReturns(readFile(goldenPath)));
  auto got = splitLines(actual);
  for (std::size_t i = 0; i < std::max(expected.size(), got.size()); ++i) {
    std::string_view e =
        i < expected.size() ? std::string_view(expected[i]) : "<end of file>";
    std::string_view g =
        i < got.size() ? std::string_view(got[i]) : "<end of output>";
    if (e != g) {
      std::cout << "FAIL: output differs from "
                << goldenPath.filename().string() << " at line " << i + 1
                << "\n  expected: " << e << "\n  actual:   " << g
                << "\n\nFull output:\n"
                << actual;
      return 1;
    }
  }
  std::cout << "PASS: " << got.size() << " lines match "
            << goldenPath.filename().string() << '\n';
  return 0;
}

int runAst(const std::filesystem::path &file, bool update) {
  ParseResult result = parseSource(file.stem().string(), readFile(file));
  if (!result.diagnostics.empty()) {
    std::cout << "FAIL: expected no diagnostics, got:\n"
              << result.diagnosticText;
    return 1;
  }
  auto golden = file;
  return compareWithGolden(golden.replace_extension(".ast"), result.ast,
                           update);
}

int runTokens(const std::filesystem::path &file, bool update) {
  SourceManager sources(file.filename().string(), readFile(file));
  std::ostringstream diagOut;
  DiagnosticEngine diags(sources, diagOut);
  Lexer lexer(sources, diags);
  std::ostringstream tokens;
  printTokens(lexer.tokenize(), tokens);
  if (diags.hasErrors()) {
    std::cout << "FAIL: expected no lexer errors, got:\n" << diagOut.str();
    return 1;
  }
  auto golden = file;
  return compareWithGolden(golden.replace_extension(".tokens"), tokens.str(),
                           update);
}

struct Expectation {
  std::uint32_t line;
  std::uint32_t column;
  std::string text;
  bool met = false;
};

std::vector<Expectation> readExpectations(const std::string &source) {
  std::vector<Expectation> expectations;
  auto lines = splitLines(source);
  for (std::size_t i = 0; i < lines.size(); ++i) {
    const std::string &line = lines[i];
    std::size_t pos = line.find("expect-error");
    if (pos == std::string::npos)
      continue;
    pos += std::string_view("expect-error").size();

    long long target = static_cast<long long>(i) + 1;
    if (pos < line.size() && line[pos] == '@') {
      std::size_t end = line.find(':', pos);
      target += std::stoll(line.substr(pos + 1, end - pos - 1));
      pos = end;
    }
    if (pos >= line.size() || line[pos] != ':')
      throw std::runtime_error("bad expect-error on line " +
                               std::to_string(i + 1));
    std::size_t colonAfterColumn = line.find(':', pos + 1);
    auto column = static_cast<std::uint32_t>(
        std::stoul(line.substr(pos + 1, colonAfterColumn - pos - 1)));
    std::string text = line.substr(colonAfterColumn + 1);
    text.erase(0, text.find_first_not_of(' '));
    text.erase(text.find_last_not_of(' ') + 1);
    expectations.push_back({static_cast<std::uint32_t>(target), column, text});
  }
  return expectations;
}

// `errors` mode: parse only, and the file must expect at least one error.
// `check` mode (analyze = true): the whole front-end; a file with no
// expect-error comments must come through clean. Notes are not checked.
int runErrors(const std::filesystem::path &file, bool analyze = false) {
  std::string source = readFile(file);
  auto expectations = readExpectations(source);
  if (expectations.empty() && !analyze) {
    std::cout << "FAIL: no expect-error comments in "
              << file.filename().string() << '\n';
    return 1;
  }
  ParseResult result =
      parseSource(file.stem().string(), source, false, analyze);

  int failures = 0;
  for (const Diagnostic &d : result.diagnostics) {
    if (d.severity == Severity::Note)
      continue; // Notes explain an error; the error itself is what's checked.
    auto match = std::find_if(
        expectations.begin(), expectations.end(), [&](const Expectation &e) {
          return !e.met && e.line == d.range.begin.line &&
                 e.column == d.range.begin.column &&
                 d.message.find(e.text) != std::string::npos;
        });
    if (match != expectations.end()) {
      match->met = true;
    } else {
      std::cout << "FAIL: unexpected " << severityName(d.severity) << " at "
                << d.range.begin.line << ':' << d.range.begin.column << ": "
                << d.message << '\n';
      ++failures;
    }
  }
  for (const Expectation &e : expectations) {
    if (e.met)
      continue;
    std::cout << "FAIL: missing error at " << e.line << ':' << e.column << ": "
              << e.text << '\n';
    ++failures;
  }
  if (failures != 0) {
    std::cout << "\nWhat the compiler printed:\n" << result.diagnosticText;
    return 1;
  }
  if (expectations.empty())
    std::cout << "PASS: no errors, as expected\n";
  else
    std::cout << "PASS: all " << expectations.size()
              << " expected errors reported, no others\n";
  return 0;
}

// ---------------------------------------------------------------------------
// Stress tests
// ---------------------------------------------------------------------------

std::string repeat(std::string_view piece, std::size_t count) {
  std::string out;
  out.reserve(piece.size() * count);
  for (std::size_t i = 0; i < count; ++i)
    out += piece;
  return out;
}

struct StressCase {
  std::string name;
  std::string source;
  std::size_t expectedErrors;
  std::string expectedMessage; // Must appear in the first error, if any.
  bool analyze = false;        // Also run semantic and destructive analysis.
};

std::vector<StressCase> stressCases() {
  constexpr std::size_t kDeep = 100000; // Far past Parser::kMaxNestingDepth.
  const std::string tooDeep = "nesting is too deep";
  const std::size_t atLimit =
      Parser::kMaxNestingDepth / 2 - 4; // Comfortably under the limit.

  std::vector<StressCase> cases;
  cases.push_back({"empty file", "", 0, ""});
  cases.push_back({"only whitespace", repeat(" \t\r\n", 1000), 0, ""});
  cases.push_back({"only comments", repeat("# comment\n", 1000), 0, ""});
  cases.push_back({"no trailing newline", "def f() { return 1; }", 0, ""});
  cases.push_back(
      {"CRLF line endings", "def f() {\r\n    return 1;\r\n}\r\n", 0, ""});
  cases.push_back({"NUL byte", std::string("int a = 1;\0int b = 2;", 21), 1,
                   "unexpected character"});

  cases.push_back({"parentheses under the limit",
                   "def f(int x) { return " + repeat("(", atLimit) + "x" +
                       repeat(")", atLimit) + "; }",
                   0, ""});
  cases.push_back({"100000 nested parentheses",
                   "def f(int x) { return " + repeat("(", kDeep) + "x" +
                       repeat(")", kDeep) + "; }",
                   1, tooDeep});
  // Macro uses inside macro arguments recurse in the expander.
  const std::string idMacro = "macro id(x) { x }\n";
  const std::size_t argLimit = MacroExpander::kMaxExpansionDepth;
  cases.push_back({"macro arguments at the nesting limit",
                   idMacro + "def f() -> int { return " +
                       repeat("id!(", argLimit) + "1" + repeat(")", argLimit) +
                       "; }",
                   0, ""});
  cases.push_back({"100000 nested macro arguments",
                   idMacro + "def f() -> int { return " +
                       repeat("id!(", kDeep) + "1" + repeat(")", kDeep) +
                       "; }",
                   1, "macro arguments are nested too deeply"});
  cases.push_back({"100000 unary minus",
                   "def f(int x) { return " + repeat("- ", kDeep) + "x; }", 1,
                   tooDeep});
  cases.push_back({"100000 unary plus",
                   "def f(int x) { return " + repeat("+ ", kDeep) + "x; }", 1,
                   tooDeep});
  cases.push_back({"100000 'not'",
                   "def f(bool x) { return " + repeat("not ", kDeep) + "x; }",
                   1, tooDeep});
  cases.push_back({"100000 '**' (right-associative)",
                   "def f(int x) { return x" + repeat(" ** x", kDeep) + "; }",
                   1, tooDeep});
  cases.push_back({"100000 nested if blocks",
                   "def f(bool x) { " + repeat("if x { ", kDeep) +
                       repeat("} ", kDeep) + "}",
                   1, tooDeep});
  cases.push_back({"100000 else-if chain",
                   "def f(int x) { if x == 0 { }" +
                       repeat(" else if x == 1 { }", kDeep) + " }",
                   1, tooDeep});
  cases.push_back({"100000 nested type arguments",
                   "def f() { " + repeat("matrix<", kDeep) + "int" +
                       repeat(">", kDeep) + " m; }",
                   1, tooDeep});
  cases.push_back({"100000 nested calls",
                   "def f(int x) { return " + repeat("g(", kDeep) + "x" +
                       repeat(")", kDeep) + "; }",
                   1, tooDeep});

  // Long but flat input: must parse with no errors.
  cases.push_back(
      {"10000-term sum",
       "def f(int x) -> int { return x" + repeat(" + x", 10000) + "; }", 0,
       ""});
  cases.push_back({"10000-term matmul chain",
                   "def f(matrix<float> m) -> matrix<float> { return m" +
                       repeat(" @ m", 10000) + "; }",
                   0, ""});
  // Postfix chains count toward the nesting limit (2026-11-12): later passes
  // walk them recursively.
  cases.push_back({"10000 casts",
                   "def f(int x) { return x" + repeat(" as int", 10000) + "; }",
                   1, tooDeep});
  cases.push_back({"casts under the limit",
                   "def f(int x) { return x" + repeat(" as int", atLimit) + "; }",
                   0, ""});
  cases.push_back({"10000 call arguments",
                   "def f(int x) { g(x" + repeat(", x", 10000) + "); }", 0,
                   ""});
  cases.push_back({"1000 parameters",
                   "def f(int p" + repeat(", int p", 999) + ") { }", 0, ""});
  cases.push_back(
      {"1 MB identifier", "int " + std::string(1 << 20, 'a') + " = 1;", 0, ""});
  cases.push_back({"1 MB string literal",
                   "str s = \"" + std::string(1 << 20, 's') + "\";", 0, ""});
  cases.push_back(
      {"1 MB line of statements", repeat("x = 1; ", 150000), 0, ""});
  cases.push_back({"1000-digit integer",
                   "int big = " + std::string(1000, '9') + ";", 1,
                   "too large for u64"});
  // The first inner '{' is an error and the skip runs to EOF; then the body's '}' is missing.
  cases.push_back({"100000 unclosed '{'", "def f() " + repeat("{ ", kDeep), 2,
                   "can't stand on its own"});
  cases.push_back(
      {"100000 stray '}'", repeat("} ", kDeep), kDeep, "unexpected '}'"});
  cases.push_back({"100000 lexer errors", repeat("$ ", kDeep), kDeep,
                   "unexpected character"});

  // Through semantic and destructive analysis too. Long left-leaning chains
  // make the analyses recurse as deep as the chain is long.
  auto analyzed = [&](std::string name, std::string source,
                      std::size_t expectedErrors, std::string message) {
    cases.push_back({std::move(name), std::move(source), expectedErrors,
                     std::move(message), /*analyze=*/true});
  };
  analyzed("analyzed: 10000-term sum",
           "def f(int x) -> int { return x" + repeat(" + x", 10000) + "; }",
           0, "");
  analyzed("analyzed: 10000-term matmul chain of copies",
           "def f(matrix<float> m) -> matrix<float> { return copy(m)" +
               repeat(" @ copy(m)", 10000) + "; }",
           0, "");
  analyzed("analyzed: 1000 matmuls reusing one matrix",
           "def f(matrix<float> m) -> matrix<float> { return m" +
               repeat(" @ m", 1000) + "; }",
           1, "use of consumed matrix 'm'");
  analyzed("analyzed: 2000 functions",
           repeat("def g() -> int { return 1; }\n", 1) +
               [&] {
                 std::string s;
                 for (int i = 0; i < 2000; ++i)
                   // "fn_": f32 and f64 are type names.
                   s += "def fn_" + std::to_string(i) + "(int x) -> int { return g() + x; }\n";
                 return s;
               }(),
           0, "");
  analyzed("analyzed: 100 nested loops consuming",
           "def f(matrix<float> m, int n) -> matrix<float> { " +
               repeat("while n > 0 { ", 100) + "m = m @ m; " +
               repeat("} ", 100) + "return m; }",
           1, "use of consumed matrix 'm'");
  return cases;
}

int runStress() {
  int failures = 0;
  for (const StressCase &c : stressCases()) {
    // Flushed first, so a crash still shows which case caused it.
    std::printf(".... %s\r", c.name.c_str());
    std::fflush(stdout);
    ParseResult result = parseSource("stress", c.source, false, c.analyze);
    std::size_t errors = static_cast<std::size_t>(std::count_if(
        result.diagnostics.begin(), result.diagnostics.end(),
        [](const Diagnostic &d) { return d.severity == Severity::Error; }));
    // Printed output must stay small per error, even when the source line is huge.
    bool boundedOutput = result.diagnosticText.size() <= 512 * (errors + 1);
    bool ok = errors == c.expectedErrors && boundedOutput &&
              (c.expectedMessage.empty() || errors == 0 ||
               result.diagnostics.front().message.find(c.expectedMessage) !=
                   std::string::npos);
    std::printf("%s %-36s %9zu bytes %8.2f ms  %zu error(s)\n",
                ok ? "PASS" : "FAIL", c.name.c_str(), c.source.size(),
                result.milliseconds, errors);
    if (!ok) {
      ++failures;
      if (!boundedOutput)
        std::printf("     printed %zu bytes of diagnostics for %zu error(s)\n",
                    result.diagnosticText.size(), errors);
      std::printf("     expected %zu error(s) starting with \"%s\"; first "
                  "message: %s\n",
                  c.expectedErrors, c.expectedMessage.c_str(),
                  result.diagnostics.empty()
                      ? "(none)"
                      : result.diagnostics.front().message.c_str());
    }
  }

  // Line endings must not change the result.
  std::string lf = readFile(std::filesystem::path(MEMOLANG_SOURCE_DIR) /
                            "examples" / "matmul_chain.memo");
  lf = withoutCarriageReturns(lf);
  std::string crlf;
  for (char ch : lf) {
    if (ch == '\n')
      crlf += '\r';
    crlf += ch;
  }
  bool same = parseSource("eol", lf).ast == parseSource("eol", crlf).ast;
  std::printf("%s LF and CRLF give the same tree\n", same ? "PASS" : "FAIL");
  if (!same)
    ++failures;

  // Throughput on realistic code: the matmul example repeated with fresh names.
  std::string big;
  for (int i = 0; i < 4000; ++i) {
    std::string copy = lf;
    for (std::string_view name :
         {"chain", "power_iteration", "scale", "memo_matmul"}) {
      std::string from = std::string(name) + "(";
      std::string to = std::string(name) + "_" + std::to_string(i) + "(";
      for (std::size_t p = copy.find(from); p != std::string::npos;
           p = copy.find(from, p + to.size()))
        copy.replace(p, from.size(), to);
    }
    big += copy;
  }
  ParseResult result = parseSource("throughput", big, false);
  double mb = static_cast<double>(big.size()) / (1024.0 * 1024.0);
  std::printf(
      "%s throughput: %.1f MB, %zu lines, %zu functions in %.1f ms = %.1f "
      "MB/s\n",
      result.diagnostics.empty() ? "PASS" : "FAIL", mb,
      static_cast<std::size_t>(std::count(big.begin(), big.end(), '\n')),
      result.statements, result.milliseconds,
      mb / (result.milliseconds / 1000.0));
  if (!result.diagnostics.empty())
    ++failures;

  return failures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
// Running programs (interpreter). Added 2026-11-12.
// ---------------------------------------------------------------------------

// A program checked and ready to run. Owns everything the interpreter needs.
struct LoadedProgram {
  std::unique_ptr<SourceManager> sources;
  std::ostringstream diagOut;
  std::unique_ptr<DiagnosticEngine> diags;
  Module module;
  std::unique_ptr<TypeContext> types;
  std::unique_ptr<sema::SemanticAnalyzer> sema;
  std::unique_ptr<Destructive> destructive;
  bool ok = false;
};

std::unique_ptr<LoadedProgram> loadProgram(const std::filesystem::path &file) {
  auto program = std::make_unique<LoadedProgram>();
  program->sources = std::make_unique<SourceManager>(file.filename().string(),
                                                     readFile(file));
  program->diags =
      std::make_unique<DiagnosticEngine>(*program->sources, program->diagOut);
  Lexer lexer(*program->sources, *program->diags);
  MacroExpander expander(lexer, *program->diags);
  Parser parser(expander, *program->diags);
  program->module = parser.parseModule(file.stem().string());
  program->types = std::make_unique<TypeContext>();
  program->sema = std::make_unique<sema::SemanticAnalyzer>(*program->types,
                                                           *program->diags);
  program->destructive =
      std::make_unique<Destructive>(*program->sema, *program->diags);
  if (!program->diags->hasErrors() &&
      program->sema->analyzeModule(program->module))
    program->destructive->analyzeModule(program->module);
  program->ok = !program->diags->hasErrors();
  return program;
}

// The text after `key` on the first line containing it, trimmed.
std::optional<std::string> findDirective(const std::string &source,
                                         std::string_view key) {
  for (const std::string &line : splitLines(source)) {
    std::size_t pos = line.find(key);
    if (pos == std::string::npos)
      continue;
    std::string value = line.substr(pos + key.size());
    value.erase(0, value.find_first_not_of(' '));
    value.erase(value.find_last_not_of(' ') + 1);
    return value;
  }
  return std::nullopt;
}

std::string memoryLine(const char *mode, const RunReport &r) {
  char buffer[200];
  std::snprintf(buffer, sizeof buffer,
                "  %-24s peak %6.2f MiB (%llu matrices), %llu allocations, "
                "%llu frees\n",
                mode, static_cast<double>(r.stats.peakBytes) / 1048576.0,
                static_cast<unsigned long long>(r.stats.peakMatrices),
                static_cast<unsigned long long>(r.stats.allocations),
                static_cast<unsigned long long>(r.stats.frees));
  return buffer;
}

// `run` mode: runs main() with and without destructive reads. Both must give
// the same result (`# expect-result: <value>` if the file says), or fail with
// the error in `# expect-runtime-error: <text>`, and both must free every
// matrix they allocate.
int runProgramTest(const std::filesystem::path &file) {
  std::string source = readFile(file);
  auto expectResult = findDirective(source, "expect-result:");
  auto expectError = findDirective(source, "expect-runtime-error:");
  auto program = loadProgram(file);
  if (!program->ok) {
    std::cout << "FAIL: the program doesn't compile:\n"
              << program->diagOut.str();
    return 1;
  }
  RunReport base = runProgram(program->module, *program->sema,
                              *program->destructive,
                              Interpreter::Mode::Baseline);
  RunReport destr = runProgram(program->module, *program->sema,
                               *program->destructive,
                               Interpreter::Mode::Destructive);
  int failures = 0;
  auto check = [&](bool ok, const std::string &what) {
    if (!ok) {
      std::cout << "FAIL: " << what << '\n';
      ++failures;
    }
  };
  for (const auto *r : {&base, &destr}) {
    const char *mode = r == &base ? "without destructive reads"
                                  : "with destructive reads";
    if (expectError) {
      check(!r->result.ok &&
                r->result.error.find(*expectError) != std::string::npos,
            std::string(mode) + ": expected runtime error '" + *expectError +
                "', got " +
                (r->result.ok ? "result " + r->result.value
                              : "error '" + r->result.error + "'"));
    } else {
      check(r->result.ok, std::string(mode) + ": runtime error: " +
                              r->result.error);
      if (expectResult)
        check(r->result.value == *expectResult,
              std::string(mode) + ": main() returned " + r->result.value +
                  ", expected " + *expectResult);
    }
    check(r->stats.frees == r->stats.allocations,
          std::string(mode) + ": " +
              std::to_string(r->stats.allocations - r->stats.frees) +
              " matrices were never freed");
  }
  if (!expectError)
    check(base.result.value == destr.result.value,
          "the two modes returned different results: " + base.result.value +
              " and " + destr.result.value);
  std::cout << memoryLine("without destructive reads", base)
            << memoryLine("with destructive reads", destr);
  if (failures == 0)
    std::cout << "PASS: "
              << (expectError ? "the expected runtime error in both modes"
                              : "main() returned " + destr.result.value +
                                    " in both modes")
              << ", no leaks\n";
  return failures == 0 ? 0 : 1;
}

// The conditions a benchmark result is only meaningful next to.
std::string cpuName() {
  char brand[49] = {};
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
  int regs[4];
  __cpuid(regs, static_cast<int>(0x80000000));
  if (static_cast<unsigned>(regs[0]) >= 0x80000004u)
    for (int i = 0; i < 3; ++i) {
      __cpuid(regs, static_cast<int>(0x80000002u + static_cast<unsigned>(i)));
      std::memcpy(brand + 16 * i, regs, 16);
    }
#elif (defined(__x86_64__) || defined(__i386__)) && defined(__GNUC__)
  unsigned regs[4];
  for (unsigned i = 0; i < 3; ++i)
    if (__get_cpuid(0x80000002u + i, &regs[0], &regs[1], &regs[2], &regs[3]))
      std::memcpy(brand + 16 * i, regs, 16);
#endif
  std::string name(brand);
  name.erase(0, name.find_first_not_of(' '));
  name.erase(name.find_last_not_of(' ') + 1);
  return name.empty() ? "unknown CPU" : name;
}

std::string compilerName() {
#if defined(_MSC_VER) && !defined(__clang__)
  int v = _MSC_FULL_VER; // e.g. 195136260 -> 19.51.36260
  return "MSVC " + std::to_string(v / 10000000) + "." +
         std::to_string((v / 100000) % 100) + "." + std::to_string(v % 100000);
#elif defined(__clang__)
  return std::string("Clang ") + __clang_version__;
#elif defined(__GNUC__)
  return std::string("GCC ") + __VERSION__;
#else
  return "unknown compiler";
#endif
}

std::string buildType() {
#ifdef NDEBUG
  return "Release";
#else
  return "Debug (timings not representative)";
#endif
}

std::string localTime(const char *format) {
  std::time_t now = std::time(nullptr);
  std::tm parts{};
#if defined(_WIN32)
  localtime_s(&parts, &now);
#else
  localtime_r(&now, &parts);
#endif
  char buffer[64];
  std::strftime(buffer, sizeof buffer, format, &parts);
  return buffer;
}

// `membench` mode: the tables to record. Runs every program in benchmarks/
// (or the files given) four ways: MemoLang without and with destructive
// reads, and the same workload in native C++ (benchmarks/cpp/), plain and with
// matrices freed by hand. All four use the same matrix runtime, so they differ
// only in when matrices are freed and in interpreted vs native code. Memory
// numbers are exact and the same every run; times are the median of `runs`
// runs, on the CPU.
// With `save`, also writes docs/results/membench-<date>-<time>.md and appends
// every measurement to docs/results/membench.csv, with the conditions.
int runMemBench(std::vector<std::string> files, int runs, bool save) {
  if (files.empty()) {
    for (const auto &entry : std::filesystem::directory_iterator(
             std::filesystem::path(MEMOLANG_SOURCE_DIR) / "benchmarks"))
      if (entry.path().extension() == ".memo")
        files.push_back(entry.path().string());
    std::sort(files.begin(), files.end());
  }
  auto median = [](std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
  };

  struct Measured {
    bool ran = false;
    double peakMiB = 0;
    double allocations = 0;
    double ms = 0;
    std::string result;
  };
  struct Row {
    std::string name;
    Measured variant[4]; // MemoLang base, MemoLang destr, C++ plain, C++ hand.
  };
  auto fromStats = [](Measured &m, const runtime::MemoryStats &s) {
    m.ran = true;
    m.peakMiB = static_cast<double>(s.peakBytes) / 1048576.0;
    m.allocations = static_cast<double>(s.allocations);
  };

  std::vector<Row> rows;
  int failures = 0;
  for (const std::string &file : files) {
    Row row;
    row.name = std::filesystem::path(file).stem().string();
    auto program = loadProgram(file);
    if (!program->ok) {
      std::printf("%s doesn't compile:\n%s", row.name.c_str(),
                  program->diagOut.str().c_str());
      ++failures;
      continue;
    }
    // MemoLang, both modes.
    for (int v = 0; v < 2; ++v) {
      auto mode = v == 0 ? Interpreter::Mode::Baseline
                         : Interpreter::Mode::Destructive;
      std::vector<double> times;
      RunReport report;
      for (int run = 0; run < runs; ++run) {
        report = runProgram(program->module, *program->sema,
                            *program->destructive, mode);
        times.push_back(report.totalMs);
      }
      if (!report.result.ok) {
        std::printf("%s: runtime error: %s\n", row.name.c_str(),
                    report.result.error.c_str());
        ++failures;
        continue;
      }
      fromStats(row.variant[v], report.stats);
      row.variant[v].ms = median(times);
      row.variant[v].result = report.result.value;
    }
    // Native C++, both versions.
    for (const auto &workload : cppbench::workloads()) {
      if (row.name != workload.name)
        continue;
      for (int v = 0; v < 2; ++v) {
        auto function = v == 0 ? workload.plain : workload.handFreed;
        std::vector<double> times;
        runtime::MemoryStats stats;
        double value = 0;
        for (int run = 0; run < runs; ++run) {
          runtime::Runtime rt; // Same random numbers as the MemoLang runs.
          auto start = Clock::now();
          value = function(rt);
          times.push_back(millisecondsSince(start));
          stats = rt.stats();
        }
        Measured &m = row.variant[2 + v];
        fromStats(m, stats);
        m.ms = median(times);
        char buffer[64];
        std::snprintf(buffer, sizeof buffer, "%.10g", value);
        m.result = buffer;
      }
    }
    // Every version must compute the same thing.
    for (const Measured &m : row.variant) {
      if (m.ran && m.result != row.variant[1].result) {
        std::printf("%s: the versions return different results (%s vs %s)\n",
                    row.name.c_str(), m.result.c_str(),
                    row.variant[1].result.c_str());
        ++failures;
        break;
      }
    }
    rows.push_back(std::move(row));
  }

  auto table = [&](const char *title, int decimals,
                   double Measured::*field) {
    std::printf("%s\n", title);
    std::printf("  %-19s %14s %14s %14s %14s\n", "workload", "MemoLang",
                "MemoLang", "C++ (MSVC)", "C++ (MSVC)");
    std::printf("  %-19s %14s %14s %14s %14s\n", "", "base", "destructive",
                "plain", "hand-freed");
    for (const Row &row : rows) {
      std::printf("  %-19s", row.name.c_str());
      for (const Measured &m : row.variant) {
        char cell[32] = "-";
        if (m.ran)
          std::snprintf(cell, sizeof cell, "%.*f", decimals, m.*field);
        std::printf(" %14s", cell);
      }
      std::printf("\n");
    }
    std::printf("\n");
  };
  std::printf("MemoLang runtime benchmark: CPU matrix runtime, 256x256 f32 "
              "matrices, median of %d runs\n\n",
              runs);
  table("Peak matrix memory (MiB)", 2, &Measured::peakMiB);
  table("Matrix allocations", 0, &Measured::allocations);
  table("Time (ms)", 1, &Measured::ms);
  std::printf(
      "MemoLang base:        matrices freed at the end of their scope (like "
      "C++).\n"
      "MemoLang destructive: matrices freed the moment they are consumed.\n"
      "C++ plain:            native code, matrices freed at the end of their "
      "scope.\n"
      "C++ hand-freed:       native code, each matrix freed by hand after its "
      "last use.\n"
      "All four compute the same results. Memory numbers carry over to a "
      "GPU; CPU\n"
      "times don't (GPU timing needs the cuBLAS version on an NVIDIA GPU).\n");

  std::string conditions = cpuName() + ", " + compilerName() + ", " +
                           buildType() + ", median of " +
                           std::to_string(runs) + " runs";
  std::printf("\nConditions: %s\n", conditions.c_str());
  if (!save)
    return failures == 0 ? 0 : 1;

  // Save the results, exactly as measured.
  static const char *variantNames[4] = {"MemoLang base", "MemoLang destructive",
                                        "C++ plain", "C++ hand-freed"};
  auto resultsDir = std::filesystem::path(MEMOLANG_SOURCE_DIR) / "docs" /
                    "results";
  std::filesystem::create_directories(resultsDir);
  std::string stamp = localTime("%Y-%m-%d %H:%M");
  auto mdPath = resultsDir / ("membench-" + localTime("%Y-%m-%d-%H%M") + ".md");
  {
    std::ofstream md(mdPath, std::ios::binary);
    md << "# MemoLang runtime benchmark, " << stamp << "\n\n"
       << "Written by `MemoLangTests membench --save`. Do not edit by hand.\n\n"
       << "- Conditions: " << conditions << "\n"
       << "- Matrices: 256x256 f32 (0.25 MiB each), CPU matrix runtime\n"
       << "- MemoLang base: matrices freed at the end of their scope (like C++)\n"
       << "- MemoLang destructive: matrices freed the moment they are "
          "consumed\n"
       << "- C++ plain: native code, freed at the end of scope\n"
       << "- C++ hand-freed: native code, each matrix freed by hand after its "
          "last use\n"
       << "- All four versions of each workload returned the same result"
       << (failures == 0 ? "" : " (EXCEPT where noted as failed)") << "\n\n";
    auto mdTable = [&](const char *title, int decimals,
                       double Measured::*field) {
      md << "## " << title << "\n\n| Workload |";
      for (const char *v : variantNames)
        md << ' ' << v << " |";
      md << "\n|---|---|---|---|---|\n";
      for (const Row &row : rows) {
        md << "| `" << row.name << "` |";
        for (const Measured &m : row.variant) {
          char cell[32] = "-";
          if (m.ran)
            std::snprintf(cell, sizeof cell, "%.*f", decimals, m.*field);
          md << ' ' << cell << " |";
        }
        md << '\n';
      }
      md << '\n';
    };
    mdTable("Peak matrix memory (MiB)", 2, &Measured::peakMiB);
    mdTable("Matrix allocations", 0, &Measured::allocations);
    mdTable("Time (ms, CPU; noisy, see note)", 1, &Measured::ms);
    md << "Memory numbers are exact and carry over to a GPU. CPU times vary "
          "between\nidentical runs and don't carry over; GPU timing needs the "
          "cuBLAS version.\n";
  }
  auto csvPath = resultsDir / "membench.csv";
  bool newCsv = !std::filesystem::exists(csvPath);
  {
    std::ofstream csv(csvPath, std::ios::binary | std::ios::app);
    if (newCsv)
      csv << "date,cpu,compiler,build,runs,workload,variant,peak_mib,"
             "allocations,time_ms,result\n";
    for (const Row &row : rows)
      for (int v = 0; v < 4; ++v) {
        const Measured &m = row.variant[v];
        if (!m.ran)
          continue;
        char numbers[96];
        std::snprintf(numbers, sizeof numbers, "%.4f,%.0f,%.2f", m.peakMiB,
                      m.allocations, m.ms);
        csv << stamp << ",\"" << cpuName() << "\",\"" << compilerName()
            << "\"," << buildType() << ',' << runs << ',' << row.name << ','
            << variantNames[v] << ',' << numbers << ',' << m.result << '\n';
      }
  }
  std::printf("Saved: %s\n       %s (appended)\n", mdPath.string().c_str(),
              csvPath.string().c_str());
  return failures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
// Benchmark: how long each compiler stage takes on one file. Not a test (CTest
// doesn't run it); run it by hand on a Release build. Without a file it builds
// a realistic one: examples/matmul_chain.memo repeated 3000 times with renamed
// functions (about 3.6 MB, 93000 lines, 12000 functions).
// Added 2026-11-12.
// ---------------------------------------------------------------------------
int runBench(const std::string &path, int runs) {
  std::string text;
  if (path.empty()) {
    std::string example = withoutCarriageReturns(readFile(
        std::filesystem::path(MEMOLANG_SOURCE_DIR) / "examples" /
        "matmul_chain.memo"));
    for (int i = 0; i < 3000; ++i) {
      std::string copy = example;
      for (std::string_view name :
           {"chain", "power_iteration", "scale", "memo_matmul"}) {
        std::string from = std::string(name) + "(";
        std::string to = std::string(name) + "_" + std::to_string(i) + "(";
        for (std::size_t p = copy.find(from); p != std::string::npos;
             p = copy.find(from, p + to.size()))
          copy.replace(p, from.size(), to);
      }
      text += copy;
    }
  } else {
    text = readFile(path);
  }

  auto median = [](std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
  };
  std::vector<double> parse, semantic, destructive, total;
  for (int run = 0; run < runs; ++run) {
    SourceManager sources("bench.memo", text);
    std::ostringstream diagOut;
    DiagnosticEngine diags(sources, diagOut);
    auto t0 = Clock::now();
    Lexer lexer(sources, diags);
    MacroExpander expander(lexer, diags);
    Parser parser(expander, diags);
    Module module = parser.parseModule("bench");
    auto t1 = Clock::now();
    TypeContext types;
    sema::SemanticAnalyzer analyzer(types, diags);
    bool ok = !diags.hasErrors() && analyzer.analyzeModule(module);
    auto t2 = Clock::now();
    if (ok) {
      Destructive consumption(analyzer, diags);
      consumption.analyzeModule(module);
    }
    auto t3 = Clock::now();
    if (!ok || diags.hasErrors()) {
      std::cout << "FAIL: the benchmark file has errors:\n" << diagOut.str();
      return 1;
    }
    auto ms = [](Clock::time_point a, Clock::time_point b) {
      return std::chrono::duration<double, std::milli>(b - a).count();
    };
    parse.push_back(ms(t0, t1));
    semantic.push_back(ms(t1, t2));
    destructive.push_back(ms(t2, t3));
    total.push_back(ms(t0, t3));
  }

  double totalMs = median(total);
  double mb = static_cast<double>(text.size()) / (1024.0 * 1024.0);
  std::size_t lines =
      static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n'));
  std::printf("MemoLang front-end benchmark: %.1f MB, %zu lines, median of %d "
              "runs\n\n",
              mb, lines, runs);
  std::printf("  stage                     time   share\n");
  std::printf("  lexer + parser       %8.1f ms   %4.1f%%\n", median(parse),
              100.0 * median(parse) / totalMs);
  std::printf("  semantic analysis    %8.1f ms   %4.1f%%\n", median(semantic),
              100.0 * median(semantic) / totalMs);
  std::printf("  destructive reads    %8.1f ms   %4.1f%%\n", median(destructive),
              100.0 * median(destructive) / totalMs);
  std::printf("  total                %8.1f ms\n\n", totalMs);
  std::printf("  throughput           %8.1f MB/s, %.0f lines/s\n",
              mb / (totalMs / 1000.0),
              static_cast<double>(lines) / (totalMs / 1000.0));
  return 0;
}

// ---------------------------------------------------------------------------
// Fuzzing: random input must never crash, hang or trip an assert. The seed is
// fixed so a failure can be reproduced; the failing input is written to disk.
// ---------------------------------------------------------------------------
int runFuzz() {
  std::mt19937 rng(20261015);
  auto pick = [&](std::size_t n) {
    return std::uniform_int_distribution<std::size_t>(0, n - 1)(rng);
  };

  const std::vector<std::string> vocabulary = {
      "def",   "extern", "return", "if",    "else",
      "while", "for",    "break",  "const", "static",
      "true",  "false",  "none",   "not",   "and",
      "or",    "in",     "is",     "as",    "import",
      "int",   "float",  "matrix", "bool",  "x",
      "y",     "f",      "0",      "1",     "9223372036854775808",
      "1.5",   "2.0f",   "0x1F",   "\"s\"", "\"unterminated",
      "(",     ")",      "{",      "}",     "[",
      "]",     "<",      ">",      ">>",    ",",
      ";",     ":",      ".",      "->",    "=",
      "==",    "+",      "++",     "+=",    "-",
      "--",    "*",      "**",     "/",     "//",
      "%",     "@",      "&",      "|",     "^",
      "~",     "!",      "$",      "#",     "\n",
      " ",
  };
  std::vector<std::string> seeds;
  for (const char *name : {"basicadd.memo", "matmul_chain.memo", "errors.memo"})
    seeds.push_back(readFile(std::filesystem::path(MEMOLANG_SOURCE_DIR) /
                             "examples" / name));

  struct Kind {
    const char *name;
    std::function<std::string()> make;
  };
  std::vector<Kind> kinds = {
      {"random bytes",
       [&] {
         std::string s(pick(2000), '\0');
         for (char &c : s)
           c = static_cast<char>(pick(256));
         return s;
       }},
      {"random token soup",
       [&] {
         std::string s;
         for (std::size_t i = 0, n = pick(400); i < n; ++i)
           s += vocabulary[pick(vocabulary.size())] + " ";
         return s;
       }},
      {"mutated examples",
       [&] {
         std::string s = seeds[pick(seeds.size())];
         for (std::size_t i = 0, n = 1 + pick(10); i < n && !s.empty(); ++i) {
           std::size_t at = pick(s.size());
           switch (pick(3)) {
           case 0:
             s.erase(at, 1 + pick(5));
             break;
           case 1:
             s.insert(at, vocabulary[pick(vocabulary.size())]);
             break;
           default:
             s.insert(at, s.substr(pick(s.size()), pick(20)));
             break;
           }
         }
         return s;
       }},
  };

  constexpr int kIterations = 3000;
  for (const Kind &kind : kinds) {
    auto start = Clock::now();
    std::size_t bytes = 0;
    std::size_t inputsWithErrors = 0;
    for (int i = 0; i < kIterations; ++i) {
      std::string input = kind.make();
      bytes += input.size();
      // Saved first, so a crash leaves the input behind for debugging.
      std::ofstream("fuzz_last_input.memo", std::ios::binary) << input;
      ParseResult first = parseSource("fuzz", input, true, /*analyze=*/true);
      ParseResult second = parseSource("fuzz", input, true, /*analyze=*/true);
      if (first.diagnosticText != second.diagnosticText ||
          first.ast != second.ast) {
        std::printf("FAIL %s #%d: two runs on the same input differ (see "
                    "fuzz_last_input.memo)\n",
                    kind.name, i);
        return 1;
      }
      if (!first.diagnostics.empty())
        ++inputsWithErrors;
    }
    std::printf(
        "PASS %-18s %d inputs, %zu KB, %zu rejected with errors, %.0f ms\n",
        kind.name, kIterations, bytes / 1024, inputsWithErrors,
        millisecondsSince(start));
  }
  std::filesystem::remove("fuzz_last_input.memo");
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  try {
    std::vector<std::string_view> args(argv + 1, argv + argc);
    bool update = std::find(args.begin(), args.end(), "--update") != args.end();
    if (args.size() >= 2 && args[0] == "ast")
      return runAst(args[1], update);
    if (args.size() >= 2 && args[0] == "tokens")
      return runTokens(args[1], update);
    if (args.size() >= 2 && args[0] == "errors")
      return runErrors(args[1]);
    if (args.size() >= 2 && args[0] == "check")
      return runErrors(args[1], /*analyze=*/true);
    if (args.size() == 1 && args[0] == "stress")
      return runStress();
    if (args.size() == 1 && args[0] == "fuzz")
      return runFuzz();
    if (!args.empty() && args[0] == "bench") {
      // bench [file.memo] [runs]: a number alone is the run count.
      std::string file;
      int runs = 10;
      for (std::size_t i = 1; i < args.size(); ++i) {
        std::string arg(args[i]);
        if (!arg.empty() && std::all_of(arg.begin(), arg.end(), ::isdigit))
          runs = std::max(1, std::stoi(arg));
        else
          file = arg;
      }
      return runBench(file, runs);
    }
    if (args.size() >= 2 && args[0] == "run")
      return runProgramTest(args[1]);
    if (!args.empty() && args[0] == "membench") {
      // membench [files...] [runs] [--save]: a number alone is the run count.
      std::vector<std::string> files;
      int runs = 5;
      bool save = false;
      for (std::size_t i = 1; i < args.size(); ++i) {
        std::string arg(args[i]);
        if (arg == "--save")
          save = true;
        else if (!arg.empty() && std::all_of(arg.begin(), arg.end(), ::isdigit))
          runs = std::max(1, std::stoi(arg));
        else
          files.push_back(arg);
      }
      return runMemBench(std::move(files), runs, save);
    }
    std::cerr << "usage: MemoLangTests ast|tokens|errors|check|run <file.memo> "
                 "[--update] | stress | fuzz | bench [file.memo] [runs] | "
                 "membench [files...] [runs] [--save]\n";
    return 2;
  } catch (const std::exception &e) {
    std::cout << "FAIL: " << e.what() << '\n';
    return 1;
  }
}
