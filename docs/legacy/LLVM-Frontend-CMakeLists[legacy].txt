cmake_minimum_required(VERSION 4.2)
add_executable(LLVM-Frontend "main.cpp")

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET LLVM-Frontend PROPERTY CXX_STANDARD 20)
endif()
target_include_directories(LLVM-Frontend PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if (TARGET LLVM::Core OR TARGET LLVM::Support)
  target_link_libraries(LLVM-Frontend PRIVATE LLVM::Support LLVM::Core LLVM::IRReader)
elseif (DEFINED llvm_libs)
  target_link_libraries(LLVM-Frontend PRIVATE ${llvm_libs})
else()
  message(FATAL_ERROR "Could not find LLVM imported targets or legacy llvm_libs")
endif()


