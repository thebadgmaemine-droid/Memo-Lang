#ifndef MEMOLANG_SUPPORT_COMPILER_H_
#define MEMOLANG_SUPPORT_COMPILER_H_
// ---------------------------------------------------------------------------
// Compiler-specific annotations, spelled so every supported compiler (Clang,
// GCC, MSVC) accepts them.
//
// History: added 2026-11-26 for Clang's -Wlifetime-safety-*-suggestions.
// ---------------------------------------------------------------------------

// MEMOLANG_LIFETIMEBOUND: the result refers into this parameter (or into
// `*this`, after a member function's qualifiers), so it must not outlive it.
// Clang then warns about dangling uses such as
//   const std::string &name = makeNode()->name(); // node destroyed here
// Other compilers don't know the attribute and would warn about it, so for
// them it expands to nothing.
//
//   const std::string &name() const MEMOLANG_LIFETIMEBOUND { return name_; }
//   Foo(Bar &bar MEMOLANG_LIFETIMEBOUND) : bar_(bar) {}
#if defined(__has_cpp_attribute)
#if __has_cpp_attribute(clang::lifetimebound)
#define MEMOLANG_LIFETIMEBOUND [[clang::lifetimebound]]
#endif
#endif
#ifndef MEMOLANG_LIFETIMEBOUND
#define MEMOLANG_LIFETIMEBOUND
#endif

#endif // MEMOLANG_SUPPORT_COMPILER_H_
