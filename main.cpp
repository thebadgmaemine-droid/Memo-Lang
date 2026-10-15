#include "Driver/driver.h"

// The old Kaleidoscope-style REPL that lived here is in git history
// (commit dae18ee, LLVM-Frontend/main.cpp).
int main(int argc, char** argv) {
    return MemoLang::runDriver(argc, argv);
}
