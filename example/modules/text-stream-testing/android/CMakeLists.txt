project(TextStreamTesting)
cmake_minimum_required(VERSION 3.9.0)

set(PACKAGE_NAME TextStreamTesting)
set(CMAKE_CXX_STANDARD 20)

add_library(${PACKAGE_NAME} SHARED
  src/main/cpp/cpp-adapter.cpp
)

include(${CMAKE_SOURCE_DIR}/../nitrogen/generated/android/TextStreamTesting+autolinking.cmake)

# The TSB1 layout, from the library (writes go through its release stores, as nitro-fetch's do).
include_directories(${CMAKE_SOURCE_DIR}/../../../../packages/react-native-text-stream/ios)
# 32-bit x86 aligns a struct's uint64_t to 4 bytes; the header starts an 8-aligned buffer.
target_compile_options(${PACKAGE_NAME} PRIVATE -Wno-atomic-alignment)

find_library(LOG_LIB log)
target_link_libraries(${PACKAGE_NAME} ${LOG_LIB} android)
