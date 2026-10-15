#ifndef DRIVER_H_
#define DRIVER_H_

namespace MemoLang {

// Command-line entry point: MemoLang [options] <file.memo>
// Returns the process exit code: 0 = success, 1 = compile errors, 2 = bad usage.
int runDriver(int argc, char** argv);

}  // namespace MemoLang

#endif  // DRIVER_H_
