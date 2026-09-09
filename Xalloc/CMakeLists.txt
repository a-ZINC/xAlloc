# CMakeList.txt : CMake project for Xalloc, include source and define
# project specific logic here.
#

# Add source to this project's executable.
include_directories(include)

add_library(XallocLib src/MonotonicBrk.cpp include/Xalloc.h)

add_executable(Benchmark test/Benchmark.cpp)

target_link_libraries(Benchmark PRIVATE XallocLib)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET XallocLib PROPERTY CXX_STANDARD 20)
  set_property(TARGET Benchmark PROPERTY CXX_STANDARD 20)
endif()

option(ENABLE_ASAN "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)

if (ENABLE_ASAN)
  set(SANITIZE_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
  foreach(TARGET XallocLib Benchmark)
    target_compile_options(${TARGET} PRIVATE ${SANITIZE_FLAGS})
    target_link_options(${TARGET} PRIVATE ${SANITIZE_FLAGS})
  endforeach()
endif()

# TODO: Add tests and install targets if needed.
