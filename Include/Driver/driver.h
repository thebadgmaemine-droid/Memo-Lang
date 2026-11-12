#ifndef DRIVER_H_
#define DRIVER_H_

namespace MemoLang {

// Command-line entry point: MemoLang [options] <file.memo>
// Returns the process exit code: 0 = success, 1 = errors (in the source, or
// the input file can't be read), 2 = bad command line.
int runDriver(int argc, char **argv);

} // namespace MemoLang

#endif // DRIVER_H_
