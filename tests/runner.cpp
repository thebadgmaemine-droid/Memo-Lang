// Test runner for the MemoLang front-end. CTest runs it once per test:
//
//   MemoLangTests ast    <file.memo> [--update]  parse; output must match <file>.ast
//   MemoLangTests tokens <file.memo> [--update]  lex; output must match <file>.tokens
//   MemoLangTests errors <file.memo>             errors must match the file's
//                                                "# expect-error" comments
//   MemoLangTests stress                         huge and deeply nested input
//   MemoLangTests fuzz                           random input must not crash or hang
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

#include <algorithm>
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
#include "Diag/diagengine.h"
#include "Lex/lexer.h"
#include "Parser/parser.h"
#include "Source/SourceManager.h"

using namespace MemoLang;

namespace {

using Clock = std::chrono::steady_clock;

double millisecondsSince(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

struct ParseResult {
    std::vector<Diagnostic> diagnostics;
    std::string diagnosticText;  // Exactly what the compiler would print.
    std::string ast;
    std::size_t statements = 0;
    double milliseconds = 0;
};

ParseResult parseSource(const std::string& moduleName, std::string text, bool printTree = true) {
    SourceManager sources(moduleName + ".memo", std::move(text));
    std::ostringstream diagOut;
    DiagnosticEngine diags(sources, diagOut);
    diags.setErrorLimit(0);

    ParseResult result;
    auto start = Clock::now();
    {
        Lexer lexer(sources, diags);
        Parser parser(lexer, diags);
        Module module = parser.parseModule(moduleName);
        result.milliseconds = millisecondsSince(start);
        result.statements = module.body.size();
        if (printTree) {
            std::ostringstream astOut;
            printAST(module, astOut);
            result.ast = astOut.str();
        }
    }  // The module is destroyed here, inside the timed run's scope.
    result.diagnostics = diags.diagnostics();
    result.diagnosticText = diagOut.str();
    return result;
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + path.string());
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::string withoutCarriageReturns(std::string text) {
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    return text;
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::string line;
    std::istringstream in(text);
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

// Compares `actual` with the expected-output file, or rewrites the file with --update.
int compareWithGolden(const std::filesystem::path& goldenPath, const std::string& actual, bool update) {
    if (update || !std::filesystem::exists(goldenPath)) {
        std::ofstream(goldenPath, std::ios::binary) << actual;
        std::cout << (update ? "updated " : "created ") << goldenPath.string() << '\n';
        return update ? 0 : 1;  // A newly created file must be reviewed first.
    }
    auto expected = splitLines(withoutCarriageReturns(readFile(goldenPath)));
    auto got = splitLines(actual);
    for (std::size_t i = 0; i < std::max(expected.size(), got.size()); ++i) {
        std::string_view e = i < expected.size() ? std::string_view(expected[i]) : "<end of file>";
        std::string_view g = i < got.size() ? std::string_view(got[i]) : "<end of output>";
        if (e != g) {
            std::cout << "FAIL: output differs from " << goldenPath.filename().string() << " at line " << i + 1
                      << "\n  expected: " << e << "\n  actual:   " << g << "\n\nFull output:\n" << actual;
            return 1;
        }
    }
    std::cout << "PASS: " << got.size() << " lines match " << goldenPath.filename().string() << '\n';
    return 0;
}

int runAst(const std::filesystem::path& file, bool update) {
    ParseResult result = parseSource(file.stem().string(), readFile(file));
    if (!result.diagnostics.empty()) {
        std::cout << "FAIL: expected no diagnostics, got:\n" << result.diagnosticText;
        return 1;
    }
    auto golden = file;
    return compareWithGolden(golden.replace_extension(".ast"), result.ast, update);
}

int runTokens(const std::filesystem::path& file, bool update) {
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
    return compareWithGolden(golden.replace_extension(".tokens"), tokens.str(), update);
}

struct Expectation {
    std::uint32_t line;
    std::uint32_t column;
    std::string text;
    bool met = false;
};

std::vector<Expectation> readExpectations(const std::string& source) {
    std::vector<Expectation> expectations;
    auto lines = splitLines(source);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        std::size_t pos = line.find("expect-error");
        if (pos == std::string::npos) continue;
        pos += std::string_view("expect-error").size();

        long long target = static_cast<long long>(i) + 1;
        if (pos < line.size() && line[pos] == '@') {
            std::size_t end = line.find(':', pos);
            target += std::stoll(line.substr(pos + 1, end - pos - 1));
            pos = end;
        }
        if (pos >= line.size() || line[pos] != ':') throw std::runtime_error("bad expect-error on line " + std::to_string(i + 1));
        std::size_t colonAfterColumn = line.find(':', pos + 1);
        auto column = static_cast<std::uint32_t>(std::stoul(line.substr(pos + 1, colonAfterColumn - pos - 1)));
        std::string text = line.substr(colonAfterColumn + 1);
        text.erase(0, text.find_first_not_of(' '));
        text.erase(text.find_last_not_of(' ') + 1);
        expectations.push_back({static_cast<std::uint32_t>(target), column, text});
    }
    return expectations;
}

int runErrors(const std::filesystem::path& file) {
    std::string source = readFile(file);
    auto expectations = readExpectations(source);
    if (expectations.empty()) {
        std::cout << "FAIL: no expect-error comments in " << file.filename().string() << '\n';
        return 1;
    }
    ParseResult result = parseSource(file.stem().string(), source, false);

    int failures = 0;
    for (const Diagnostic& d : result.diagnostics) {
        auto match = std::find_if(expectations.begin(), expectations.end(), [&](const Expectation& e) {
            return !e.met && e.line == d.range.begin.line && e.column == d.range.begin.column &&
                   d.message.find(e.text) != std::string::npos;
        });
        if (match != expectations.end()) {
            match->met = true;
        } else {
            std::cout << "FAIL: unexpected " << severityName(d.severity) << " at " << d.range.begin.line << ':'
                      << d.range.begin.column << ": " << d.message << '\n';
            ++failures;
        }
    }
    for (const Expectation& e : expectations) {
        if (e.met) continue;
        std::cout << "FAIL: missing error at " << e.line << ':' << e.column << ": " << e.text << '\n';
        ++failures;
    }
    if (failures != 0) {
        std::cout << "\nWhat the compiler printed:\n" << result.diagnosticText;
        return 1;
    }
    std::cout << "PASS: all " << expectations.size() << " expected errors reported, no others\n";
    return 0;
}

// ---------------------------------------------------------------------------
// Stress tests
// ---------------------------------------------------------------------------

std::string repeat(std::string_view piece, std::size_t count) {
    std::string out;
    out.reserve(piece.size() * count);
    for (std::size_t i = 0; i < count; ++i) out += piece;
    return out;
}

struct StressCase {
    std::string name;
    std::string source;
    std::size_t expectedErrors;
    std::string expectedMessage;  // Must appear in the first error, if any.
};

std::vector<StressCase> stressCases() {
    constexpr std::size_t kDeep = 100000;  // Far past Parser::kMaxNestingDepth.
    const std::string tooDeep = "nesting is too deep";
    const std::size_t atLimit = Parser::kMaxNestingDepth / 2 - 4;  // Comfortably under the limit.

    std::vector<StressCase> cases;
    cases.push_back({"empty file", "", 0, ""});
    cases.push_back({"only whitespace", repeat(" \t\r\n", 1000), 0, ""});
    cases.push_back({"only comments", repeat("# comment\n", 1000), 0, ""});
    cases.push_back({"no trailing newline", "def f() { return 1; }", 0, ""});
    cases.push_back({"CRLF line endings", "def f() {\r\n    return 1;\r\n}\r\n", 0, ""});
    cases.push_back({"NUL byte", std::string("int a = 1;\0int b = 2;", 21), 1, "unexpected character"});

    cases.push_back({"parentheses under the limit",
                     "def f(int x) { return " + repeat("(", atLimit) + "x" + repeat(")", atLimit) + "; }", 0, ""});
    cases.push_back({"100000 nested parentheses",
                     "def f(int x) { return " + repeat("(", kDeep) + "x" + repeat(")", kDeep) + "; }", 1, tooDeep});
    cases.push_back({"100000 unary minus", "def f(int x) { return " + repeat("- ", kDeep) + "x; }", 1, tooDeep});
    cases.push_back({"100000 unary plus", "def f(int x) { return " + repeat("+ ", kDeep) + "x; }", 1, tooDeep});
    cases.push_back({"100000 'not'", "def f(bool x) { return " + repeat("not ", kDeep) + "x; }", 1, tooDeep});
    cases.push_back({"100000 '**' (right-associative)",
                     "def f(int x) { return x" + repeat(" ** x", kDeep) + "; }", 1, tooDeep});
    cases.push_back({"100000 nested if blocks",
                     "def f(bool x) { " + repeat("if x { ", kDeep) + repeat("} ", kDeep) + "}", 1, tooDeep});
    cases.push_back({"100000 else-if chain",
                     "def f(int x) { if x == 0 { }" + repeat(" else if x == 1 { }", kDeep) + " }", 1, tooDeep});
    cases.push_back({"100000 nested type arguments",
                     "def f() { " + repeat("matrix<", kDeep) + "int" + repeat(">", kDeep) + " m; }", 1, tooDeep});
    cases.push_back({"100000 nested calls", "def f(int x) { return " + repeat("g(", kDeep) + "x" + repeat(")", kDeep) + "; }", 1, tooDeep});

    // Long but flat input: must parse with no errors.
    cases.push_back({"10000-term sum", "def f(int x) -> int { return x" + repeat(" + x", 10000) + "; }", 0, ""});
    cases.push_back({"10000-term matmul chain",
                     "def f(matrix<float> m) -> matrix<float> { return m" + repeat(" @ m", 10000) + "; }", 0, ""});
    cases.push_back({"10000 casts", "def f(int x) { return x" + repeat(" as int", 10000) + "; }", 0, ""});
    cases.push_back({"10000 call arguments", "def f(int x) { g(x" + repeat(", x", 10000) + "); }", 0, ""});
    cases.push_back({"1000 parameters", "def f(int p" + repeat(", int p", 999) + ") { }", 0, ""});
    cases.push_back({"1 MB identifier", "int " + std::string(1 << 20, 'a') + " = 1;", 0, ""});
    cases.push_back({"1 MB string literal", "str s = \"" + std::string(1 << 20, 's') + "\";", 0, ""});
    cases.push_back({"1 MB line of statements", repeat("x = 1; ", 150000), 0, ""});
    cases.push_back({"1000-digit integer", "int big = " + std::string(1000, '9') + ";", 1, "too large for int64"});
    // The first inner '{' is an error and the skip runs to EOF; then the body's '}' is missing.
    cases.push_back({"100000 unclosed '{'", "def f() " + repeat("{ ", kDeep), 2, "can't stand on its own"});
    cases.push_back({"100000 stray '}'", repeat("} ", kDeep), kDeep, "unexpected '}'"});
    cases.push_back({"100000 lexer errors", repeat("$ ", kDeep), kDeep, "unexpected character"});
    return cases;
}

int runStress() {
    int failures = 0;
    for (const StressCase& c : stressCases()) {
        // Flushed first, so a crash still shows which case caused it.
        std::printf(".... %s\r", c.name.c_str());
        std::fflush(stdout);
        ParseResult result = parseSource("stress", c.source, false);
        std::size_t errors = static_cast<std::size_t>(std::count_if(
            result.diagnostics.begin(), result.diagnostics.end(),
            [](const Diagnostic& d) { return d.severity == Severity::Error; }));
        bool ok = errors == c.expectedErrors &&
                  (c.expectedMessage.empty() || errors == 0 ||
                   result.diagnostics.front().message.find(c.expectedMessage) != std::string::npos);
        std::printf("%s %-36s %9zu bytes %8.2f ms  %zu error(s)\n", ok ? "PASS" : "FAIL", c.name.c_str(),
                    c.source.size(), result.milliseconds, errors);
        if (!ok) {
            ++failures;
            std::printf("     expected %zu error(s) starting with \"%s\"; first message: %s\n", c.expectedErrors,
                        c.expectedMessage.c_str(),
                        result.diagnostics.empty() ? "(none)" : result.diagnostics.front().message.c_str());
        }
    }

    // Line endings must not change the result.
    std::string lf = readFile(std::filesystem::path(MEMOLANG_SOURCE_DIR) / "examples" / "matmul_chain.memo");
    lf = withoutCarriageReturns(lf);
    std::string crlf;
    for (char ch : lf) {
        if (ch == '\n') crlf += '\r';
        crlf += ch;
    }
    bool same = parseSource("eol", lf).ast == parseSource("eol", crlf).ast;
    std::printf("%s LF and CRLF give the same tree\n", same ? "PASS" : "FAIL");
    if (!same) ++failures;

    // Throughput on realistic code: the matmul example repeated with fresh names.
    std::string big;
    for (int i = 0; i < 4000; ++i) {
        std::string copy = lf;
        for (std::string_view name : {"chain", "power_iteration", "scale", "memo_matmul"}) {
            std::string from = std::string(name) + "(";
            std::string to = std::string(name) + "_" + std::to_string(i) + "(";
            for (std::size_t p = copy.find(from); p != std::string::npos; p = copy.find(from, p + to.size()))
                copy.replace(p, from.size(), to);
        }
        big += copy;
    }
    ParseResult result = parseSource("throughput", big, false);
    double mb = static_cast<double>(big.size()) / (1024.0 * 1024.0);
    std::printf("%s throughput: %.1f MB, %zu lines, %zu functions in %.1f ms = %.1f MB/s\n",
                result.diagnostics.empty() ? "PASS" : "FAIL", mb,
                static_cast<std::size_t>(std::count(big.begin(), big.end(), '\n')), result.statements,
                result.milliseconds, mb / (result.milliseconds / 1000.0));
    if (!result.diagnostics.empty()) ++failures;

    return failures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
// Fuzzing: random input must never crash, hang or trip an assert. The seed is
// fixed so a failure can be reproduced; the failing input is written to disk.
// ---------------------------------------------------------------------------
int runFuzz() {
    std::mt19937 rng(20261015);
    auto pick = [&](std::size_t n) { return std::uniform_int_distribution<std::size_t>(0, n - 1)(rng); };

    const std::vector<std::string> vocabulary = {
        "def", "extern", "return", "if", "else", "while", "for", "break", "const", "static", "true",
        "false", "none", "not", "and", "or", "in", "is", "as", "import", "int", "float", "matrix",
        "bool", "x", "y", "f", "0", "1", "9223372036854775808", "1.5", "2.0f", "0x1F", "\"s\"",
        "\"unterminated", "(", ")", "{", "}", "[", "]", "<", ">", ">>", ",", ";", ":", ".", "->",
        "=", "==", "+", "++", "+=", "-", "--", "*", "**", "/", "//", "%", "@", "&", "|", "^", "~",
        "!", "$", "#", "\n", " ",
    };
    std::vector<std::string> seeds;
    for (const char* name : {"basicadd.memo", "matmul_chain.memo", "errors.memo"})
        seeds.push_back(readFile(std::filesystem::path(MEMOLANG_SOURCE_DIR) / "examples" / name));

    struct Kind {
        const char* name;
        std::function<std::string()> make;
    };
    std::vector<Kind> kinds = {
        {"random bytes",
         [&] {
             std::string s(pick(2000), '\0');
             for (char& c : s) c = static_cast<char>(pick(256));
             return s;
         }},
        {"random token soup",
         [&] {
             std::string s;
             for (std::size_t i = 0, n = pick(400); i < n; ++i) s += vocabulary[pick(vocabulary.size())] + " ";
             return s;
         }},
        {"mutated examples",
         [&] {
             std::string s = seeds[pick(seeds.size())];
             for (std::size_t i = 0, n = 1 + pick(10); i < n && !s.empty(); ++i) {
                 std::size_t at = pick(s.size());
                 switch (pick(3)) {
                 case 0: s.erase(at, 1 + pick(5)); break;
                 case 1: s.insert(at, vocabulary[pick(vocabulary.size())]); break;
                 default: s.insert(at, s.substr(pick(s.size()), pick(20))); break;
                 }
             }
             return s;
         }},
    };

    constexpr int kIterations = 3000;
    for (const Kind& kind : kinds) {
        auto start = Clock::now();
        std::size_t bytes = 0;
        std::size_t inputsWithErrors = 0;
        for (int i = 0; i < kIterations; ++i) {
            std::string input = kind.make();
            bytes += input.size();
            // Saved first, so a crash leaves the input behind for debugging.
            std::ofstream("fuzz_last_input.memo", std::ios::binary) << input;
            ParseResult first = parseSource("fuzz", input);
            ParseResult second = parseSource("fuzz", input);
            if (first.diagnosticText != second.diagnosticText || first.ast != second.ast) {
                std::printf("FAIL %s #%d: two runs on the same input differ (see fuzz_last_input.memo)\n",
                            kind.name, i);
                return 1;
            }
            if (!first.diagnostics.empty()) ++inputsWithErrors;
        }
        std::printf("PASS %-18s %d inputs, %zu KB, %zu rejected with errors, %.0f ms\n", kind.name, kIterations,
                    bytes / 1024, inputsWithErrors, millisecondsSince(start));
    }
    std::filesystem::remove("fuzz_last_input.memo");
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::vector<std::string_view> args(argv + 1, argv + argc);
        bool update = std::find(args.begin(), args.end(), "--update") != args.end();
        if (args.size() >= 2 && args[0] == "ast") return runAst(args[1], update);
        if (args.size() >= 2 && args[0] == "tokens") return runTokens(args[1], update);
        if (args.size() >= 2 && args[0] == "errors") return runErrors(args[1]);
        if (args.size() == 1 && args[0] == "stress") return runStress();
        if (args.size() == 1 && args[0] == "fuzz") return runFuzz();
        std::cerr << "usage: MemoLangTests ast|tokens|errors <file.memo> [--update] | stress | fuzz\n";
        return 2;
    } catch (const std::exception& e) {
        std::cout << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
