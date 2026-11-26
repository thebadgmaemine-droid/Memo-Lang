cmake_minimum_required(VERSION 3.20)
# Bump VERSION for each tagged release and add a matching entry to CHANGELOG.md.
project(MemoLang VERSION 0.1.0 LANGUAGES C CXX)

option(MEMOLANG_BUILD_TESTS "Build MemoLang tests" ON)
option(MEMOLANG_BUILD_EXAMPLES "Build MemoLang example programs" ON)
# The front-end (lexer, macros, parser, sema, interpreter) doesn't use LLVM,
# so it builds without it; only code generation (Include/Codegen) needs it.
# Default: ON when the LLVM version codegen is developed against (23.1) is
# installed, OFF otherwise, so machines without it (or with another LLVM,
# like the preinstalled ones on CI runners) still build. Pass
# -DMEMOLANG_USE_LLVM=ON/OFF to choose; ON accepts any LLVM version.
set(MEMOLANG_LLVM_AUTO_VERSION 23.1)
if(NOT DEFINED MEMOLANG_USE_LLVM)
  if(NOT DEFINED LLVM_DIR AND NOT WIN32
     AND EXISTS "/usr/lib/llvm-23/lib/cmake/llvm/LLVMConfig.cmake")
    set(LLVM_DIR "/usr/lib/llvm-23/lib/cmake/llvm" CACHE PATH
        "Path to the LLVM CMake package (folder containing LLVMConfig.cmake)")
  endif()
  find_package(LLVM ${MEMOLANG_LLVM_AUTO_VERSION} CONFIG QUIET)
  set(MEMOLANG_USE_LLVM_DEFAULT ${LLVM_FOUND})
  if(LLVM_FOUND)
    message(STATUS "LLVM ${LLVM_PACKAGE_VERSION} found: code generation is on")
  else()
    message(STATUS "LLVM ${MEMOLANG_LLVM_AUTO_VERSION} not found: building "
                   "without code generation (-DMEMOLANG_USE_LLVM=ON to force)")
  endif()
else()
  set(MEMOLANG_USE_LLVM_DEFAULT OFF) # Already chosen; option() keeps it.
endif()
option(MEMOLANG_USE_LLVM "Link against LLVM and build code generation"
       ${MEMOLANG_USE_LLVM_DEFAULT})

set(CMAKE_CXX_STANDARD 20) # Use Cpp 20 for compiler and tests
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# LLVM
# LLVM_DIR must point at the folder containing LLVMConfig.cmake (NOT the
# include folder). Override on the command line with -DLLVM_DIR=...
# ---------------------------------------------------------------------------
if(MEMOLANG_USE_LLVM)
  if(NOT DEFINED LLVM_DIR AND NOT WIN32
     AND EXISTS "/usr/lib/llvm-23/lib/cmake/llvm/LLVMConfig.cmake")
    set(LLVM_DIR "/usr/lib/llvm-23/lib/cmake/llvm" CACHE PATH
        "Path to the LLVM CMake package (folder containing LLVMConfig.cmake)")
  endif()

  # No hard-coded paths: LLVMConfig.cmake sets LLVM_INCLUDE_DIRS for installed
  # packages and for hand-built trees alike (a build tree's config lists both
  # its source and build include folders). On Windows, point LLVM_DIR at your
  # LLVM, e.g. -DLLVM_DIR=C:/dev/llvm/build/lib/cmake/llvm.
  find_package(LLVM REQUIRED CONFIG)

  message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
  message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

  # SYSTEM: warnings from inside LLVM's own headers aren't ours to fix.
  include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})
  # LLVM_DEFINITIONS is one space-separated string; split it into a list.
  separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
  add_definitions(${LLVM_DEFINITIONS_LIST})

  # Code generation (Include/Codegen): IR, the O1-O3 pipelines, bitcode,
  # module linking, and the host target for assembly/object files. orcjit runs
  # compiled programs in-process for the tests (MemoLangTests codegen ...).
  llvm_map_components_to_libnames(llvm_libs support core irreader analysis
                                  passes bitwriter linker target mc native
                                  orcjit)
endif()

set(MEMOLANG_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/Include
)
file(GLOB MEMOLANG_INCLUDE_ENTRIES LIST_DIRECTORIES true
    "${CMAKE_SOURCE_DIR}/Include/*"
)
# Only folders belong on the include path; the glob also returns files.
foreach(entry ${MEMOLANG_INCLUDE_ENTRIES})
  if(IS_DIRECTORY ${entry})
    list(APPEND MEMOLANG_INCLUDE_DIRS ${entry})
  endif()
endforeach()

# The matrix runtime, on its own so compiled programs can link it:
#   MemoLang -c prog.memo && c++ prog.o libMemoLangRuntime.a -o prog
# runtime_abi.cpp is the C interface generated code calls.
add_library(MemoLangRuntime STATIC
    Include/Runtime/runtime.cpp
    Include/Runtime/runtime_abi.cpp
)
target_include_directories(MemoLangRuntime PUBLIC ${MEMOLANG_INCLUDE_DIRS})

# The front-end as a library, so the compiler and the test runner share it.
# Paths updated 2026-11-06 after the 2026-11-02 reorganization moved each .cpp
# next to its header. When a file moves, update this list too.
add_library(MemoLangFrontend STATIC
    Include/Analysis/destructive.cpp
    Include/AST/AST.cpp
    Include/AST/ASTPrinter.cpp
    Include/Diag/diags.cpp
    Include/Driver/driver.cpp
    Include/Interp/interpreter.cpp
    Include/Lex/lexer.cpp
    Include/Lex/token.cpp
    Include/Macro/MacroExpander.cpp
    Include/Parser/parser.cpp
    Include/Semantics/Scope.cpp
    Include/Semantics/sema.cpp
    Include/Source/SourceManager.cpp
    Include/Types/Typecontext.cpp
)
# Code generation needs the LLVM headers, so it's built only with LLVM.
if(MEMOLANG_USE_LLVM)
  target_sources(MemoLangFrontend PRIVATE
      Include/Diag/BackendDiagnostics.cpp
      Include/Codegen/CodegenAction.cpp
      Include/Codegen/CodegenFunction.cpp
      Include/Codegen/CodegenModule.cpp
      Include/Codegen/LifetimeUtils.cpp
      Include/Codegen/Linker.cpp
      Include/Codegen/ModuleBuilder.cpp
  )
endif()
target_include_directories(MemoLangFrontend PUBLIC ${MEMOLANG_INCLUDE_DIRS})
target_link_libraries(MemoLangFrontend PUBLIC MemoLangRuntime)
if(MEMOLANG_USE_LLVM)
  # Code generation is part of the front-end library, so everything linking
  # it needs LLVM; MEMOLANG_HAS_LLVM turns on the driver's codegen options.
  target_link_libraries(MemoLangFrontend PUBLIC ${llvm_libs})
  target_compile_definitions(MemoLangFrontend PUBLIC MEMOLANG_HAS_LLVM=1)
endif()
target_compile_definitions(MemoLangFrontend PRIVATE MEMOLANG_VERSION="${PROJECT_VERSION}")

add_executable(MemoLang main.cpp)
target_link_libraries(MemoLang PRIVATE MemoLangFrontend)

# The parser and the AST passes are recursive. Windows gives a program only
# 1 MB of stack, which Debug builds use up before Parser::kMaxNestingDepth is
# reached, so ask for 8 MB, the Linux default (clang uses the same).
function(memolang_use_big_stack target)
  if(MSVC)
    target_link_options(${target} PRIVATE /STACK:8388608)
  elseif(WIN32)
    target_link_options(${target} PRIVATE -Wl,--stack,8388608)
  endif()
endfunction()
memolang_use_big_stack(MemoLang)

# Warnings. Clang (not clang-cl, which takes the MSVC branch) gets every
# warning it has (-Weverything) minus groups that don't fit this project:
#   c++98-compat*, pre-c++NN-compat   the project requires C++20
#   padded                            reports struct padding, not mistakes
#   weak-vtables                      every AST node defines its virtuals in
#                                     AST.h; out-of-line anchors buy nothing
#   switch-enum, switch-default       switches use `default:` on purpose;
#                                     -Wswitch (in -Wall) still checks them
#   unsafe-buffer-usage*              C++ "safe buffers" hardening mode
#   nrvo                              copy-elision hints, not mistakes
#   lifetime-safety-invalidation      (Clang 23, experimental) flags any
#                                     push_back on a vector passed by
#                                     reference, element held or not
# New Clang releases add warnings to -Weverything, so a compiler update can
# show new ones. GCC keeps -Wall -Wextra.
set(MEMOLANG_CLANG_WARNINGS
    -Weverything
    -Wno-c++98-compat -Wno-c++98-compat-pedantic
    -Wno-pre-c++14-compat -Wno-pre-c++17-compat -Wno-pre-c++20-compat
    -Wno-padded
    -Wno-weak-vtables
    -Wno-switch-enum -Wno-switch-default
    -Wno-unsafe-buffer-usage -Wno-unsafe-buffer-usage-in-libc-call
    -Wno-nrvo
    -Wno-lifetime-safety-invalidation
    # Options newer than the oldest supported Clang are ignored, not errors.
    -Wno-unknown-warning-option)
function(memolang_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(${target} PRIVATE ${MEMOLANG_CLANG_WARNINGS})
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra)
  endif()
endfunction()

foreach(target MemoLangRuntime MemoLangFrontend MemoLang)
  memolang_warnings(${target})
endforeach()

# LLVM's include dirs and definitions come from include_directories() and
# add_definitions() above (adding the unsplit LLVM_DEFINITIONS again produced
# -D_GLIBCXX_USE_CXX11_ABI="1 -D..."). The libraries are linked to
# MemoLangFrontend (PUBLIC) since 2026-11-17, when code generation started
# using LLVM symbols, so MemoLang and MemoLangTests both get them.

if(MEMOLANG_BUILD_TESTS)
  enable_testing()
  # benchmarks/cpp: the benchmark workloads in native C++, for `membench`.
  add_executable(MemoLangTests tests/runner.cpp
    benchmarks/cpp/workloads.cpp
    Include/IR/IRBuilder_GPU.cpp
    Include/Lex/TokenSource.h)
  target_include_directories(MemoLangTests PRIVATE ${CMAKE_SOURCE_DIR}/benchmarks/cpp)
  target_link_libraries(MemoLangTests PRIVATE MemoLangFrontend)
  memolang_use_big_stack(MemoLangTests)
  target_compile_definitions(MemoLangTests PRIVATE MEMOLANG_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  memolang_warnings(MemoLangTests)

  # One test per file: tests/<mode>/<name>.memo runs as "<mode>/<name>".
  foreach(mode ast tokens errors check run)
    file(GLOB test_files CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/${mode}/*.memo)
    foreach(test_file ${test_files})
      get_filename_component(test_name ${test_file} NAME_WE)
      add_test(NAME ${mode}/${test_name} COMMAND MemoLangTests ${mode} ${test_file})
    endforeach()
  endforeach()
  # The benchmark programs must run in both modes with the same result and no
  # leaks. (`MemoLangTests membench` prints their memory/time table.)
  file(GLOB benchmark_files CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/benchmarks/*.memo)
  foreach(benchmark_file ${benchmark_files})
    get_filename_component(benchmark_name ${benchmark_file} NAME_WE)
    add_test(NAME benchmarks/${benchmark_name}
             COMMAND MemoLangTests run ${benchmark_file})
  endforeach()
  add_test(NAME stress COMMAND MemoLangTests stress)
  add_test(NAME fuzz COMMAND MemoLangTests fuzz)
  set_tests_properties(stress fuzz PROPERTIES TIMEOUT 300)

  # The compiler itself: every example must parse cleanly...
  foreach(example basicadd matmul_chain)
    add_test(NAME examples/${example}
                 COMMAND MemoLang ${CMAKE_SOURCE_DIR}/examples/${example}.memo)
  endforeach()
  # ...and the deliberately broken one must be rejected.
  add_test(NAME examples/errors COMMAND MemoLang ${CMAKE_SOURCE_DIR}/examples/errors.memo)
  set_tests_properties(examples/errors PROPERTIES WILL_FAIL TRUE)
endif()

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
             PROPERTY VS_STARTUP_PROJECT MemoLang)