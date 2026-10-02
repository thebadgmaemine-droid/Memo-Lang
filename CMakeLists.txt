cmake_minimum_required(VERSION 3.20)
project(MemoLang LANGUAGES CXX)

option(MEMOLANG_BUILD_TESTS "Build MemoLang tests" OFF)
option(MEMOLANG_BUILD_EXAMPLES "Build MemoLang example programs" OFF)
option(MEMOLANG_USE_LLVM "Link against LLVM and enable LLVM frontend support" ON)

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

add_executable(MemoLang main.cpp Include/Source/AST.cpp)

set(MEMOLANG_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/Include
)
file(GLOB MEMOLANG_INCLUDE_SUBDIRS LIST_DIRECTORIES true
    "${CMAKE_SOURCE_DIR}/Include/*"
)
list(APPEND MEMOLANG_INCLUDE_DIRS ${MEMOLANG_INCLUDE_SUBDIRS})

target_include_directories(MemoLang PRIVATE ${MEMOLANG_INCLUDE_DIRS})

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
endif()

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
             PROPERTY VS_STARTUP_PROJECT MemoLang)
