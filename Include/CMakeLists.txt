cmake_minimum_required(VERSION 3.20)

add_executable(MemoLang "${CMAKE_SOURCE_DIR}/main.cpp")
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET MemoLang PROPERTY CXX_STANDARD 20)
endif()
target_include_directories(MemoLang PRIVATE ${CMAKE_SOURCE_DIR}/Include)

  if (TARGET LLVM::Core OR TARGET LLVM::Support)
  target_link_libraries(MemoLang PRIVATE LLVM::Support LLVM::Core LLVM::IRReader)
elseif (DEFINED llvm_libs)
  target_link_libraries(MemoLang PRIVATE ${llvm_libs})
else()
  message(FATAL_ERROR "Could not find LLVM imported targets or legacy llvm_libs")
endif()


