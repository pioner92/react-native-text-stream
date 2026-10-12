# Built by the app through autolinking (react-native.config.js): the shadow node, the
# StreamTextBuffers TurboModule (cpp/) and the JNI the Kotlin view reads its buffer through.
cmake_minimum_required(VERSION 3.13)
set(CMAKE_VERBOSE_MAKEFILE ON)

set(LIB_TARGET_NAME react_codegen_textstream)
set(LIB_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../..)

add_library(
  ${LIB_TARGET_NAME}
  SHARED
  ${LIB_ROOT_DIR}/cpp/StreamTextBuffers.cpp
  ${LIB_ROOT_DIR}/cpp/StreamTextMappedBuffer.cpp
  ${LIB_ROOT_DIR}/cpp/StreamTextFetch.cpp
  ${LIB_ROOT_DIR}/cpp/StreamTextStreams.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/StreamTextFetchAndroid.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/textstream.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/StreamTextPreparedAndroid.cpp
)

target_include_directories(
  ${LIB_TARGET_NAME}
  PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${LIB_ROOT_DIR}/cpp
)

target_link_libraries(
  ${LIB_TARGET_NAME}
  fbjni
  jsi
  reactnative
)

target_compile_reactnative_options(${LIB_TARGET_NAME} PUBLIC)
# 32-bit x86 aligns a struct's uint64_t to 4 bytes, so clang can't prove TSBHeader.committed is
# 8-aligned; it is (the header starts the buffer, a page-aligned mapping).
target_compile_options(${LIB_TARGET_NAME} PRIVATE -Wno-atomic-alignment)
