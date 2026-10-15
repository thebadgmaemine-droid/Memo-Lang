cmake_minimum_required(VERSION 3.20)
# Bump VERSION for each tagged release and add a matching entry to CHANGELOG.md.
project(MemoLang VERSION 0.1.0 LANGUAGES CXX)

option(MEMOLANG_BUILD_TESTS "Build MemoLang tests" OFF)
option(MEMOLANG_BUILD_EXAMPLES "Build MemoLang example programs" OFF)
# The front-end (lexer, parser, diagnostics) doesn't use LLVM, so it builds
# without it. Turn this on once code generation is written.
option(MEMOLANG_USE_LLVM "Link against LLVM and enable LLVM frontend support" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Configure LLVM (adjust LLVM_DIR if needed)
set(LLVM_DIR "C:/dev/llvm/build/lib/cmake/llvm" CACHE PATH
    "Path to the LLVM CMake package")

if(MEMOLANG_USE_LLVM)
    find_package(LLVM REQUIRED CONFIG)

    list(APPEND LLVM_INCLUDE_DIRS
        "C:/dev/llvm/build/include"
        "C:/dev/llvm/llvm/include"
    )

    message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
    message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

    include_directories(${LLVM_INCLUDE_DIRS})
    add_definitions(${LLVM_DEFINITIONS})

    llvm_map_components_to_libnames(llvm_libs support core irreader)
endif()

set(MEMOLANG_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/Include
)
file(GLOB MEMOLANG_INCLUDE_SUBDIRS LIST_DIRECTORIES true
    "${CMAKE_SOURCE_DIR}/Include/*"
)
list(APPEND MEMOLANG_INCLUDE_DIRS ${MEMOLANG_INCLUDE_SUBDIRS})

# The front-end as a library, so the compiler and the test runner share it.
add_library(MemoLangFrontend STATIC
    Include/Source/AST.cpp
    Include/Source/ASTPrinter.cpp
    Include/Source/SourceManager.cpp
    Include/Source/diags.cpp
    Include/Source/driver.cpp
    Include/Source/lexer.cpp
    Include/Source/parser.cpp
    Include/Source/token.cpp
)
target_include_directories(MemoLangFrontend PUBLIC ${MEMOLANG_INCLUDE_DIRS})
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

foreach(target MemoLangFrontend MemoLang)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endforeach()

if(MEMOLANG_USE_LLVM)
    target_include_directories(MemoLang PRIVATE ${LLVM_INCLUDE_DIRS})
    target_compile_definitions(MemoLang PRIVATE ${LLVM_DEFINITIONS})

    if (TARGET LLVM::Core OR TARGET LLVM::Support)
        target_link_libraries(MemoLang PRIVATE LLVM::Support LLVM::Core LLVM::IRReader)
    elseif (DEFINED llvm_libs)
        target_link_libraries(MemoLang PRIVATE ${llvm_libs})
    else()
        message(FATAL_ERROR "Could not find LLVM imported targets or legacy llvm_libs")
    endif()
endif()

if(MEMOLANG_BUILD_TESTS)
    enable_testing()
    add_executable(MemoLangTests tests/runner.cpp)
    target_link_libraries(MemoLangTests PRIVATE MemoLangFrontend)
    memolang_use_big_stack(MemoLangTests)
    target_compile_definitions(MemoLangTests PRIVATE MEMOLANG_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    if(MSVC)
        target_compile_options(MemoLangTests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(MemoLangTests PRIVATE -Wall -Wextra)
    endif()

    # One test per file: tests/<mode>/<name>.memo runs as "<mode>/<name>".
    foreach(mode ast tokens errors)
        file(GLOB test_files CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/${mode}/*.memo)
        foreach(test_file ${test_files})
            get_filename_component(test_name ${test_file} NAME_WE)
            add_test(NAME ${mode}/${test_name} COMMAND MemoLangTests ${mode} ${test_file})
        endforeach()
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
