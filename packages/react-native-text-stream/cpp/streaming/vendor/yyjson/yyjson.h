/*==============================================================================
 Copyright (c) 2020 YaoYuan <ibireme@gmail.com>
 
 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:
 
 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 SOFTWARE.
 *============================================================================*/

/** 
 @file yyjson.h
 @date 2019-03-09
 @author YaoYuan
 */

#ifndef YYJSON_H
#define YYJSON_H



/*==============================================================================
 * Header Files
 *============================================================================*/

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <limits.h>
#include <string.h>
#include <float.h>



/*==============================================================================
 * Compile-time Options
 *============================================================================*/

/*
 Define as 1 to disable JSON reader if JSON parsing is not required.
 
 This will disable these functions at compile-time:
    - styyjson_read()
    - styyjson_read_opts()
    - styyjson_read_file()
    - styyjson_read_number()
    - styyjson_mut_read_number()
 
 This will reduce the binary size by about 60%.
 */
#ifndef YYJSON_DISABLE_READER
#endif

/*
 Define as 1 to disable JSON writer if JSON serialization is not required.
 
 This will disable these functions at compile-time:
    - styyjson_write()
    - styyjson_write_file()
    - styyjson_write_opts()
    - styyjson_val_write()
    - styyjson_val_write_file()
    - styyjson_val_write_opts()
    - styyjson_mut_write()
    - styyjson_mut_write_file()
    - styyjson_mut_write_opts()
    - styyjson_mut_val_write()
    - styyjson_mut_val_write_file()
    - styyjson_mut_val_write_opts()
 
 This will reduce the binary size by about 30%.
 */
#ifndef YYJSON_DISABLE_WRITER
#endif

/*
 Define as 1 to disable JSON Pointer, JSON Patch and JSON Merge Patch supports.
 
 This will disable these functions at compile-time:
    - styyjson_ptr_xxx()
    - styyjson_mut_ptr_xxx()
    - styyjson_doc_ptr_xxx()
    - styyjson_mut_doc_ptr_xxx()
    - styyjson_patch()
    - styyjson_mut_patch()
    - styyjson_merge_patch()
    - styyjson_mut_merge_patch()
 */
#ifndef YYJSON_DISABLE_UTILS
#endif

/*
 Define as 1 to disable the fast floating-point number conversion in yyjson,
 and use libc's `strtod/snprintf` instead.
 
 This will reduce the binary size by about 30%, but significantly slow down the
 floating-point read/write speed.
 */
#ifndef YYJSON_DISABLE_FAST_FP_CONV
#endif

/*
 Define as 1 to disable non-standard JSON support at compile-time:
    - Reading and writing inf/nan literal, such as `NaN`, `-Infinity`.
    - Single line and multiple line comments.
    - Single trailing comma at the end of an object or array.
    - Invalid unicode in string value.
 
 This will also invalidate these run-time options:
    - YYJSON_READ_ALLOW_INF_AND_NAN
    - YYJSON_READ_ALLOW_COMMENTS
    - YYJSON_READ_ALLOW_TRAILING_COMMAS
    - YYJSON_READ_ALLOW_INVALID_UNICODE
    - YYJSON_WRITE_ALLOW_INF_AND_NAN
    - YYJSON_WRITE_ALLOW_INVALID_UNICODE
 
 This will reduce the binary size by about 10%, and speed up the reading and
 writing speed by about 2% to 6%.
 */
#ifndef YYJSON_DISABLE_NON_STANDARD
#endif

/*
 Define as 1 to disable UTF-8 validation at compile time.
 
 If all input strings are guaranteed to be valid UTF-8 encoding (for example,
 some language's String object has already validated the encoding), using this
 flag can avoid redundant UTF-8 validation in yyjson.
 
 This flag can speed up the reading and writing speed of non-ASCII encoded
 strings by about 3% to 7%.
 
 Note: If this flag is used while passing in illegal UTF-8 strings, the
 following errors may occur:
 - Escaped characters may be ignored when parsing JSON strings.
 - Ending quotes may be ignored when parsing JSON strings, causing the string
   to be concatenated to the next value.
 - When accessing `styyjson_mut_val` for serialization, the string ending may be
   accessed out of bounds, causing a segmentation fault.
 */
#ifndef YYJSON_DISABLE_UTF8_VALIDATION
#endif

/*
 Define as 1 to indicate that the target architecture does not support unaligned
 memory access. Please refer to the comments in the C file for details.
 */
#ifndef YYJSON_DISABLE_UNALIGNED_MEMORY_ACCESS
#endif

/* Define as 1 to export symbols when building this library as Windows DLL. */
#ifndef YYJSON_EXPORTS
#endif

/* Define as 1 to import symbols when using this library as Windows DLL. */
#ifndef YYJSON_IMPORTS
#endif

/* Define as 1 to include <stdint.h> for compiler which doesn't support C99. */
#ifndef YYJSON_HAS_STDINT_H
#endif

/* Define as 1 to include <stdbool.h> for compiler which doesn't support C99. */
#ifndef YYJSON_HAS_STDBOOL_H
#endif



/*==============================================================================
 * Compiler Macros
 *============================================================================*/

/** compiler version (MSVC) */
#ifdef _MSC_VER
#   define YYJSON_MSC_VER _MSC_VER
#else
#   define YYJSON_MSC_VER 0
#endif

/** compiler version (GCC) */
#ifdef __GNUC__
#   define YYJSON_GCC_VER __GNUC__
#   if defined(__GNUC_PATCHLEVEL__)
#       define styyjson_gcc_available(major, minor, patch) \
            ((__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__) \
            >= (major * 10000 + minor * 100 + patch))
#   else
#       define styyjson_gcc_available(major, minor, patch) \
            ((__GNUC__ * 10000 + __GNUC_MINOR__ * 100) \
            >= (major * 10000 + minor * 100 + patch))
#   endif
#else
#   define YYJSON_GCC_VER 0
#   define styyjson_gcc_available(major, minor, patch) 0
#endif

/** real gcc check */
#if !defined(__clang__) && !defined(__INTEL_COMPILER) && !defined(__ICC) && \
    defined(__GNUC__)
#   define YYJSON_IS_REAL_GCC 1
#else
#   define YYJSON_IS_REAL_GCC 0
#endif

/** C version (STDC) */
#if defined(__STDC__) && (__STDC__ >= 1) && defined(__STDC_VERSION__)
#   define YYJSON_STDC_VER __STDC_VERSION__
#else
#   define YYJSON_STDC_VER 0
#endif

/** C++ version */
#if defined(__cplusplus)
#   define YYJSON_CPP_VER __cplusplus
#else
#   define YYJSON_CPP_VER 0
#endif

/** compiler builtin check (since gcc 10.0, clang 2.6, icc 2021) */
#ifndef styyjson_has_builtin
#   ifdef __has_builtin
#       define styyjson_has_builtin(x) __has_builtin(x)
#   else
#       define styyjson_has_builtin(x) 0
#   endif
#endif

/** compiler attribute check (since gcc 5.0, clang 2.9, icc 17) */
#ifndef styyjson_has_attribute
#   ifdef __has_attribute
#       define styyjson_has_attribute(x) __has_attribute(x)
#   else
#       define styyjson_has_attribute(x) 0
#   endif
#endif

/** compiler feature check (since clang 2.6, icc 17) */
#ifndef styyjson_has_feature
#   ifdef __has_feature
#       define styyjson_has_feature(x) __has_feature(x)
#   else
#       define styyjson_has_feature(x) 0
#   endif
#endif

/** include check (since gcc 5.0, clang 2.7, icc 16, msvc 2017 15.3) */
#ifndef styyjson_has_include
#   ifdef __has_include
#       define styyjson_has_include(x) __has_include(x)
#   else
#       define styyjson_has_include(x) 0
#   endif
#endif

/** inline for compiler */
#ifndef styyjson_inline
#   if YYJSON_MSC_VER >= 1200
#       define styyjson_inline __forceinline
#   elif defined(_MSC_VER)
#       define styyjson_inline __inline
#   elif styyjson_has_attribute(always_inline) || YYJSON_GCC_VER >= 4
#       define styyjson_inline __inline__ __attribute__((always_inline))
#   elif defined(__clang__) || defined(__GNUC__)
#       define styyjson_inline __inline__
#   elif defined(__cplusplus) || YYJSON_STDC_VER >= 199901L
#       define styyjson_inline inline
#   else
#       define styyjson_inline
#   endif
#endif

/** noinline for compiler */
#ifndef styyjson_noinline
#   if YYJSON_MSC_VER >= 1400
#       define styyjson_noinline __declspec(noinline)
#   elif styyjson_has_attribute(noinline) || YYJSON_GCC_VER >= 4
#       define styyjson_noinline __attribute__((noinline))
#   else
#       define styyjson_noinline
#   endif
#endif

/** align for compiler */
#ifndef styyjson_align
#   if YYJSON_MSC_VER >= 1300
#       define styyjson_align(x) __declspec(align(x))
#   elif styyjson_has_attribute(aligned) || defined(__GNUC__)
#       define styyjson_align(x) __attribute__((aligned(x)))
#   elif YYJSON_CPP_VER >= 201103L
#       define styyjson_align(x) alignas(x)
#   else
#       define styyjson_align(x)
#   endif
#endif

/** likely for compiler */
#ifndef styyjson_likely
#   if styyjson_has_builtin(__builtin_expect) || \
    (YYJSON_GCC_VER >= 4 && YYJSON_GCC_VER != 5)
#       define styyjson_likely(expr) __builtin_expect(!!(expr), 1)
#   else
#       define styyjson_likely(expr) (expr)
#   endif
#endif

/** unlikely for compiler */
#ifndef styyjson_unlikely
#   if styyjson_has_builtin(__builtin_expect) || \
    (YYJSON_GCC_VER >= 4 && YYJSON_GCC_VER != 5)
#       define styyjson_unlikely(expr) __builtin_expect(!!(expr), 0)
#   else
#       define styyjson_unlikely(expr) (expr)
#   endif
#endif

/** compile-time constant check for compiler */
#ifndef styyjson_constant_p
#   if styyjson_has_builtin(__builtin_constant_p) || (YYJSON_GCC_VER >= 3)
#       define YYJSON_HAS_CONSTANT_P 1
#       define styyjson_constant_p(value) __builtin_constant_p(value)
#   else
#       define YYJSON_HAS_CONSTANT_P 0
#       define styyjson_constant_p(value) 0
#   endif
#endif

/** deprecate warning */
#ifndef styyjson_deprecated
#   if YYJSON_MSC_VER >= 1400
#       define styyjson_deprecated(msg) __declspec(deprecated(msg))
#   elif styyjson_has_feature(attribute_deprecated_with_message) || \
        (YYJSON_GCC_VER > 4 || (YYJSON_GCC_VER == 4 && __GNUC_MINOR__ >= 5))
#       define styyjson_deprecated(msg) __attribute__((deprecated(msg)))
#   elif YYJSON_GCC_VER >= 3
#       define styyjson_deprecated(msg) __attribute__((deprecated))
#   else
#       define styyjson_deprecated(msg)
#   endif
#endif

/** function export */
#ifndef styyjson_api
#   if defined(_WIN32)
#       if defined(YYJSON_EXPORTS) && YYJSON_EXPORTS
#           define styyjson_api __declspec(dllexport)
#       elif defined(YYJSON_IMPORTS) && YYJSON_IMPORTS
#           define styyjson_api __declspec(dllimport)
#       else
#           define styyjson_api
#       endif
#   elif styyjson_has_attribute(visibility) || YYJSON_GCC_VER >= 4
#       define styyjson_api __attribute__((visibility("default")))
#   else
#       define styyjson_api
#   endif
#endif

/** inline function export */
#ifndef styyjson_api_inline
#   define styyjson_api_inline static styyjson_inline
#endif

/** stdint (C89 compatible) */
#if (defined(YYJSON_HAS_STDINT_H) && YYJSON_HAS_STDINT_H) || \
    YYJSON_MSC_VER >= 1600 || YYJSON_STDC_VER >= 199901L || \
    defined(_STDINT_H) || defined(_STDINT_H_) || \
    defined(__CLANG_STDINT_H) || defined(_STDINT_H_INCLUDED) || \
    styyjson_has_include(<stdint.h>)
#   include <stdint.h>
#elif defined(_MSC_VER)
#   if _MSC_VER < 1300
        typedef signed char         int8_t;
        typedef signed short        int16_t;
        typedef signed int          int32_t;
        typedef unsigned char       uint8_t;
        typedef unsigned short      uint16_t;
        typedef unsigned int        uint32_t;
        typedef signed __int64      int64_t;
        typedef unsigned __int64    uint64_t;
#   else
        typedef signed __int8       int8_t;
        typedef signed __int16      int16_t;
        typedef signed __int32      int32_t;
        typedef unsigned __int8     uint8_t;
        typedef unsigned __int16    uint16_t;
        typedef unsigned __int32    uint32_t;
        typedef signed __int64      int64_t;
        typedef unsigned __int64    uint64_t;
#   endif
#else
#   if UCHAR_MAX == 0xFFU
        typedef signed char     int8_t;
        typedef unsigned char   uint8_t;
#   else
#       error cannot find 8-bit integer type
#   endif
#   if USHRT_MAX == 0xFFFFU
        typedef unsigned short  uint16_t;
        typedef signed short    int16_t;
#   elif UINT_MAX == 0xFFFFU
        typedef unsigned int    uint16_t;
        typedef signed int      int16_t;
#   else
#       error cannot find 16-bit integer type
#   endif
#   if UINT_MAX == 0xFFFFFFFFUL
        typedef unsigned int    uint32_t;
        typedef signed int      int32_t;
#   elif ULONG_MAX == 0xFFFFFFFFUL
        typedef unsigned long   uint32_t;
        typedef signed long     int32_t;
#   elif USHRT_MAX == 0xFFFFFFFFUL
        typedef unsigned short  uint32_t;
        typedef signed short    int32_t;
#   else
#       error cannot find 32-bit integer type
#   endif
#   if defined(__INT64_TYPE__) && defined(__UINT64_TYPE__)
        typedef __INT64_TYPE__  int64_t;
        typedef __UINT64_TYPE__ uint64_t;
#   elif defined(__GNUC__) || defined(__clang__)
#       if !defined(_SYS_TYPES_H) && !defined(__int8_t_defined)
        __extension__ typedef long long             int64_t;
#       endif
        __extension__ typedef unsigned long long    uint64_t;
#   elif defined(_LONG_LONG) || defined(__MWERKS__) || defined(_CRAYC) || \
        defined(__SUNPRO_C) || defined(__SUNPRO_CC)
        typedef long long           int64_t;
        typedef unsigned long long  uint64_t;
#   elif (defined(__BORLANDC__) && __BORLANDC__ > 0x460) || \
        defined(__WATCOM_INT64__) || defined (__alpha) || defined (__DECC)
        typedef __int64             int64_t;
        typedef unsigned __int64    uint64_t;
#   else
#       error cannot find 64-bit integer type
#   endif
#endif

/** stdbool (C89 compatible) */
#if (defined(YYJSON_HAS_STDBOOL_H) && YYJSON_HAS_STDBOOL_H) || \
    (styyjson_has_include(<stdbool.h>) && !defined(__STRICT_ANSI__)) || \
    YYJSON_MSC_VER >= 1800 || YYJSON_STDC_VER >= 199901L
#   include <stdbool.h>
#elif !defined(__bool_true_false_are_defined)
#   define __bool_true_false_are_defined 1
#   if defined(__cplusplus)
#       if defined(__GNUC__) && !defined(__STRICT_ANSI__)
#           define _Bool bool
#           if __cplusplus < 201103L
#               define bool bool
#               define false false
#               define true true
#           endif
#       endif
#   else
#       define bool unsigned char
#       define true 1
#       define false 0
#   endif
#endif

/** char bit check */
#if defined(CHAR_BIT)
#   if CHAR_BIT != 8
#       error non 8-bit char is not supported
#   endif
#endif

/**
 Microsoft Visual C++ 6.0 doesn't support converting number from u64 to f64:
 error C2520: conversion from unsigned __int64 to double not implemented.
 */
#ifndef YYJSON_U64_TO_F64_NO_IMPL
#   if (0 < YYJSON_MSC_VER) && (YYJSON_MSC_VER <= 1200)
#       define YYJSON_U64_TO_F64_NO_IMPL 1
#   else
#       define YYJSON_U64_TO_F64_NO_IMPL 0
#   endif
#endif



/*==============================================================================
 * Compile Hint Begin
 *============================================================================*/

/* extern "C" begin */
#ifdef __cplusplus
extern "C" {
#endif

/* warning suppress begin */
#if defined(__clang__)
#   pragma clang diagnostic push
#   pragma clang diagnostic ignored "-Wunused-function"
#   pragma clang diagnostic ignored "-Wunused-parameter"
#elif defined(__GNUC__)
#   if (__GNUC__ > 4) || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6)
#   pragma GCC diagnostic push
#   endif
#   pragma GCC diagnostic ignored "-Wunused-function"
#   pragma GCC diagnostic ignored "-Wunused-parameter"
#elif defined(_MSC_VER)
#   pragma warning(push)
#   pragma warning(disable:4800) /* 'int': forcing value to 'true' or 'false' */
#endif



/*==============================================================================
 * Version
 *============================================================================*/

/** The major version of yyjson. */
#define YYJSON_VERSION_MAJOR  0

/** The minor version of yyjson. */
#define YYJSON_VERSION_MINOR  10

/** The patch version of yyjson. */
#define YYJSON_VERSION_PATCH  0

/** The version of yyjson in hex: `(major << 16) | (minor << 8) | (patch)`. */
#define YYJSON_VERSION_HEX    0x000A00

/** The version string of yyjson. */
#define YYJSON_VERSION_STRING "0.10.0"

/** The version of yyjson in hex, same as `YYJSON_VERSION_HEX`. */
styyjson_api uint32_t styyjson_version(void);



/*==============================================================================
 * JSON Types
 *============================================================================*/

/** Type of a JSON value (3 bit). */
typedef uint8_t styyjson_type;
/** No type, invalid. */
#define YYJSON_TYPE_NONE        ((uint8_t)0)        /* _____000 */
/** Raw string type, no subtype. */
#define YYJSON_TYPE_RAW         ((uint8_t)1)        /* _____001 */
/** Null type: `null` literal, no subtype. */
#define YYJSON_TYPE_NULL        ((uint8_t)2)        /* _____010 */
/** Boolean type, subtype: TRUE, FALSE. */
#define YYJSON_TYPE_BOOL        ((uint8_t)3)        /* _____011 */
/** Number type, subtype: UINT, SINT, REAL. */
#define YYJSON_TYPE_NUM         ((uint8_t)4)        /* _____100 */
/** String type, subtype: NONE, NOESC. */
#define YYJSON_TYPE_STR         ((uint8_t)5)        /* _____101 */
/** Array type, no subtype. */
#define YYJSON_TYPE_ARR         ((uint8_t)6)        /* _____110 */
/** Object type, no subtype. */
#define YYJSON_TYPE_OBJ         ((uint8_t)7)        /* _____111 */

/** Subtype of a JSON value (2 bit). */
typedef uint8_t styyjson_subtype;
/** No subtype. */
#define YYJSON_SUBTYPE_NONE     ((uint8_t)(0 << 3)) /* ___00___ */
/** False subtype: `false` literal. */
#define YYJSON_SUBTYPE_FALSE    ((uint8_t)(0 << 3)) /* ___00___ */
/** True subtype: `true` literal. */
#define YYJSON_SUBTYPE_TRUE     ((uint8_t)(1 << 3)) /* ___01___ */
/** Unsigned integer subtype: `uint64_t`. */
#define YYJSON_SUBTYPE_UINT     ((uint8_t)(0 << 3)) /* ___00___ */
/** Signed integer subtype: `int64_t`. */
#define YYJSON_SUBTYPE_SINT     ((uint8_t)(1 << 3)) /* ___01___ */
/** Real number subtype: `double`. */
#define YYJSON_SUBTYPE_REAL     ((uint8_t)(2 << 3)) /* ___10___ */
/** String that do not need to be escaped for writing (internal use). */
#define YYJSON_SUBTYPE_NOESC    ((uint8_t)(1 << 3)) /* ___01___ */

/** The mask used to extract the type of a JSON value. */
#define YYJSON_TYPE_MASK        ((uint8_t)0x07)     /* _____111 */
/** The number of bits used by the type. */
#define YYJSON_TYPE_BIT         ((uint8_t)3)
/** The mask used to extract the subtype of a JSON value. */
#define YYJSON_SUBTYPE_MASK     ((uint8_t)0x18)     /* ___11___ */
/** The number of bits used by the subtype. */
#define YYJSON_SUBTYPE_BIT      ((uint8_t)2)
/** The mask used to extract the reserved bits of a JSON value. */
#define YYJSON_RESERVED_MASK    ((uint8_t)0xE0)     /* 111_____ */
/** The number of reserved bits. */
#define YYJSON_RESERVED_BIT     ((uint8_t)3)
/** The mask used to extract the tag of a JSON value. */
#define YYJSON_TAG_MASK         ((uint8_t)0xFF)     /* 11111111 */
/** The number of bits used by the tag. */
#define YYJSON_TAG_BIT          ((uint8_t)8)

/** Padding size for JSON reader. */
#define YYJSON_PADDING_SIZE     4



/*==============================================================================
 * Allocator
 *============================================================================*/

/**
 A memory allocator.
 
 Typically you don't need to use it, unless you want to customize your own
 memory allocator.
 */
typedef struct styyjson_alc {
    /** Same as libc's malloc(size), should not be NULL. */
    void *(*malloc)(void *ctx, size_t size);
    /** Same as libc's realloc(ptr, size), should not be NULL. */
    void *(*realloc)(void *ctx, void *ptr, size_t old_size, size_t size);
    /** Same as libc's free(ptr), should not be NULL. */
    void (*free)(void *ctx, void *ptr);
    /** A context for malloc/realloc/free, can be NULL. */
    void *ctx;
} styyjson_alc;

/**
 A pool allocator uses fixed length pre-allocated memory.
 
 This allocator may be used to avoid malloc/realloc calls. The pre-allocated 
 memory should be held by the caller. The maximum amount of memory required to
 read a JSON can be calculated using the `styyjson_read_max_memory_usage()`
 function, but the amount of memory required to write a JSON cannot be directly 
 calculated.
 
 This is not a general-purpose allocator. It is designed to handle a single JSON
 data at a time. If it is used for overly complex memory tasks, such as parsing
 multiple JSON documents using the same allocator but releasing only a few of
 them, it may cause memory fragmentation, resulting in performance degradation
 and memory waste.
 
 @param alc The allocator to be initialized.
    If this parameter is NULL, the function will fail and return false.
    If `buf` or `size` is invalid, this will be set to an empty allocator.
 @param buf The buffer memory for this allocator.
    If this parameter is NULL, the function will fail and return false.
 @param size The size of `buf`, in bytes.
    If this parameter is less than 8 words (32/64 bytes on 32/64-bit OS), the
    function will fail and return false.
 @return true if the `alc` has been successfully initialized.
 
 @par Example
 @code
    // parse JSON with stack memory
    char buf[1024];
    styyjson_alc alc;
    styyjson_alc_pool_init(&alc, buf, 1024);
    
    const char *json = "{\"name\":\"Helvetica\",\"size\":16}"
    styyjson_doc *doc = styyjson_read_opts(json, strlen(json), 0, &alc, NULL);
    // the memory of `doc` is on the stack
 @endcode
 
 @warning This Allocator is not thread-safe.
 */
styyjson_api bool styyjson_alc_pool_init(styyjson_alc *alc, void *buf, size_t size);

/**
 A dynamic allocator.
 
 This allocator has a similar usage to the pool allocator above. However, when
 there is not enough memory, this allocator will dynamically request more memory
 using libc's `malloc` function, and frees it all at once when it is destroyed.
 
 @return A new dynamic allocator, or NULL if memory allocation failed.
 @note The returned value should be freed with `styyjson_alc_dyn_free()`.
 
 @warning This Allocator is not thread-safe.
 */
styyjson_api styyjson_alc *styyjson_alc_dyn_new(void);

/**
 Free a dynamic allocator which is created by `styyjson_alc_dyn_new()`.
 @param alc The dynamic allocator to be destroyed.
 */
styyjson_api void styyjson_alc_dyn_free(styyjson_alc *alc);



/*==============================================================================
 * JSON Structure
 *============================================================================*/

/**
 An immutable document for reading JSON.
 This document holds memory for all its JSON values and strings. When it is no
 longer used, the user should call `styyjson_doc_free()` to free its memory.
 */
typedef struct styyjson_doc styyjson_doc;

/**
 An immutable value for reading JSON.
 A JSON Value has the same lifetime as its document. The memory is held by its
 document and and cannot be freed alone.
 */
typedef struct styyjson_val styyjson_val;

/**
 A mutable document for building JSON.
 This document holds memory for all its JSON values and strings. When it is no
 longer used, the user should call `styyjson_mut_doc_free()` to free its memory.
 */
typedef struct styyjson_mut_doc styyjson_mut_doc;

/**
 A mutable value for building JSON.
 A JSON Value has the same lifetime as its document. The memory is held by its
 document and and cannot be freed alone.
 */
typedef struct styyjson_mut_val styyjson_mut_val;



/*==============================================================================
 * JSON Reader API
 *============================================================================*/

/** Run-time options for JSON reader. */
typedef uint32_t styyjson_read_flag;

/** Default option (RFC 8259 compliant):
    - Read positive integer as uint64_t.
    - Read negative integer as int64_t.
    - Read floating-point number as double with round-to-nearest mode.
    - Read integer which cannot fit in uint64_t or int64_t as double.
    - Report error if double number is infinity.
    - Report error if string contains invalid UTF-8 character or BOM.
    - Report error on trailing commas, comments, inf and nan literals. */
static const styyjson_read_flag YYJSON_READ_NOFLAG                = 0;

/** Read the input data in-situ.
    This option allows the reader to modify and use input data to store string
    values, which can increase reading speed slightly.
    The caller should hold the input data before free the document.
    The input data must be padded by at least `YYJSON_PADDING_SIZE` bytes.
    For example: `[1,2]` should be `[1,2]\0\0\0\0`, input length should be 5. */
static const styyjson_read_flag YYJSON_READ_INSITU                = 1 << 0;

/** Stop when done instead of issuing an error if there's additional content
    after a JSON document. This option may be used to parse small pieces of JSON
    in larger data, such as `NDJSON`. */
static const styyjson_read_flag YYJSON_READ_STOP_WHEN_DONE        = 1 << 1;

/** Allow single trailing comma at the end of an object or array,
    such as `[1,2,3,]`, `{"a":1,"b":2,}` (non-standard). */
static const styyjson_read_flag YYJSON_READ_ALLOW_TRAILING_COMMAS = 1 << 2;

/** Allow C-style single line and multiple line comments (non-standard). */
static const styyjson_read_flag YYJSON_READ_ALLOW_COMMENTS        = 1 << 3;

/** Allow inf/nan number and literal, case-insensitive,
    such as 1e999, NaN, inf, -Infinity (non-standard). */
static const styyjson_read_flag YYJSON_READ_ALLOW_INF_AND_NAN     = 1 << 4;

/** Read all numbers as raw strings (value with `YYJSON_TYPE_RAW` type),
    inf/nan literal is also read as raw with `ALLOW_INF_AND_NAN` flag. */
static const styyjson_read_flag YYJSON_READ_NUMBER_AS_RAW         = 1 << 5;

/** Allow reading invalid unicode when parsing string values (non-standard).
    Invalid characters will be allowed to appear in the string values, but
    invalid escape sequences will still be reported as errors.
    This flag does not affect the performance of correctly encoded strings.
    
    @warning Strings in JSON values may contain incorrect encoding when this
    option is used, you need to handle these strings carefully to avoid security
    risks. */
static const styyjson_read_flag YYJSON_READ_ALLOW_INVALID_UNICODE = 1 << 6;

/** Read big numbers as raw strings. These big numbers include integers that
    cannot be represented by `int64_t` and `uint64_t`, and floating-point
    numbers that cannot be represented by finite `double`.
    The flag will be overridden by `YYJSON_READ_NUMBER_AS_RAW` flag. */
static const styyjson_read_flag YYJSON_READ_BIGNUM_AS_RAW         = 1 << 7;



/** Result code for JSON reader. */
typedef uint32_t styyjson_read_code;

/** Success, no error. */
static const styyjson_read_code YYJSON_READ_SUCCESS                       = 0;

/** Invalid parameter, such as NULL input string or 0 input length. */
static const styyjson_read_code YYJSON_READ_ERROR_INVALID_PARAMETER       = 1;

/** Memory allocation failure occurs. */
static const styyjson_read_code YYJSON_READ_ERROR_MEMORY_ALLOCATION       = 2;

/** Input JSON string is empty. */
static const styyjson_read_code YYJSON_READ_ERROR_EMPTY_CONTENT           = 3;

/** Unexpected content after document, such as `[123]abc`. */
static const styyjson_read_code YYJSON_READ_ERROR_UNEXPECTED_CONTENT      = 4;

/** Unexpected ending, such as `[123`. */
static const styyjson_read_code YYJSON_READ_ERROR_UNEXPECTED_END          = 5;

/** Unexpected character inside the document, such as `[abc]`. */
static const styyjson_read_code YYJSON_READ_ERROR_UNEXPECTED_CHARACTER    = 6;

/** Invalid JSON structure, such as `[1,]`. */
static const styyjson_read_code YYJSON_READ_ERROR_JSON_STRUCTURE          = 7;

/** Invalid comment, such as unclosed multi-line comment. */
static const styyjson_read_code YYJSON_READ_ERROR_INVALID_COMMENT         = 8;

/** Invalid number, such as `123.e12`, `000`. */
static const styyjson_read_code YYJSON_READ_ERROR_INVALID_NUMBER          = 9;

/** Invalid string, such as invalid escaped character inside a string. */
static const styyjson_read_code YYJSON_READ_ERROR_INVALID_STRING          = 10;

/** Invalid JSON literal, such as `truu`. */
static const styyjson_read_code YYJSON_READ_ERROR_LITERAL                 = 11;

/** Failed to open a file. */
static const styyjson_read_code YYJSON_READ_ERROR_FILE_OPEN               = 12;

/** Failed to read a file. */
static const styyjson_read_code YYJSON_READ_ERROR_FILE_READ               = 13;

/** Error information for JSON reader. */
typedef struct styyjson_read_err {
    /** Error code, see `styyjson_read_code` for all possible values. */
    styyjson_read_code code;
    /** Error message, constant, no need to free (NULL if success). */
    const char *msg;
    /** Error byte position for input data (0 if success). */
    size_t pos;
} styyjson_read_err;

/**
 Locate the line and column number for a byte position in a string.
 This can be used to get better description for error position.
 
 @param str The input string.
 @param len The byte length of the input string.
 @param pos The byte position within the input string.
 @param line A pointer to receive the line number, starting from 1.
 @param col  A pointer to receive the column number, starting from 1.
 @param chr  A pointer to receive the character index, starting from 0.
 @return true on success, false if `str` is NULL or `pos` is out of bounds.
 @note Line/column/character are calculated based on Unicode characters for
    compatibility with text editors. For multi-byte UTF-8 characters,
    the returned value may not directly correspond to the byte position.
 */
styyjson_api bool styyjson_locate_pos(const char *str, size_t len, size_t pos,
                                  size_t *line, size_t *col, size_t *chr);



/**
 Read JSON with options.
 
 This function is thread-safe when:
 1. The `dat` is not modified by other threads.
 2. The `alc` is thread-safe or NULL.
 
 @param dat The JSON data (UTF-8 without BOM), null-terminator is not required.
    If this parameter is NULL, the function will fail and return NULL.
    The `dat` will not be modified without the flag `YYJSON_READ_INSITU`, so you
    can pass a `const char *` string and case it to `char *` if you don't use
    the `YYJSON_READ_INSITU` flag.
 @param len The length of JSON data in bytes.
    If this parameter is 0, the function will fail and return NULL.
 @param flg The JSON read options.
    Multiple options can be combined with `|` operator. 0 means no options.
 @param alc The memory allocator used by JSON reader.
    Pass NULL to use the libc's default allocator.
 @param err A pointer to receive error information.
    Pass NULL if you don't need error information.
 @return A new JSON document, or NULL if an error occurs.
    When it's no longer needed, it should be freed with `styyjson_doc_free()`.
 */
styyjson_api styyjson_doc *styyjson_read_opts(char *dat,
                                        size_t len,
                                        styyjson_read_flag flg,
                                        const styyjson_alc *alc,
                                        styyjson_read_err *err);

/**
 Read a JSON file.
 
 This function is thread-safe when:
 1. The file is not modified by other threads.
 2. The `alc` is thread-safe or NULL.
 
 @param path The JSON file's path.
    If this path is NULL or invalid, the function will fail and return NULL.
 @param flg The JSON read options.
    Multiple options can be combined with `|` operator. 0 means no options.
 @param alc The memory allocator used by JSON reader.
    Pass NULL to use the libc's default allocator.
 @param err A pointer to receive error information.
    Pass NULL if you don't need error information.
 @return A new JSON document, or NULL if an error occurs.
    When it's no longer needed, it should be freed with `styyjson_doc_free()`.
 
 @warning On 32-bit operating system, files larger than 2GB may fail to read.
 */
styyjson_api styyjson_doc *styyjson_read_file(const char *path,
                                        styyjson_read_flag flg,
                                        const styyjson_alc *alc,
                                        styyjson_read_err *err);

/**
 Read JSON from a file pointer.
 
 @param fp The file pointer.
    The data will be read from the current position of the FILE to the end.
    If this fp is NULL or invalid, the function will fail and return NULL.
 @param flg The JSON read options.
    Multiple options can be combined with `|` operator. 0 means no options.
 @param alc The memory allocator used by JSON reader.
    Pass NULL to use the libc's default allocator.
 @param err A pointer to receive error information.
    Pass NULL if you don't need error information.
 @return A new JSON document, or NULL if an error occurs.
    When it's no longer needed, it should be freed with `styyjson_doc_free()`.
 
 @warning On 32-bit operating system, files larger than 2GB may fail to read.
 */
styyjson_api styyjson_doc *styyjson_read_fp(FILE *fp,
                                      styyjson_read_flag flg,
                                      const styyjson_alc *alc,
                                      styyjson_read_err *err);

/**
 Read a JSON string.
 
 This function is thread-safe.
 
 @param dat The JSON data (UTF-8 without BOM), null-terminator is not required.
    If this parameter is NULL, the function will fail and return NULL.
 @param len The length of JSON data in bytes.
    If this parameter is 0, the function will fail and return NULL.
 @param flg The JSON read options.
    Multiple options can be combined with `|` operator. 0 means no options.
 @return A new JSON document, or NULL if an error occurs.
    When it's no longer needed, it should be freed with `styyjson_doc_free()`.
 */
styyjson_api_inline styyjson_doc *styyjson_read(const char *dat,
                                          size_t len,
                                          styyjson_read_flag flg) {
    flg &= ~YYJSON_READ_INSITU; /* const string cannot be modified */
    return styyjson_read_opts((char *)(void *)(size_t)(const void *)dat,
                            len, flg, NULL, NULL);
}

/**
 Returns the size of maximum memory usage to read a JSON data.
 
 You may use this value to avoid malloc() or calloc() call inside the reader
 to get better performance, or read multiple JSON with one piece of memory.
 
 @param len The length of JSON data in bytes.
 @param flg The JSON read options.
 @return The maximum memory size to read this JSON, or 0 if overflow.
 
 @par Example
 @code
    // read multiple JSON with same pre-allocated memory
    
    char *dat1, *dat2, *dat3; // JSON data
    size_t len1, len2, len3; // JSON length
    size_t max_len = MAX(len1, MAX(len2, len3));
    styyjson_doc *doc;
    
    // use one allocator for multiple JSON
    size_t size = styyjson_read_max_memory_usage(max_len, 0);
    void *buf = malloc(size);
    styyjson_alc alc;
    styyjson_alc_pool_init(&alc, buf, size);
    
    // no more alloc() or realloc() call during reading
    doc = styyjson_read_opts(dat1, len1, 0, &alc, NULL);
    styyjson_doc_free(doc);
    doc = styyjson_read_opts(dat2, len2, 0, &alc, NULL);
    styyjson_doc_free(doc);
    doc = styyjson_read_opts(dat3, len3, 0, &alc, NULL);
    styyjson_doc_free(doc);
    
    free(buf);
 @endcode
 @see styyjson_alc_pool_init()
 */
styyjson_api_inline size_t styyjson_read_max_memory_usage(size_t len,
                                                      styyjson_read_flag flg) {
    /*
     1. The max value count is (json_size / 2 + 1),
        for example: "[1,2,3,4]" size is 9, value count is 5.
     2. Some broken JSON may cost more memory during reading, but fail at end,
        for example: "[[[[[[[[".
     3. yyjson use 16 bytes per value, see struct styyjson_val.
     4. yyjson use dynamic memory with a growth factor of 1.5.
     
     The max memory size is (json_size / 2 * 16 * 1.5 + padding).
     */
    size_t mul = (size_t)12 + !(flg & YYJSON_READ_INSITU);
    size_t pad = 256;
    size_t max = (size_t)(~(size_t)0);
    if (flg & YYJSON_READ_STOP_WHEN_DONE) len = len < 256 ? 256 : len;
    if (len >= (max - pad - mul) / mul) return 0;
    return len * mul + pad;
}

/**
 Read a JSON number.

 This function is thread-safe when data is not modified by other threads.

 @param dat The JSON data (UTF-8 without BOM), null-terminator is required.
    If this parameter is NULL, the function will fail and return NULL.
 @param val The output value where result is stored.
    If this parameter is NULL, the function will fail and return NULL.
    The value will hold either UINT or SINT or REAL number;
 @param flg The JSON read options.
    Multiple options can be combined with `|` operator. 0 means no options.
    Supports `YYJSON_READ_NUMBER_AS_RAW` and `YYJSON_READ_ALLOW_INF_AND_NAN`.
 @param alc The memory allocator used for long number.
    It is only used when the built-in floating point reader is disabled.
    Pass NULL to use the libc's default allocator.
 @param err A pointer to receive error information.
    Pass NULL if you don't need error information.
 @return If successful, a pointer to the character after the last character
    used in the conversion, NULL if an error occurs.
 */
styyjson_api const char *styyjson_read_number(const char *dat,
                                          styyjson_val *val,
                                          styyjson_read_flag flg,
                                          const styyjson_alc *alc,
                                          styyjson_read_err *err);

/**
 Read a JSON number.

 This function is thread-safe when data is not modified by other threads.

 @param dat The JSON data (UTF-8 without BOM), null-terminator is required.
    If this parameter is NULL, the function will fail and return NULL.
 @param val The output value where result is stored.
    If this parameter is NULL, the function will fail and return NULL.
    The value will hold either UINT or SINT or REAL number;
 @param flg The JSON read options.
    Multiple options can be combined with `|` operator. 0 means no options.
    Supports `YYJSON_READ_NUMBER_AS_RAW` and `YYJSON_READ_ALLOW_INF_AND_NAN`.
 @param alc The memory allocator used for long number.
    It is only used when the built-in floating point reader is disabled.
    Pass NULL to use the libc's default allocator.
 @param err A pointer to receive error information.
    Pass NULL if you don't need error information.
 @return If successful, a pointer to the character after the last character
    used in the conversion, NULL if an error occurs.
 */
styyjson_api_inline const char *styyjson_mut_read_number(const char *dat,
                                                     styyjson_mut_val *val,
                                                     styyjson_read_flag flg,
                                                     const styyjson_alc *alc,
                                                     styyjson_read_err *err) {
    return styyjson_read_number(dat, (styyjson_val *)val, flg, alc, err);
}


/*==============================================================================
 * JSON Writer API
 *============================================================================*/

/** Run-time options for JSON writer. */
typedef uint32_t styyjson_write_flag;

/** Default option:
    - Write JSON minify.
    - Report error on inf or nan number.
    - Report error on invalid UTF-8 string.
    - Do not escape unicode or slash. */
static const styyjson_write_flag YYJSON_WRITE_NOFLAG                  = 0;

/** Write JSON pretty with 4 space indent. */
static const styyjson_write_flag YYJSON_WRITE_PRETTY                  = 1 << 0;

/** Escape unicode as `uXXXX`, make the output ASCII only. */
static const styyjson_write_flag YYJSON_WRITE_ESCAPE_UNICODE          = 1 << 1;

/** Escape '/' as '\/'. */
static const styyjson_write_flag YYJSON_WRITE_ESCAPE_SLASHES          = 1 << 2;

/** Write inf and nan number as 'Infinity' and 'NaN' literal (non-standard). */
static const styyjson_write_flag YYJSON_WRITE_ALLOW_INF_AND_NAN       = 1 << 3;

/** Write inf and nan number as null literal.
    This flag will override `YYJSON_WRITE_ALLOW_INF_AND_NAN` flag. */
static const styyjson_write_flag YYJSON_WRITE_INF_AND_NAN_AS_NULL     = 1 << 4;

/** Allow invalid unicode when encoding string values (non-standard).
    Invalid characters in string value will be copied byte by byte.
    If `YYJSON_WRITE_ESCAPE_UNICODE` flag is also set, invalid character will be
    escaped as `U+FFFD` (replacement character).
    This flag does not affect the performance of correctly encoded strings. */
static const styyjson_write_flag YYJSON_WRITE_ALLOW_INVALID_UNICODE   = 1 << 5;

/** Write JSON pretty with 2 space indent.
    This flag will override `YYJSON_WRITE_PRETTY` flag. */
static const styyjson_write_flag YYJSON_WRITE_PRETTY_TWO_SPACES       = 1 << 6;

/** Adds a newline character `\n` at the end of the JSON.
    This can be helpful for text editors or NDJSON. */
static const styyjson_write_flag YYJSON_WRITE_NEWLINE_AT_END          = 1 << 7;



/** Result code for JSON writer */
typedef uint32_t styyjson_write_code;

/** Success, no error. */
static const styyjson_write_code YYJSON_WRITE_SUCCESS                     = 0;

/** Invalid parameter, such as NULL document. */
static const styyjson_write_code YYJSON_WRITE_ERROR_INVALID_PARAMETER     = 1;

/** Memory allocation failure occurs. */
static const styyjson_write_code YYJSON_WRITE_ERROR_MEMORY_ALLOCATION     = 2;

/** Invalid value type in JSON document. */
static const styyjson_write_code YYJSON_WRITE_ERROR_INVALID_VALUE_TYPE    = 3;

/** NaN or Infinity number occurs. */
static const styyjson_write_code YYJSON_WRITE_ERROR_NAN_OR_INF            = 4;

/** Failed to open a file. */
static const styyjson_write_code YYJSON_WRITE_ERROR_FILE_OPEN             = 5;

/** Failed to write a file. */
static const styyjson_write_code YYJSON_WRITE_ERROR_FILE_WRITE            = 6;

/** Invalid unicode in string. */
static const styyjson_write_code YYJSON_WRITE_ERROR_INVALID_STRING        = 7;

/** Error information for JSON writer. */
typedef struct styyjson_write_err {
    /** Error code, see `styyjson_write_code` for all possible values. */
    styyjson_write_code code;
    /** Error message, constant, no need to free (NULL if success). */
    const char *msg;
} styyjson_write_err;



/*==============================================================================
 * JSON Document Writer API
 *============================================================================*/

/**
 Write a document to JSON string with options.
 
 This function is thread-safe when:
 The `alc` is thread-safe or NULL.
 
 @param doc The JSON document.
    If this doc is NULL or has no root, the function will fail and return false.
 @param flg The JSON write options.
    Multiple options can be combined with `|` operator. 0 means no options.
 @param alc The memory allocator used by JSON writer.
    Pass NULL to use the libc's default allocator.
 @param len A pointer to receive output length in bytes (not including the
    null-terminator). Pass NULL if you don't need length information.
 @param err A pointer to receive error information.
    Pass NULL if you don't need error information.
 @return A new JSON string, or NULL if an error occurs.
    This string is encoded as UTF-8 with a null-terminator.
    When it's no longer needed, it should be freed with free() or alc->free().
 */
styyjson_api char *styyjson_write_opts(const styyjson_doc *doc,
                                   styyjson_write_flag flg,
                                   const styyjson_alc *alc,
                                   size_t *len,
                                   styyjson_write_err *err);

/**
 Write a document to JSON file with options.
 
 This function is thread-safe when:
 1. The file is not accessed by other threads.
 2. The `alc` is thread-safe or NULL.

 @param path The JSON file's path.
    If this path is NULL or invalid, the function will fail and return false.
    If this file is not empty, the content will be discarded.
 @param doc The JSON document.
    If this doc is NULL or has no root, the function will fail and return false.
 @param flg The JSON write options.
    Multiple options can be combined with `|` operator. 0 means no options.
 @param alc The memory allocator used by JSON writer.
    Pass NULL to use the libc's default allocator.
 @param err A pointer to receive error information.
    Pass NULL if you don't need error information.
 @return true if successful, false if an error occurs.
 
 @warning On 32-bit operating system, files larger than 2GB may fail to write.
 */
styyjson_api bool styyjson_write_file(const char *path,
                                  const styyjson_doc *doc,
                                  styyjson_write_flag flg,
                                  const styyjson_alc *alc,
                                  styyjson_write_err *err);

/**
 Write a document to file pointer with options.
 
 @param fp The file pointer.
    The data will be written to the current position of the file.
    If this fp is NULL or invalid, the function will fail and return false.
 @param doc The JSON document.
    If this doc is NULL or has no root, the function will fail and return false.
 @param flg The JSON write options.
    Multiple options can be combined with `|` operator. 0 means no options.
 @param alc The memory allocator used by JSON writer.
    Pass NULL to use the libc's default allocator.
 @param err A pointer to receive error information.
    Pass NULL if you don't need error information.
 @return true if successful, false if an error occurs.
 
 @warning On 32-bit operating system, files larger than 2GB may fail to write.
 */
styyjson_api bool styyjson_write_fp(FILE *fp,
                                const styyjson_doc *doc,
                                styyjson_write_flag flg,
                                const styyjson_alc *alc,
                                styyjson_write_err *err);

/**
 Write a document to JSON string.
 
 This function is thread-safe.
 
 @param doc The JSON document.
    If this doc is NULL or has no root, the function will fail and return false.
 @param flg The JSON write options.
    Multiple options can be combined with `|` operator. 0 means no options.
 @param len A pointer to receive output length in bytes (not including the
    null-terminator). Pass NULL if you don't need length information.
 @return A new JSON string, or NULL if an error occurs.
    This string is encoded as UTF-8 with a null-terminator.
    When it's no longer needed, it should be freed with free().
 */
styyjson_api_inline char *styyjson_write(const styyjson_doc *doc,
                                     styyjson_write_flag flg,
                                     size_t *len) {
    return styyjson_write_opts(doc, flg, NULL, len, NULL);
}



/**
 Write a document to JSON string with options.
 
 This function is thread-safe when:
 1. The `doc` is not modified by other threads.
 2. The `alc` is thread-safe or NULL.

 @param doc The mutable JSON document.
    If this doc is NULL or has no root, the function will fail and return false.
 @param flg The JSON write options.
    Multiple options can be combined with `|` operator. 0 means no options.
 @param alc The memory allocator used by JSON writer.
    Pass NULL to use the libc's default allocator.
 @param len A pointer to receive output length in bytes (not including the
    null-terminator). Pass NULL if you don't need length information.
 @param err A pointer to receive error information.
    Pass NULL if you don't need error information.
 @return A new JSON string, or NULL if an error occurs.
    This string is encoded as UTF-8 with a null-terminator.
    When it's no longer needed, it should be freed with free() or alc->free().
 */
styyjson_api char *styyjson_mut_write_opts(const styyjson_mut_doc *doc,
                                       styyjson_write_flag flg,
                                       const styyjson_alc *alc,
                                       size_t *len,
                                       styyjson_write_err *err);

/**
 Write a document to JSON file with options.
 
 This function is thread-safe when:
 1. The file is not accessed by other threads.
 2. The `doc` is not modified by other threads.
 3. The `alc` is thread-safe or NULL.
 
 @param path The JSON file's path.
    If this path is NULL or invalid, the function will fail and return false.
    If this file is not empty, the content will be discarded.
 @param doc The mutable JSON document.
    If this doc is NULL or has no root, the function will fail and return false.
 @param flg The JSON write options.
    Multiple options can be combined with `|` operator. 0 means no options.
 @param alc The memory allocator used by JSON writer.
    Pass NULL to use the libc's default allocator.
 @param err A pointer to receive error information.
    Pass NULL if you don't need error information.
 @return true if successful, false if an error occurs.
 
 @warning On 32-bit operating system, files larger than 2GB may fail to write.
 */
styyjson_api bool styyjson_mut_write_file(const char *path,
                                      const styyjson_mut_doc *doc,
                                      styyjson_write_flag flg,
                                      const styyjson_alc *alc,
                                      styyjson_write_err *err);

/**
 Write a document to file pointer with options.
 
 @param fp The file pointer.
    The data will be written to the current position of the file.
    If this fp is NULL or invalid, the function will fail and return false.
 @param doc The mutable JSON document.
    If this doc is NULL or has no root, the function will fail and return false.
 @param flg The JSON write options.
    Multiple options can be combined with `|` operator. 0 means no options.
 @param alc The memory allocator used by JSON writer.
    Pass NULL to use the libc's default allocator.
 @param err A pointer to receive error information.
    Pass NULL if you don't need error information.
 @return true if successful, false if an error occurs.
 
 @warning On 32-bit operating system, files larger than 2GB may fail to write.
 */
styyjson_api bool styyjson_mut_write_fp(FILE *fp,
                                    const styyjson_mut_doc *doc,
                                    styyjson_write_flag flg,
                                    const styyjson_alc *alc,
                                    styyjson_write_err *err);

/**
 Write a document to JSON string.
 
 This function is thread-safe when:
 The `doc` is not modified by other threads.
 
 @param doc The JSON document.
    If this doc is NULL or has no root, the function will fail and return false.
 @param flg The JSON write options.
    Multiple options can be combined with `|` operator. 0 means no options.
 @param len A pointer to receive output length in bytes (not including the
    null-terminator). Pass NULL if you don't need length information.
 @return A new JSON string, or NULL if an error occurs.
    This string is encoded as UTF-8 with a null-terminator.
    When it's no longer needed, it should be freed with free().
 */
styyjson_api_inline char *styyjson_mut_write(const styyjson_mut_doc *doc,
                                         styyjson_write_flag flg,
                                         size_t *len) {
    return styyjson_mut_write_opts(doc, flg, NULL, len, NULL);
}



/*==============================================================================
 * JSON Value Writer API
 *============================================================================*/

/**
 Write a value to JSON string with options.
 
 This function is thread-safe when:
 The `alc` is thread-safe or NULL.
 
 @param val The JSON root value.
    If this parameter is NULL, the function will fail and return NULL.
 @param flg The JSON write options.
    Multiple options can be combined with `|` operator. 0 means no options.
 @param alc The memory allocator used by JSON writer.
    Pass NULL to use the libc's default allocator.
 @param len A pointer to receive output length in bytes (not including the
    null-terminator). Pass NULL if you don't need length information.
 @param err A pointer to receive error information.
    Pass NULL if you don't need error information.
 @return A new JSON string, or NULL if an error occurs.
    This string is encoded as UTF-8 with a null-terminator.
    When it's no longer needed, it should be freed with free() or alc->free().
 */
styyjson_api char *styyjson_val_write_opts(const styyjson_val *val,
                                       styyjson_write_flag flg,
                                       const styyjson_alc *alc,
                                       size_t *len,
                                       styyjson_write_err *err);

/**
 Write a value to JSON file with options.
 
 This function is thread-safe when:
 1. The file is not accessed by other threads.
 2. The `alc` is thread-safe or NULL.
 
 @param path The JSON file's path.
    If this path is NULL or invalid, the function will fail and return false.
    If this file is not empty, the content will be discarded.
 @param val The JSON root value.
    If this parameter is NULL, the function will fail and return NULL.
 @param flg The JSON write options.
    Multiple options can be combined with `|` operator. 0 means no options.
 @param alc The memory allocator used by JSON writer.
    Pass NULL to use the libc's default allocator.
 @param err A pointer to receive error information.
    Pass NULL if you don't need error information.
 @return true if successful, false if an error occurs.
 
 @warning On 32-bit operating system, files larger than 2GB may fail to write.
 */
styyjson_api bool styyjson_val_write_file(const char *path,
                                      const styyjson_val *val,
                                      styyjson_write_flag flg,
                                      const styyjson_alc *alc,
                                      styyjson_write_err *err);

/**
 Write a value to file pointer with options.
 
 @param fp The file pointer.
    The data will be written to the current position of the file.
    If this path is NULL or invalid, the function will fail and return false.
 @param val The JSON root value.
    If this parameter is NULL, the function will fail and return NULL.
 @param flg The JSON write options.
    Multiple options can be combined with `|` operator. 0 means no options.
 @param alc The memory allocator used by JSON writer.
    Pass NULL to use the libc's default allocator.
 @param err A pointer to receive error information.
    Pass NULL if you don't need error information.
 @return true if successful, false if an error occurs.
 
 @warning On 32-bit operating system, files larger than 2GB may fail to write.
 */
styyjson_api bool styyjson_val_write_fp(FILE *fp,
                                    const styyjson_val *val,
                                    styyjson_write_flag flg,
                                    const styyjson_alc *alc,
                                    styyjson_write_err *err);

/**
 Write a value to JSON string.
 
 This function is thread-safe.
 
 @param val The JSON root value.
    If this parameter is NULL, the function will fail and return NULL.
 @param flg The JSON write options.
    Multiple options can be combined with `|` operator. 0 means no options.
 @param len A pointer to receive output length in bytes (not including the
    null-terminator). Pass NULL if you don't need length information.
 @return A new JSON string, or NULL if an error occurs.
    This string is encoded as UTF-8 with a null-terminator.
    When it's no longer needed, it should be freed with free().
 */
styyjson_api_inline char *styyjson_val_write(const styyjson_val *val,
                                         styyjson_write_flag flg,
                                         size_t *len) {
    return styyjson_val_write_opts(val, flg, NULL, len, NULL);
}

/**
 Write a value to JSON string with options.
 
 This function is thread-safe when:
 1. The `val` is not modified by other threads.
 2. The `alc` is thread-safe or NULL.
 
 @param val The mutable JSON root value.
    If this parameter is NULL, the function will fail and return NULL.
 @param flg The JSON write options.
    Multiple options can be combined with `|` operator. 0 means no options.
 @param alc The memory allocator used by JSON writer.
    Pass NULL to use the libc's default allocator.
 @param len A pointer to receive output length in bytes (not including the
    null-terminator). Pass NULL if you don't need length information.
 @param err A pointer to receive error information.
    Pass NULL if you don't need error information.
 @return  A new JSON string, or NULL if an error occurs.
    This string is encoded as UTF-8 with a null-terminator.
    When it's no longer needed, it should be freed with free() or alc->free().
 */
styyjson_api char *styyjson_mut_val_write_opts(const styyjson_mut_val *val,
                                           styyjson_write_flag flg,
                                           const styyjson_alc *alc,
                                           size_t *len,
                                           styyjson_write_err *err);

/**
 Write a value to JSON file with options.
 
 This function is thread-safe when:
 1. The file is not accessed by other threads.
 2. The `val` is not modified by other threads.
 3. The `alc` is thread-safe or NULL.
 
 @param path The JSON file's path.
    If this path is NULL or invalid, the function will fail and return false.
    If this file is not empty, the content will be discarded.
 @param val The mutable JSON root value.
    If this parameter is NULL, the function will fail and return NULL.
 @param flg The JSON write options.
    Multiple options can be combined with `|` operator. 0 means no options.
 @param alc The memory allocator used by JSON writer.
    Pass NULL to use the libc's default allocator.
 @param err A pointer to receive error information.
    Pass NULL if you don't need error information.
 @return true if successful, false if an error occurs.
 
 @warning On 32-bit operating system, files larger than 2GB may fail to write.
 */
styyjson_api bool styyjson_mut_val_write_file(const char *path,
                                          const styyjson_mut_val *val,
                                          styyjson_write_flag flg,
                                          const styyjson_alc *alc,
                                          styyjson_write_err *err);

/**
 Write a value to JSON file with options.
 
 @param fp The file pointer.
    The data will be written to the current position of the file.
    If this path is NULL or invalid, the function will fail and return false.
 @param val The mutable JSON root value.
    If this parameter is NULL, the function will fail and return NULL.
 @param flg The JSON write options.
    Multiple options can be combined with `|` operator. 0 means no options.
 @param alc The memory allocator used by JSON writer.
    Pass NULL to use the libc's default allocator.
 @param err A pointer to receive error information.
    Pass NULL if you don't need error information.
 @return true if successful, false if an error occurs.
 
 @warning On 32-bit operating system, files larger than 2GB may fail to write.
 */
styyjson_api bool styyjson_mut_val_write_fp(FILE *fp,
                                        const styyjson_mut_val *val,
                                        styyjson_write_flag flg,
                                        const styyjson_alc *alc,
                                        styyjson_write_err *err);

/**
 Write a value to JSON string.
 
 This function is thread-safe when:
 The `val` is not modified by other threads.
 
 @param val The JSON root value.
    If this parameter is NULL, the function will fail and return NULL.
 @param flg The JSON write options.
    Multiple options can be combined with `|` operator. 0 means no options.
 @param len A pointer to receive output length in bytes (not including the
    null-terminator). Pass NULL if you don't need length information.
 @return A new JSON string, or NULL if an error occurs.
    This string is encoded as UTF-8 with a null-terminator.
    When it's no longer needed, it should be freed with free().
 */
styyjson_api_inline char *styyjson_mut_val_write(const styyjson_mut_val *val,
                                             styyjson_write_flag flg,
                                             size_t *len) {
    return styyjson_mut_val_write_opts(val, flg, NULL, len, NULL);
}



/*==============================================================================
 * JSON Document API
 *============================================================================*/

/** Returns the root value of this JSON document.
    Returns NULL if `doc` is NULL. */
styyjson_api_inline styyjson_val *styyjson_doc_get_root(styyjson_doc *doc);

/** Returns read size of input JSON data.
    Returns 0 if `doc` is NULL.
    For example: the read size of `[1,2,3]` is 7 bytes.  */
styyjson_api_inline size_t styyjson_doc_get_read_size(styyjson_doc *doc);

/** Returns total value count in this JSON document.
    Returns 0 if `doc` is NULL.
    For example: the value count of `[1,2,3]` is 4. */
styyjson_api_inline size_t styyjson_doc_get_val_count(styyjson_doc *doc);

/** Release the JSON document and free the memory.
    After calling this function, the `doc` and all values from the `doc` are no
    longer available. This function will do nothing if the `doc` is NULL. */
styyjson_api_inline void styyjson_doc_free(styyjson_doc *doc);



/*==============================================================================
 * JSON Value Type API
 *============================================================================*/

/** Returns whether the JSON value is raw.
    Returns false if `val` is NULL. */
styyjson_api_inline bool styyjson_is_raw(styyjson_val *val);

/** Returns whether the JSON value is `null`.
    Returns false if `val` is NULL. */
styyjson_api_inline bool styyjson_is_null(styyjson_val *val);

/** Returns whether the JSON value is `true`.
    Returns false if `val` is NULL. */
styyjson_api_inline bool styyjson_is_true(styyjson_val *val);

/** Returns whether the JSON value is `false`.
    Returns false if `val` is NULL. */
styyjson_api_inline bool styyjson_is_false(styyjson_val *val);

/** Returns whether the JSON value is bool (true/false).
    Returns false if `val` is NULL. */
styyjson_api_inline bool styyjson_is_bool(styyjson_val *val);

/** Returns whether the JSON value is unsigned integer (uint64_t).
    Returns false if `val` is NULL. */
styyjson_api_inline bool styyjson_is_uint(styyjson_val *val);

/** Returns whether the JSON value is signed integer (int64_t).
    Returns false if `val` is NULL. */
styyjson_api_inline bool styyjson_is_sint(styyjson_val *val);

/** Returns whether the JSON value is integer (uint64_t/int64_t).
    Returns false if `val` is NULL. */
styyjson_api_inline bool styyjson_is_int(styyjson_val *val);

/** Returns whether the JSON value is real number (double).
    Returns false if `val` is NULL. */
styyjson_api_inline bool styyjson_is_real(styyjson_val *val);

/** Returns whether the JSON value is number (uint64_t/int64_t/double).
    Returns false if `val` is NULL. */
styyjson_api_inline bool styyjson_is_num(styyjson_val *val);

/** Returns whether the JSON value is string.
    Returns false if `val` is NULL. */
styyjson_api_inline bool styyjson_is_str(styyjson_val *val);

/** Returns whether the JSON value is array.
    Returns false if `val` is NULL. */
styyjson_api_inline bool styyjson_is_arr(styyjson_val *val);

/** Returns whether the JSON value is object.
    Returns false if `val` is NULL. */
styyjson_api_inline bool styyjson_is_obj(styyjson_val *val);

/** Returns whether the JSON value is container (array/object).
    Returns false if `val` is NULL. */
styyjson_api_inline bool styyjson_is_ctn(styyjson_val *val);



/*==============================================================================
 * JSON Value Content API
 *============================================================================*/

/** Returns the JSON value's type.
    Returns YYJSON_TYPE_NONE if `val` is NULL. */
styyjson_api_inline styyjson_type styyjson_get_type(styyjson_val *val);

/** Returns the JSON value's subtype.
    Returns YYJSON_SUBTYPE_NONE if `val` is NULL. */
styyjson_api_inline styyjson_subtype styyjson_get_subtype(styyjson_val *val);

/** Returns the JSON value's tag.
    Returns 0 if `val` is NULL. */
styyjson_api_inline uint8_t styyjson_get_tag(styyjson_val *val);

/** Returns the JSON value's type description.
    The return value should be one of these strings: "raw", "null", "string",
    "array", "object", "true", "false", "uint", "sint", "real", "unknown". */
styyjson_api_inline const char *styyjson_get_type_desc(styyjson_val *val);

/** Returns the content if the value is raw.
    Returns NULL if `val` is NULL or type is not raw. */
styyjson_api_inline const char *styyjson_get_raw(styyjson_val *val);

/** Returns the content if the value is bool.
    Returns NULL if `val` is NULL or type is not bool. */
styyjson_api_inline bool styyjson_get_bool(styyjson_val *val);

/** Returns the content and cast to uint64_t.
    Returns 0 if `val` is NULL or type is not integer(sint/uint). */
styyjson_api_inline uint64_t styyjson_get_uint(styyjson_val *val);

/** Returns the content and cast to int64_t.
    Returns 0 if `val` is NULL or type is not integer(sint/uint). */
styyjson_api_inline int64_t styyjson_get_sint(styyjson_val *val);

/** Returns the content and cast to int.
    Returns 0 if `val` is NULL or type is not integer(sint/uint). */
styyjson_api_inline int styyjson_get_int(styyjson_val *val);

/** Returns the content if the value is real number, or 0.0 on error.
    Returns 0.0 if `val` is NULL or type is not real(double). */
styyjson_api_inline double styyjson_get_real(styyjson_val *val);

/** Returns the content and typecast to `double` if the value is number.
    Returns 0.0 if `val` is NULL or type is not number(uint/sint/real). */
styyjson_api_inline double styyjson_get_num(styyjson_val *val);

/** Returns the content if the value is string.
    Returns NULL if `val` is NULL or type is not string. */
styyjson_api_inline const char *styyjson_get_str(styyjson_val *val);

/** Returns the content length (string length, array size, object size.
    Returns 0 if `val` is NULL or type is not string/array/object. */
styyjson_api_inline size_t styyjson_get_len(styyjson_val *val);

/** Returns whether the JSON value is equals to a string.
    Returns false if input is NULL or type is not string. */
styyjson_api_inline bool styyjson_equals_str(styyjson_val *val, const char *str);

/** Returns whether the JSON value is equals to a string.
    The `str` should be a UTF-8 string, null-terminator is not required.
    Returns false if input is NULL or type is not string. */
styyjson_api_inline bool styyjson_equals_strn(styyjson_val *val, const char *str,
                                          size_t len);

/** Returns whether two JSON values are equal (deep compare).
    Returns false if input is NULL.
    @note the result may be inaccurate if object has duplicate keys.
    @warning This function is recursive and may cause a stack overflow
        if the object level is too deep. */
styyjson_api_inline bool styyjson_equals(styyjson_val *lhs, styyjson_val *rhs);

/** Set the value to raw.
    Returns false if input is NULL or `val` is object or array.
    @warning This will modify the `immutable` value, use with caution. */
styyjson_api_inline bool styyjson_set_raw(styyjson_val *val,
                                      const char *raw, size_t len);

/** Set the value to null.
    Returns false if input is NULL or `val` is object or array.
    @warning This will modify the `immutable` value, use with caution. */
styyjson_api_inline bool styyjson_set_null(styyjson_val *val);

/** Set the value to bool.
    Returns false if input is NULL or `val` is object or array.
    @warning This will modify the `immutable` value, use with caution. */
styyjson_api_inline bool styyjson_set_bool(styyjson_val *val, bool num);

/** Set the value to uint.
    Returns false if input is NULL or `val` is object or array.
    @warning This will modify the `immutable` value, use with caution. */
styyjson_api_inline bool styyjson_set_uint(styyjson_val *val, uint64_t num);

/** Set the value to sint.
    Returns false if input is NULL or `val` is object or array.
    @warning This will modify the `immutable` value, use with caution. */
styyjson_api_inline bool styyjson_set_sint(styyjson_val *val, int64_t num);

/** Set the value to int.
    Returns false if input is NULL or `val` is object or array.
    @warning This will modify the `immutable` value, use with caution. */
styyjson_api_inline bool styyjson_set_int(styyjson_val *val, int num);

/** Set the value to real.
    Returns false if input is NULL or `val` is object or array.
    @warning This will modify the `immutable` value, use with caution. */
styyjson_api_inline bool styyjson_set_real(styyjson_val *val, double num);

/** Set the value to string (null-terminated).
    Returns false if input is NULL or `val` is object or array.
    @warning This will modify the `immutable` value, use with caution. */
styyjson_api_inline bool styyjson_set_str(styyjson_val *val, const char *str);

/** Set the value to string (with length).
    Returns false if input is NULL or `val` is object or array.
    @warning This will modify the `immutable` value, use with caution. */
styyjson_api_inline bool styyjson_set_strn(styyjson_val *val,
                                       const char *str, size_t len);



/*==============================================================================
 * JSON Array API
 *============================================================================*/

/** Returns the number of elements in this array.
    Returns 0 if `arr` is NULL or type is not array. */
styyjson_api_inline size_t styyjson_arr_size(styyjson_val *arr);

/** Returns the element at the specified position in this array.
    Returns NULL if array is NULL/empty or the index is out of bounds.
    @warning This function takes a linear search time if array is not flat.
        For example: `[1,{},3]` is flat, `[1,[2],3]` is not flat. */
styyjson_api_inline styyjson_val *styyjson_arr_get(styyjson_val *arr, size_t idx);

/** Returns the first element of this array.
    Returns NULL if `arr` is NULL/empty or type is not array. */
styyjson_api_inline styyjson_val *styyjson_arr_get_first(styyjson_val *arr);

/** Returns the last element of this array.
    Returns NULL if `arr` is NULL/empty or type is not array.
    @warning This function takes a linear search time if array is not flat.
        For example: `[1,{},3]` is flat, `[1,[2],3]` is not flat.*/
styyjson_api_inline styyjson_val *styyjson_arr_get_last(styyjson_val *arr);



/*==============================================================================
 * JSON Array Iterator API
 *============================================================================*/

/**
 A JSON array iterator.
 
 @par Example
 @code
    styyjson_val *val;
    styyjson_arr_iter iter = styyjson_arr_iter_with(arr);
    while ((val = styyjson_arr_iter_next(&iter))) {
        your_func(val);
    }
 @endcode
 */
typedef struct styyjson_arr_iter {
    size_t idx; /**< next value's index */
    size_t max; /**< maximum index (arr.size) */
    styyjson_val *cur; /**< next value */
} styyjson_arr_iter;

/**
 Initialize an iterator for this array.
 
 @param arr The array to be iterated over.
    If this parameter is NULL or not an array, `iter` will be set to empty.
 @param iter The iterator to be initialized.
    If this parameter is NULL, the function will fail and return false.
 @return true if the `iter` has been successfully initialized.
 
 @note The iterator does not need to be destroyed.
 */
styyjson_api_inline bool styyjson_arr_iter_init(styyjson_val *arr,
                                            styyjson_arr_iter *iter);

/**
 Create an iterator with an array , same as `styyjson_arr_iter_init()`.
 
 @param arr The array to be iterated over.
    If this parameter is NULL or not an array, an empty iterator will returned.
 @return A new iterator for the array.
 
 @note The iterator does not need to be destroyed.
 */
styyjson_api_inline styyjson_arr_iter styyjson_arr_iter_with(styyjson_val *arr);

/**
 Returns whether the iteration has more elements.
 If `iter` is NULL, this function will return false.
 */
styyjson_api_inline bool styyjson_arr_iter_has_next(styyjson_arr_iter *iter);

/**
 Returns the next element in the iteration, or NULL on end.
 If `iter` is NULL, this function will return NULL.
 */
styyjson_api_inline styyjson_val *styyjson_arr_iter_next(styyjson_arr_iter *iter);

/**
 Macro for iterating over an array.
 It works like iterator, but with a more intuitive API.
 
 @par Example
 @code
    size_t idx, max;
    styyjson_val *val;
    styyjson_arr_foreach(arr, idx, max, val) {
        your_func(idx, val);
    }
 @endcode
 */
#define styyjson_arr_foreach(arr, idx, max, val) \
    for ((idx) = 0, \
        (max) = styyjson_arr_size(arr), \
        (val) = styyjson_arr_get_first(arr); \
        (idx) < (max); \
        (idx)++, \
        (val) = unsafe_styyjson_get_next(val))



/*==============================================================================
 * JSON Object API
 *============================================================================*/

/** Returns the number of key-value pairs in this object.
    Returns 0 if `obj` is NULL or type is not object. */
styyjson_api_inline size_t styyjson_obj_size(styyjson_val *obj);

/** Returns the value to which the specified key is mapped.
    Returns NULL if this object contains no mapping for the key.
    Returns NULL if `obj/key` is NULL, or type is not object.
    
    The `key` should be a null-terminated UTF-8 string.
    
    @warning This function takes a linear search time. */
styyjson_api_inline styyjson_val *styyjson_obj_get(styyjson_val *obj, const char *key);

/** Returns the value to which the specified key is mapped.
    Returns NULL if this object contains no mapping for the key.
    Returns NULL if `obj/key` is NULL, or type is not object.
    
    The `key` should be a UTF-8 string, null-terminator is not required.
    The `key_len` should be the length of the key, in bytes.
    
    @warning This function takes a linear search time. */
styyjson_api_inline styyjson_val *styyjson_obj_getn(styyjson_val *obj, const char *key,
                                              size_t key_len);



/*==============================================================================
 * JSON Object Iterator API
 *============================================================================*/

/**
 A JSON object iterator.
 
 @par Example
 @code
    styyjson_val *key, *val;
    styyjson_obj_iter iter = styyjson_obj_iter_with(obj);
    while ((key = styyjson_obj_iter_next(&iter))) {
        val = styyjson_obj_iter_get_val(key);
        your_func(key, val);
    }
 @endcode
 
 If the ordering of the keys is known at compile-time, you can use this method
 to speed up value lookups:
 @code
    // {"k1":1, "k2": 3, "k3": 3}
    styyjson_val *key, *val;
    styyjson_obj_iter iter = styyjson_obj_iter_with(obj);
    styyjson_val *v1 = styyjson_obj_iter_get(&iter, "k1");
    styyjson_val *v3 = styyjson_obj_iter_get(&iter, "k3");
 @endcode
 @see styyjson_obj_iter_get() and styyjson_obj_iter_getn()
 */
typedef struct styyjson_obj_iter {
    size_t idx; /**< next key's index */
    size_t max; /**< maximum key index (obj.size) */
    styyjson_val *cur; /**< next key */
    styyjson_val *obj; /**< the object being iterated */
} styyjson_obj_iter;

/**
 Initialize an iterator for this object.
 
 @param obj The object to be iterated over.
    If this parameter is NULL or not an object, `iter` will be set to empty.
 @param iter The iterator to be initialized.
    If this parameter is NULL, the function will fail and return false.
 @return true if the `iter` has been successfully initialized.
 
 @note The iterator does not need to be destroyed.
 */
styyjson_api_inline bool styyjson_obj_iter_init(styyjson_val *obj,
                                            styyjson_obj_iter *iter);

/**
 Create an iterator with an object, same as `styyjson_obj_iter_init()`.
 
 @param obj The object to be iterated over.
    If this parameter is NULL or not an object, an empty iterator will returned.
 @return A new iterator for the object.
 
 @note The iterator does not need to be destroyed.
 */
styyjson_api_inline styyjson_obj_iter styyjson_obj_iter_with(styyjson_val *obj);

/**
 Returns whether the iteration has more elements.
 If `iter` is NULL, this function will return false.
 */
styyjson_api_inline bool styyjson_obj_iter_has_next(styyjson_obj_iter *iter);

/**
 Returns the next key in the iteration, or NULL on end.
 If `iter` is NULL, this function will return NULL.
 */
styyjson_api_inline styyjson_val *styyjson_obj_iter_next(styyjson_obj_iter *iter);

/**
 Returns the value for key inside the iteration.
 If `iter` is NULL, this function will return NULL.
 */
styyjson_api_inline styyjson_val *styyjson_obj_iter_get_val(styyjson_val *key);

/**
 Iterates to a specified key and returns the value.
 
 This function does the same thing as `styyjson_obj_get()`, but is much faster
 if the ordering of the keys is known at compile-time and you are using the same
 order to look up the values. If the key exists in this object, then the
 iterator will stop at the next key, otherwise the iterator will not change and
 NULL is returned.
 
 @param iter The object iterator, should not be NULL.
 @param key The key, should be a UTF-8 string with null-terminator.
 @return The value to which the specified key is mapped.
    NULL if this object contains no mapping for the key or input is invalid.
 
 @warning This function takes a linear search time if the key is not nearby.
 */
styyjson_api_inline styyjson_val *styyjson_obj_iter_get(styyjson_obj_iter *iter,
                                                  const char *key);

/**
 Iterates to a specified key and returns the value.

 This function does the same thing as `styyjson_obj_getn()`, but is much faster
 if the ordering of the keys is known at compile-time and you are using the same
 order to look up the values. If the key exists in this object, then the
 iterator will stop at the next key, otherwise the iterator will not change and
 NULL is returned.
 
 @param iter The object iterator, should not be NULL.
 @param key The key, should be a UTF-8 string, null-terminator is not required.
 @param key_len The the length of `key`, in bytes.
 @return The value to which the specified key is mapped.
    NULL if this object contains no mapping for the key or input is invalid.
 
 @warning This function takes a linear search time if the key is not nearby.
 */
styyjson_api_inline styyjson_val *styyjson_obj_iter_getn(styyjson_obj_iter *iter,
                                                   const char *key,
                                                   size_t key_len);

/**
 Macro for iterating over an object.
 It works like iterator, but with a more intuitive API.
 
 @par Example
 @code
    size_t idx, max;
    styyjson_val *key, *val;
    styyjson_obj_foreach(obj, idx, max, key, val) {
        your_func(key, val);
    }
 @endcode
 */
#define styyjson_obj_foreach(obj, idx, max, key, val) \
    for ((idx) = 0, \
        (max) = styyjson_obj_size(obj), \
        (key) = (obj) ? unsafe_styyjson_get_first(obj) : NULL, \
        (val) = (key) + 1; \
        (idx) < (max); \
        (idx)++, \
        (key) = unsafe_styyjson_get_next(val), \
        (val) = (key) + 1)



/*==============================================================================
 * Mutable JSON Document API
 *============================================================================*/

/** Returns the root value of this JSON document.
    Returns NULL if `doc` is NULL. */
styyjson_api_inline styyjson_mut_val *styyjson_mut_doc_get_root(styyjson_mut_doc *doc);

/** Sets the root value of this JSON document.
    Pass NULL to clear root value of the document. */
styyjson_api_inline void styyjson_mut_doc_set_root(styyjson_mut_doc *doc,
                                               styyjson_mut_val *root);

/**
 Set the string pool size for a mutable document.
 This function does not allocate memory immediately, but uses the size when
 the next memory allocation is needed.
 
 If the caller knows the approximate bytes of strings that the document needs to
 store (e.g. copy string with `styyjson_mut_strcpy` function), setting a larger
 size can avoid multiple memory allocations and improve performance.
 
 @param doc The mutable document.
 @param len The desired string pool size in bytes (total string length).
 @return true if successful, false if size is 0 or overflow.
 */
styyjson_api bool styyjson_mut_doc_set_str_pool_size(styyjson_mut_doc *doc,
                                                 size_t len);

/**
 Set the value pool size for a mutable document.
 This function does not allocate memory immediately, but uses the size when
 the next memory allocation is needed.
 
 If the caller knows the approximate number of values that the document needs to
 store (e.g. create new value with `styyjson_mut_xxx` functions), setting a larger
 size can avoid multiple memory allocations and improve performance.
 
 @param doc The mutable document.
 @param count The desired value pool size (number of `styyjson_mut_val`).
 @return true if successful, false if size is 0 or overflow.
 */
styyjson_api bool styyjson_mut_doc_set_val_pool_size(styyjson_mut_doc *doc,
                                                 size_t count);

/** Release the JSON document and free the memory.
    After calling this function, the `doc` and all values from the `doc` are no
    longer available. This function will do nothing if the `doc` is NULL.  */
styyjson_api void styyjson_mut_doc_free(styyjson_mut_doc *doc);

/** Creates and returns a new mutable JSON document, returns NULL on error.
    If allocator is NULL, the default allocator will be used. */
styyjson_api styyjson_mut_doc *styyjson_mut_doc_new(const styyjson_alc *alc);

/** Copies and returns a new mutable document from input, returns NULL on error.
    This makes a `deep-copy` on the immutable document.
    If allocator is NULL, the default allocator will be used.
    @note `imut_doc` -> `mut_doc`. */
styyjson_api styyjson_mut_doc *styyjson_doc_mut_copy(styyjson_doc *doc,
                                               const styyjson_alc *alc);

/** Copies and returns a new mutable document from input, returns NULL on error.
    This makes a `deep-copy` on the mutable document.
    If allocator is NULL, the default allocator will be used.
    @note `mut_doc` -> `mut_doc`. */
styyjson_api styyjson_mut_doc *styyjson_mut_doc_mut_copy(styyjson_mut_doc *doc,
                                                   const styyjson_alc *alc);

/** Copies and returns a new mutable value from input, returns NULL on error.
    This makes a `deep-copy` on the immutable value.
    The memory was managed by mutable document.
    @note `imut_val` -> `mut_val`. */
styyjson_api styyjson_mut_val *styyjson_val_mut_copy(styyjson_mut_doc *doc,
                                               styyjson_val *val);

/** Copies and returns a new mutable value from input, returns NULL on error.
    This makes a `deep-copy` on the mutable value.
    The memory was managed by mutable document.
    @note `mut_val` -> `mut_val`.
    @warning This function is recursive and may cause a stack overflow
        if the object level is too deep. */
styyjson_api styyjson_mut_val *styyjson_mut_val_mut_copy(styyjson_mut_doc *doc,
                                                   styyjson_mut_val *val);

/** Copies and returns a new immutable document from input,
    returns NULL on error. This makes a `deep-copy` on the mutable document.
    The returned document should be freed with `styyjson_doc_free()`.
    @note `mut_doc` -> `imut_doc`.
    @warning This function is recursive and may cause a stack overflow
        if the object level is too deep. */
styyjson_api styyjson_doc *styyjson_mut_doc_imut_copy(styyjson_mut_doc *doc,
                                                const styyjson_alc *alc);

/** Copies and returns a new immutable document from input,
    returns NULL on error. This makes a `deep-copy` on the mutable value.
    The returned document should be freed with `styyjson_doc_free()`.
    @note `mut_val` -> `imut_doc`.
    @warning This function is recursive and may cause a stack overflow
        if the object level is too deep. */
styyjson_api styyjson_doc *styyjson_mut_val_imut_copy(styyjson_mut_val *val,
                                                const styyjson_alc *alc);



/*==============================================================================
 * Mutable JSON Value Type API
 *============================================================================*/

/** Returns whether the JSON value is raw.
    Returns false if `val` is NULL. */
styyjson_api_inline bool styyjson_mut_is_raw(styyjson_mut_val *val);

/** Returns whether the JSON value is `null`.
    Returns false if `val` is NULL. */
styyjson_api_inline bool styyjson_mut_is_null(styyjson_mut_val *val);

/** Returns whether the JSON value is `true`.
    Returns false if `val` is NULL. */
styyjson_api_inline bool styyjson_mut_is_true(styyjson_mut_val *val);

/** Returns whether the JSON value is `false`.
    Returns false if `val` is NULL. */
styyjson_api_inline bool styyjson_mut_is_false(styyjson_mut_val *val);

/** Returns whether the JSON value is bool (true/false).
    Returns false if `val` is NULL. */
styyjson_api_inline bool styyjson_mut_is_bool(styyjson_mut_val *val);

/** Returns whether the JSON value is unsigned integer (uint64_t).
    Returns false if `val` is NULL. */
styyjson_api_inline bool styyjson_mut_is_uint(styyjson_mut_val *val);

/** Returns whether the JSON value is signed integer (int64_t).
    Returns false if `val` is NULL. */
styyjson_api_inline bool styyjson_mut_is_sint(styyjson_mut_val *val);

/** Returns whether the JSON value is integer (uint64_t/int64_t).
    Returns false if `val` is NULL. */
styyjson_api_inline bool styyjson_mut_is_int(styyjson_mut_val *val);

/** Returns whether the JSON value is real number (double).
    Returns false if `val` is NULL. */
styyjson_api_inline bool styyjson_mut_is_real(styyjson_mut_val *val);

/** Returns whether the JSON value is number (uint/sint/real).
    Returns false if `val` is NULL. */
styyjson_api_inline bool styyjson_mut_is_num(styyjson_mut_val *val);

/** Returns whether the JSON value is string.
    Returns false if `val` is NULL. */
styyjson_api_inline bool styyjson_mut_is_str(styyjson_mut_val *val);

/** Returns whether the JSON value is array.
    Returns false if `val` is NULL. */
styyjson_api_inline bool styyjson_mut_is_arr(styyjson_mut_val *val);

/** Returns whether the JSON value is object.
    Returns false if `val` is NULL. */
styyjson_api_inline bool styyjson_mut_is_obj(styyjson_mut_val *val);

/** Returns whether the JSON value is container (array/object).
    Returns false if `val` is NULL. */
styyjson_api_inline bool styyjson_mut_is_ctn(styyjson_mut_val *val);



/*==============================================================================
 * Mutable JSON Value Content API
 *============================================================================*/

/** Returns the JSON value's type.
    Returns `YYJSON_TYPE_NONE` if `val` is NULL. */
styyjson_api_inline styyjson_type styyjson_mut_get_type(styyjson_mut_val *val);

/** Returns the JSON value's subtype.
    Returns `YYJSON_SUBTYPE_NONE` if `val` is NULL. */
styyjson_api_inline styyjson_subtype styyjson_mut_get_subtype(styyjson_mut_val *val);

/** Returns the JSON value's tag.
    Returns 0 if `val` is NULL. */
styyjson_api_inline uint8_t styyjson_mut_get_tag(styyjson_mut_val *val);

/** Returns the JSON value's type description.
    The return value should be one of these strings: "raw", "null", "string",
    "array", "object", "true", "false", "uint", "sint", "real", "unknown". */
styyjson_api_inline const char *styyjson_mut_get_type_desc(styyjson_mut_val *val);

/** Returns the content if the value is raw.
    Returns NULL if `val` is NULL or type is not raw. */
styyjson_api_inline const char *styyjson_mut_get_raw(styyjson_mut_val *val);

/** Returns the content if the value is bool.
    Returns NULL if `val` is NULL or type is not bool. */
styyjson_api_inline bool styyjson_mut_get_bool(styyjson_mut_val *val);

/** Returns the content and cast to uint64_t.
    Returns 0 if `val` is NULL or type is not integer(sint/uint). */
styyjson_api_inline uint64_t styyjson_mut_get_uint(styyjson_mut_val *val);

/** Returns the content and cast to int64_t.
    Returns 0 if `val` is NULL or type is not integer(sint/uint). */
styyjson_api_inline int64_t styyjson_mut_get_sint(styyjson_mut_val *val);

/** Returns the content and cast to int.
    Returns 0 if `val` is NULL or type is not integer(sint/uint). */
styyjson_api_inline int styyjson_mut_get_int(styyjson_mut_val *val);

/** Returns the content if the value is real number.
    Returns 0.0 if `val` is NULL or type is not real(double). */
styyjson_api_inline double styyjson_mut_get_real(styyjson_mut_val *val);

/** Returns the content and typecast to `double` if the value is number.
    Returns 0.0 if `val` is NULL or type is not number(uint/sint/real). */
styyjson_api_inline double styyjson_mut_get_num(styyjson_mut_val *val);

/** Returns the content if the value is string.
    Returns NULL if `val` is NULL or type is not string. */
styyjson_api_inline const char *styyjson_mut_get_str(styyjson_mut_val *val);

/** Returns the content length (string length, array size, object size.
    Returns 0 if `val` is NULL or type is not string/array/object. */
styyjson_api_inline size_t styyjson_mut_get_len(styyjson_mut_val *val);

/** Returns whether the JSON value is equals to a string.
    The `str` should be a null-terminated UTF-8 string.
    Returns false if input is NULL or type is not string. */
styyjson_api_inline bool styyjson_mut_equals_str(styyjson_mut_val *val,
                                             const char *str);

/** Returns whether the JSON value is equals to a string.
    The `str` should be a UTF-8 string, null-terminator is not required.
    Returns false if input is NULL or type is not string. */
styyjson_api_inline bool styyjson_mut_equals_strn(styyjson_mut_val *val,
                                              const char *str, size_t len);

/** Returns whether two JSON values are equal (deep compare).
    Returns false if input is NULL.
    @note the result may be inaccurate if object has duplicate keys.
    @warning This function is recursive and may cause a stack overflow
        if the object level is too deep. */
styyjson_api_inline bool styyjson_mut_equals(styyjson_mut_val *lhs,
                                         styyjson_mut_val *rhs);

/** Set the value to raw.
    Returns false if input is NULL.
    @warning This function should not be used on an existing object or array. */
styyjson_api_inline bool styyjson_mut_set_raw(styyjson_mut_val *val,
                                          const char *raw, size_t len);

/** Set the value to null.
    Returns false if input is NULL.
    @warning This function should not be used on an existing object or array. */
styyjson_api_inline bool styyjson_mut_set_null(styyjson_mut_val *val);

/** Set the value to bool.
    Returns false if input is NULL.
    @warning This function should not be used on an existing object or array. */
styyjson_api_inline bool styyjson_mut_set_bool(styyjson_mut_val *val, bool num);

/** Set the value to uint.
    Returns false if input is NULL.
    @warning This function should not be used on an existing object or array. */
styyjson_api_inline bool styyjson_mut_set_uint(styyjson_mut_val *val, uint64_t num);

/** Set the value to sint.
    Returns false if input is NULL.
    @warning This function should not be used on an existing object or array. */
styyjson_api_inline bool styyjson_mut_set_sint(styyjson_mut_val *val, int64_t num);

/** Set the value to int.
    Returns false if input is NULL.
    @warning This function should not be used on an existing object or array. */
styyjson_api_inline bool styyjson_mut_set_int(styyjson_mut_val *val, int num);

/** Set the value to real.
    Returns false if input is NULL.
    @warning This function should not be used on an existing object or array. */
styyjson_api_inline bool styyjson_mut_set_real(styyjson_mut_val *val, double num);

/** Set the value to string (null-terminated).
    Returns false if input is NULL.
    @warning This function should not be used on an existing object or array. */
styyjson_api_inline bool styyjson_mut_set_str(styyjson_mut_val *val, const char *str);

/** Set the value to string (with length).
    Returns false if input is NULL.
    @warning This function should not be used on an existing object or array. */
styyjson_api_inline bool styyjson_mut_set_strn(styyjson_mut_val *val,
                                           const char *str, size_t len);

/** Set the value to array.
    Returns false if input is NULL.
    @warning This function should not be used on an existing object or array. */
styyjson_api_inline bool styyjson_mut_set_arr(styyjson_mut_val *val);

/** Set the value to array.
    Returns false if input is NULL.
    @warning This function should not be used on an existing object or array. */
styyjson_api_inline bool styyjson_mut_set_obj(styyjson_mut_val *val);



/*==============================================================================
 * Mutable JSON Value Creation API
 *============================================================================*/

/** Creates and returns a raw value, returns NULL on error.
    The `str` should be a null-terminated UTF-8 string.
    
    @warning The input string is not copied, you should keep this string
        unmodified for the lifetime of this JSON document. */
styyjson_api_inline styyjson_mut_val *styyjson_mut_raw(styyjson_mut_doc *doc,
                                                 const char *str);

/** Creates and returns a raw value, returns NULL on error.
    The `str` should be a UTF-8 string, null-terminator is not required.
    
    @warning The input string is not copied, you should keep this string
        unmodified for the lifetime of this JSON document. */
styyjson_api_inline styyjson_mut_val *styyjson_mut_rawn(styyjson_mut_doc *doc,
                                                  const char *str,
                                                  size_t len);

/** Creates and returns a raw value, returns NULL on error.
    The `str` should be a null-terminated UTF-8 string.
    The input string is copied and held by the document. */
styyjson_api_inline styyjson_mut_val *styyjson_mut_rawcpy(styyjson_mut_doc *doc,
                                                    const char *str);

/** Creates and returns a raw value, returns NULL on error.
    The `str` should be a UTF-8 string, null-terminator is not required.
    The input string is copied and held by the document. */
styyjson_api_inline styyjson_mut_val *styyjson_mut_rawncpy(styyjson_mut_doc *doc,
                                                     const char *str,
                                                     size_t len);

/** Creates and returns a null value, returns NULL on error. */
styyjson_api_inline styyjson_mut_val *styyjson_mut_null(styyjson_mut_doc *doc);

/** Creates and returns a true value, returns NULL on error. */
styyjson_api_inline styyjson_mut_val *styyjson_mut_true(styyjson_mut_doc *doc);

/** Creates and returns a false value, returns NULL on error. */
styyjson_api_inline styyjson_mut_val *styyjson_mut_false(styyjson_mut_doc *doc);

/** Creates and returns a bool value, returns NULL on error. */
styyjson_api_inline styyjson_mut_val *styyjson_mut_bool(styyjson_mut_doc *doc,
                                                  bool val);

/** Creates and returns an unsigned integer value, returns NULL on error. */
styyjson_api_inline styyjson_mut_val *styyjson_mut_uint(styyjson_mut_doc *doc,
                                                  uint64_t num);

/** Creates and returns a signed integer value, returns NULL on error. */
styyjson_api_inline styyjson_mut_val *styyjson_mut_sint(styyjson_mut_doc *doc,
                                                  int64_t num);

/** Creates and returns a signed integer value, returns NULL on error. */
styyjson_api_inline styyjson_mut_val *styyjson_mut_int(styyjson_mut_doc *doc,
                                                 int64_t num);

/** Creates and returns an real number value, returns NULL on error. */
styyjson_api_inline styyjson_mut_val *styyjson_mut_real(styyjson_mut_doc *doc,
                                                  double num);

/** Creates and returns a string value, returns NULL on error.
    The `str` should be a null-terminated UTF-8 string.
    @warning The input string is not copied, you should keep this string
        unmodified for the lifetime of this JSON document. */
styyjson_api_inline styyjson_mut_val *styyjson_mut_str(styyjson_mut_doc *doc,
                                                 const char *str);

/** Creates and returns a string value, returns NULL on error.
    The `str` should be a UTF-8 string, null-terminator is not required.
    @warning The input string is not copied, you should keep this string
        unmodified for the lifetime of this JSON document. */
styyjson_api_inline styyjson_mut_val *styyjson_mut_strn(styyjson_mut_doc *doc,
                                                  const char *str,
                                                  size_t len);

/** Creates and returns a string value, returns NULL on error.
    The `str` should be a null-terminated UTF-8 string.
    The input string is copied and held by the document. */
styyjson_api_inline styyjson_mut_val *styyjson_mut_strcpy(styyjson_mut_doc *doc,
                                                    const char *str);

/** Creates and returns a string value, returns NULL on error.
    The `str` should be a UTF-8 string, null-terminator is not required.
    The input string is copied and held by the document. */
styyjson_api_inline styyjson_mut_val *styyjson_mut_strncpy(styyjson_mut_doc *doc,
                                                     const char *str,
                                                     size_t len);



/*==============================================================================
 * Mutable JSON Array API
 *============================================================================*/

/** Returns the number of elements in this array.
    Returns 0 if `arr` is NULL or type is not array. */
styyjson_api_inline size_t styyjson_mut_arr_size(styyjson_mut_val *arr);

/** Returns the element at the specified position in this array.
    Returns NULL if array is NULL/empty or the index is out of bounds.
    @warning This function takes a linear search time. */
styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_get(styyjson_mut_val *arr,
                                                     size_t idx);

/** Returns the first element of this array.
    Returns NULL if `arr` is NULL/empty or type is not array. */
styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_get_first(styyjson_mut_val *arr);

/** Returns the last element of this array.
    Returns NULL if `arr` is NULL/empty or type is not array. */
styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_get_last(styyjson_mut_val *arr);



/*==============================================================================
 * Mutable JSON Array Iterator API
 *============================================================================*/

/**
 A mutable JSON array iterator.
 
 @warning You should not modify the array while iterating over it, but you can
    use `styyjson_mut_arr_iter_remove()` to remove current value.
 
 @par Example
 @code
    styyjson_mut_val *val;
    styyjson_mut_arr_iter iter = styyjson_mut_arr_iter_with(arr);
    while ((val = styyjson_mut_arr_iter_next(&iter))) {
        your_func(val);
        if (your_val_is_unused(val)) {
            styyjson_mut_arr_iter_remove(&iter);
        }
    }
 @endcode
 */
typedef struct styyjson_mut_arr_iter {
    size_t idx; /**< next value's index */
    size_t max; /**< maximum index (arr.size) */
    styyjson_mut_val *cur; /**< current value */
    styyjson_mut_val *pre; /**< previous value */
    styyjson_mut_val *arr; /**< the array being iterated */
} styyjson_mut_arr_iter;

/**
 Initialize an iterator for this array.
 
 @param arr The array to be iterated over.
    If this parameter is NULL or not an array, `iter` will be set to empty.
 @param iter The iterator to be initialized.
    If this parameter is NULL, the function will fail and return false.
 @return true if the `iter` has been successfully initialized.
 
 @note The iterator does not need to be destroyed.
 */
styyjson_api_inline bool styyjson_mut_arr_iter_init(styyjson_mut_val *arr,
    styyjson_mut_arr_iter *iter);

/**
 Create an iterator with an array , same as `styyjson_mut_arr_iter_init()`.
 
 @param arr The array to be iterated over.
    If this parameter is NULL or not an array, an empty iterator will returned.
 @return A new iterator for the array.
 
 @note The iterator does not need to be destroyed.
 */
styyjson_api_inline styyjson_mut_arr_iter styyjson_mut_arr_iter_with(
    styyjson_mut_val *arr);

/**
 Returns whether the iteration has more elements.
 If `iter` is NULL, this function will return false.
 */
styyjson_api_inline bool styyjson_mut_arr_iter_has_next(
    styyjson_mut_arr_iter *iter);

/**
 Returns the next element in the iteration, or NULL on end.
 If `iter` is NULL, this function will return NULL.
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_iter_next(
    styyjson_mut_arr_iter *iter);

/**
 Removes and returns current element in the iteration.
 If `iter` is NULL, this function will return NULL.
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_iter_remove(
    styyjson_mut_arr_iter *iter);

/**
 Macro for iterating over an array.
 It works like iterator, but with a more intuitive API.
 
 @warning You should not modify the array while iterating over it.
 
 @par Example
 @code
    size_t idx, max;
    styyjson_mut_val *val;
    styyjson_mut_arr_foreach(arr, idx, max, val) {
        your_func(idx, val);
    }
 @endcode
 */
#define styyjson_mut_arr_foreach(arr, idx, max, val) \
    for ((idx) = 0, \
        (max) = styyjson_mut_arr_size(arr), \
        (val) = styyjson_mut_arr_get_first(arr); \
        (idx) < (max); \
        (idx)++, \
        (val) = (val)->next)



/*==============================================================================
 * Mutable JSON Array Creation API
 *============================================================================*/

/**
 Creates and returns an empty mutable array.
 @param doc A mutable document, used for memory allocation only.
 @return The new array. NULL if input is NULL or memory allocation failed.
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_arr(styyjson_mut_doc *doc);

/**
 Creates and returns a new mutable array with the given boolean values.
 
 @param doc A mutable document, used for memory allocation only.
    If this parameter is NULL, the function will fail and return NULL.
 @param vals A C array of boolean values.
 @param count The value count. If this value is 0, an empty array will return.
 @return The new array. NULL if input is invalid or memory allocation failed.
 
 @par Example
 @code
    const bool vals[3] = { true, false, true };
    styyjson_mut_val *arr = styyjson_mut_arr_with_bool(doc, vals, 3);
 @endcode
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_with_bool(
    styyjson_mut_doc *doc, const bool *vals, size_t count);

/**
 Creates and returns a new mutable array with the given sint numbers.
 
 @param doc A mutable document, used for memory allocation only.
    If this parameter is NULL, the function will fail and return NULL.
 @param vals A C array of sint numbers.
 @param count The number count. If this value is 0, an empty array will return.
 @return The new array. NULL if input is invalid or memory allocation failed.
 
 @par Example
 @code
    const int64_t vals[3] = { -1, 0, 1 };
    styyjson_mut_val *arr = styyjson_mut_arr_with_sint64(doc, vals, 3);
 @endcode
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_with_sint(
    styyjson_mut_doc *doc, const int64_t *vals, size_t count);

/**
 Creates and returns a new mutable array with the given uint numbers.
 
 @param doc A mutable document, used for memory allocation only.
    If this parameter is NULL, the function will fail and return NULL.
 @param vals A C array of uint numbers.
 @param count The number count. If this value is 0, an empty array will return.
 @return The new array. NULL if input is invalid or memory allocation failed.
 
 @par Example
 @code
    const uint64_t vals[3] = { 0, 1, 0 };
    styyjson_mut_val *arr = styyjson_mut_arr_with_uint(doc, vals, 3);
 @endcode
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_with_uint(
    styyjson_mut_doc *doc, const uint64_t *vals, size_t count);

/**
 Creates and returns a new mutable array with the given real numbers.
 
 @param doc A mutable document, used for memory allocation only.
    If this parameter is NULL, the function will fail and return NULL.
 @param vals A C array of real numbers.
 @param count The number count. If this value is 0, an empty array will return.
 @return The new array. NULL if input is invalid or memory allocation failed.
 
 @par Example
 @code
    const double vals[3] = { 0.1, 0.2, 0.3 };
    styyjson_mut_val *arr = styyjson_mut_arr_with_real(doc, vals, 3);
 @endcode
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_with_real(
    styyjson_mut_doc *doc, const double *vals, size_t count);

/**
 Creates and returns a new mutable array with the given int8 numbers.
 
 @param doc A mutable document, used for memory allocation only.
    If this parameter is NULL, the function will fail and return NULL.
 @param vals A C array of int8 numbers.
 @param count The number count. If this value is 0, an empty array will return.
 @return The new array. NULL if input is invalid or memory allocation failed.
 
 @par Example
 @code
    const int8_t vals[3] = { -1, 0, 1 };
    styyjson_mut_val *arr = styyjson_mut_arr_with_sint8(doc, vals, 3);
 @endcode
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_with_sint8(
    styyjson_mut_doc *doc, const int8_t *vals, size_t count);

/**
 Creates and returns a new mutable array with the given int16 numbers.
 
 @param doc A mutable document, used for memory allocation only.
    If this parameter is NULL, the function will fail and return NULL.
 @param vals A C array of int16 numbers.
 @param count The number count. If this value is 0, an empty array will return.
 @return The new array. NULL if input is invalid or memory allocation failed.
 
 @par Example
 @code
    const int16_t vals[3] = { -1, 0, 1 };
    styyjson_mut_val *arr = styyjson_mut_arr_with_sint16(doc, vals, 3);
 @endcode
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_with_sint16(
    styyjson_mut_doc *doc, const int16_t *vals, size_t count);

/**
 Creates and returns a new mutable array with the given int32 numbers.
 
 @param doc A mutable document, used for memory allocation only.
    If this parameter is NULL, the function will fail and return NULL.
 @param vals A C array of int32 numbers.
 @param count The number count. If this value is 0, an empty array will return.
 @return The new array. NULL if input is invalid or memory allocation failed.
 
 @par Example
 @code
    const int32_t vals[3] = { -1, 0, 1 };
    styyjson_mut_val *arr = styyjson_mut_arr_with_sint32(doc, vals, 3);
 @endcode
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_with_sint32(
    styyjson_mut_doc *doc, const int32_t *vals, size_t count);

/**
 Creates and returns a new mutable array with the given int64 numbers.
 
 @param doc A mutable document, used for memory allocation only.
    If this parameter is NULL, the function will fail and return NULL.
 @param vals A C array of int64 numbers.
 @param count The number count. If this value is 0, an empty array will return.
 @return The new array. NULL if input is invalid or memory allocation failed.
 
 @par Example
 @code
    const int64_t vals[3] = { -1, 0, 1 };
    styyjson_mut_val *arr = styyjson_mut_arr_with_sint64(doc, vals, 3);
 @endcode
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_with_sint64(
    styyjson_mut_doc *doc, const int64_t *vals, size_t count);

/**
 Creates and returns a new mutable array with the given uint8 numbers.
 
 @param doc A mutable document, used for memory allocation only.
    If this parameter is NULL, the function will fail and return NULL.
 @param vals A C array of uint8 numbers.
 @param count The number count. If this value is 0, an empty array will return.
 @return The new array. NULL if input is invalid or memory allocation failed.
 
 @par Example
 @code
    const uint8_t vals[3] = { 0, 1, 0 };
    styyjson_mut_val *arr = styyjson_mut_arr_with_uint8(doc, vals, 3);
 @endcode
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_with_uint8(
    styyjson_mut_doc *doc, const uint8_t *vals, size_t count);

/**
 Creates and returns a new mutable array with the given uint16 numbers.
 
 @param doc A mutable document, used for memory allocation only.
    If this parameter is NULL, the function will fail and return NULL.
 @param vals A C array of uint16 numbers.
 @param count The number count. If this value is 0, an empty array will return.
 @return The new array. NULL if input is invalid or memory allocation failed.
 
 @par Example
 @code
    const uint16_t vals[3] = { 0, 1, 0 };
    styyjson_mut_val *arr = styyjson_mut_arr_with_uint16(doc, vals, 3);
 @endcode
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_with_uint16(
    styyjson_mut_doc *doc, const uint16_t *vals, size_t count);

/**
 Creates and returns a new mutable array with the given uint32 numbers.
 
 @param doc A mutable document, used for memory allocation only.
    If this parameter is NULL, the function will fail and return NULL.
 @param vals A C array of uint32 numbers.
 @param count The number count. If this value is 0, an empty array will return.
 @return The new array. NULL if input is invalid or memory allocation failed.
 
 @par Example
 @code
    const uint32_t vals[3] = { 0, 1, 0 };
    styyjson_mut_val *arr = styyjson_mut_arr_with_uint32(doc, vals, 3);
 @endcode
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_with_uint32(
    styyjson_mut_doc *doc, const uint32_t *vals, size_t count);

/**
 Creates and returns a new mutable array with the given uint64 numbers.
 
 @param doc A mutable document, used for memory allocation only.
    If this parameter is NULL, the function will fail and return NULL.
 @param vals A C array of uint64 numbers.
 @param count The number count. If this value is 0, an empty array will return.
 @return The new array. NULL if input is invalid or memory allocation failed.
 
 @par Example
 @code
     const uint64_t vals[3] = { 0, 1, 0 };
     styyjson_mut_val *arr = styyjson_mut_arr_with_uint64(doc, vals, 3);
 @endcode
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_with_uint64(
    styyjson_mut_doc *doc, const uint64_t *vals, size_t count);

/**
 Creates and returns a new mutable array with the given float numbers.
 
 @param doc A mutable document, used for memory allocation only.
    If this parameter is NULL, the function will fail and return NULL.
 @param vals A C array of float numbers.
 @param count The number count. If this value is 0, an empty array will return.
 @return The new array. NULL if input is invalid or memory allocation failed.
 
 @par Example
 @code
    const float vals[3] = { -1.0f, 0.0f, 1.0f };
    styyjson_mut_val *arr = styyjson_mut_arr_with_float(doc, vals, 3);
 @endcode
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_with_float(
    styyjson_mut_doc *doc, const float *vals, size_t count);

/**
 Creates and returns a new mutable array with the given double numbers.
 
 @param doc A mutable document, used for memory allocation only.
    If this parameter is NULL, the function will fail and return NULL.
 @param vals A C array of double numbers.
 @param count The number count. If this value is 0, an empty array will return.
 @return The new array. NULL if input is invalid or memory allocation failed.
 
 @par Example
 @code
    const double vals[3] = { -1.0, 0.0, 1.0 };
    styyjson_mut_val *arr = styyjson_mut_arr_with_double(doc, vals, 3);
 @endcode
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_with_double(
    styyjson_mut_doc *doc, const double *vals, size_t count);

/**
 Creates and returns a new mutable array with the given strings, these strings
 will not be copied.
 
 @param doc A mutable document, used for memory allocation only.
    If this parameter is NULL, the function will fail and return NULL.
 @param vals A C array of UTF-8 null-terminator strings.
    If this array contains NULL, the function will fail and return NULL.
 @param count The number of values in `vals`.
    If this value is 0, an empty array will return.
 @return The new array. NULL if input is invalid or memory allocation failed.
 
 @warning The input strings are not copied, you should keep these strings
    unmodified for the lifetime of this JSON document. If these strings will be
    modified, you should use `styyjson_mut_arr_with_strcpy()` instead.
 
 @par Example
 @code
    const char *vals[3] = { "a", "b", "c" };
    styyjson_mut_val *arr = styyjson_mut_arr_with_str(doc, vals, 3);
 @endcode
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_with_str(
    styyjson_mut_doc *doc, const char **vals, size_t count);

/**
 Creates and returns a new mutable array with the given strings and string
 lengths, these strings will not be copied.
 
 @param doc A mutable document, used for memory allocation only.
    If this parameter is NULL, the function will fail and return NULL.
 @param vals A C array of UTF-8 strings, null-terminator is not required.
    If this array contains NULL, the function will fail and return NULL.
 @param lens A C array of string lengths, in bytes.
 @param count The number of strings in `vals`.
    If this value is 0, an empty array will return.
 @return The new array. NULL if input is invalid or memory allocation failed.
 
 @warning The input strings are not copied, you should keep these strings
    unmodified for the lifetime of this JSON document. If these strings will be
    modified, you should use `styyjson_mut_arr_with_strncpy()` instead.
 
 @par Example
 @code
    const char *vals[3] = { "a", "bb", "c" };
    const size_t lens[3] = { 1, 2, 1 };
    styyjson_mut_val *arr = styyjson_mut_arr_with_strn(doc, vals, lens, 3);
 @endcode
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_with_strn(
    styyjson_mut_doc *doc, const char **vals, const size_t *lens, size_t count);

/**
 Creates and returns a new mutable array with the given strings, these strings
 will be copied.
 
 @param doc A mutable document, used for memory allocation only.
    If this parameter is NULL, the function will fail and return NULL.
 @param vals A C array of UTF-8 null-terminator strings.
    If this array contains NULL, the function will fail and return NULL.
 @param count The number of values in `vals`.
    If this value is 0, an empty array will return.
 @return The new array. NULL if input is invalid or memory allocation failed.
 
 @par Example
 @code
    const char *vals[3] = { "a", "b", "c" };
    styyjson_mut_val *arr = styyjson_mut_arr_with_strcpy(doc, vals, 3);
 @endcode
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_with_strcpy(
    styyjson_mut_doc *doc, const char **vals, size_t count);

/**
 Creates and returns a new mutable array with the given strings and string
 lengths, these strings will be copied.
 
 @param doc A mutable document, used for memory allocation only.
    If this parameter is NULL, the function will fail and return NULL.
 @param vals A C array of UTF-8 strings, null-terminator is not required.
    If this array contains NULL, the function will fail and return NULL.
 @param lens A C array of string lengths, in bytes.
 @param count The number of strings in `vals`.
    If this value is 0, an empty array will return.
 @return The new array. NULL if input is invalid or memory allocation failed.
 
 @par Example
 @code
    const char *vals[3] = { "a", "bb", "c" };
    const size_t lens[3] = { 1, 2, 1 };
    styyjson_mut_val *arr = styyjson_mut_arr_with_strn(doc, vals, lens, 3);
 @endcode
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_with_strncpy(
    styyjson_mut_doc *doc, const char **vals, const size_t *lens, size_t count);



/*==============================================================================
 * Mutable JSON Array Modification API
 *============================================================================*/

/**
 Inserts a value into an array at a given index.
 @param arr The array to which the value is to be inserted.
    Returns false if it is NULL or not an array.
 @param val The value to be inserted. Returns false if it is NULL.
 @param idx The index to which to insert the new value.
    Returns false if the index is out of range.
 @return Whether successful.
 @warning This function takes a linear search time.
 */
styyjson_api_inline bool styyjson_mut_arr_insert(styyjson_mut_val *arr,
                                             styyjson_mut_val *val, size_t idx);

/**
 Inserts a value at the end of the array.
 @param arr The array to which the value is to be inserted.
    Returns false if it is NULL or not an array.
 @param val The value to be inserted. Returns false if it is NULL.
 @return Whether successful.
 */
styyjson_api_inline bool styyjson_mut_arr_append(styyjson_mut_val *arr,
                                             styyjson_mut_val *val);

/**
 Inserts a value at the head of the array.
 @param arr The array to which the value is to be inserted.
    Returns false if it is NULL or not an array.
 @param val The value to be inserted. Returns false if it is NULL.
 @return    Whether successful.
 */
styyjson_api_inline bool styyjson_mut_arr_prepend(styyjson_mut_val *arr,
                                              styyjson_mut_val *val);

/**
 Replaces a value at index and returns old value.
 @param arr The array to which the value is to be replaced.
    Returns false if it is NULL or not an array.
 @param idx The index to which to replace the value.
    Returns false if the index is out of range.
 @param val The new value to replace. Returns false if it is NULL.
 @return Old value, or NULL on error.
 @warning This function takes a linear search time.
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_replace(styyjson_mut_val *arr,
                                                         size_t idx,
                                                         styyjson_mut_val *val);

/**
 Removes and returns a value at index.
 @param arr The array from which the value is to be removed.
    Returns false if it is NULL or not an array.
 @param idx The index from which to remove the value.
    Returns false if the index is out of range.
 @return Old value, or NULL on error.
 @warning This function takes a linear search time.
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_remove(styyjson_mut_val *arr,
                                                        size_t idx);

/**
 Removes and returns the first value in this array.
 @param arr The array from which the value is to be removed.
    Returns false if it is NULL or not an array.
 @return The first value, or NULL on error.
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_remove_first(
    styyjson_mut_val *arr);

/**
 Removes and returns the last value in this array.
 @param arr The array from which the value is to be removed.
    Returns false if it is NULL or not an array.
 @return The last value, or NULL on error.
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_remove_last(
    styyjson_mut_val *arr);

/**
 Removes all values within a specified range in the array.
 @param arr The array from which the value is to be removed.
    Returns false if it is NULL or not an array.
 @param idx The start index of the range (0 is the first).
 @param len The number of items in the range (can be 0).
 @return Whether successful.
 @warning This function takes a linear search time.
 */
styyjson_api_inline bool styyjson_mut_arr_remove_range(styyjson_mut_val *arr,
                                                   size_t idx, size_t len);

/**
 Removes all values in this array.
 @param arr The array from which all of the values are to be removed.
    Returns false if it is NULL or not an array.
 @return Whether successful.
 */
styyjson_api_inline bool styyjson_mut_arr_clear(styyjson_mut_val *arr);

/**
 Rotates values in this array for the given number of times.
 For example: `[1,2,3,4,5]` rotate 2 is `[3,4,5,1,2]`.
 @param arr The array to be rotated.
 @param idx Index (or times) to rotate.
 @warning This function takes a linear search time.
 */
styyjson_api_inline bool styyjson_mut_arr_rotate(styyjson_mut_val *arr,
                                             size_t idx);



/*==============================================================================
 * Mutable JSON Array Modification Convenience API
 *============================================================================*/

/**
 Adds a value at the end of the array.
 @param arr The array to which the value is to be inserted.
    Returns false if it is NULL or not an array.
 @param val The value to be inserted. Returns false if it is NULL.
 @return Whether successful.
 */
styyjson_api_inline bool styyjson_mut_arr_add_val(styyjson_mut_val *arr,
                                              styyjson_mut_val *val);

/**
 Adds a `null` value at the end of the array.
 @param doc The `doc` is only used for memory allocation.
 @param arr The array to which the value is to be inserted.
    Returns false if it is NULL or not an array.
 @return Whether successful.
 */
styyjson_api_inline bool styyjson_mut_arr_add_null(styyjson_mut_doc *doc,
                                               styyjson_mut_val *arr);

/**
 Adds a `true` value at the end of the array.
 @param doc The `doc` is only used for memory allocation.
 @param arr The array to which the value is to be inserted.
    Returns false if it is NULL or not an array.
 @return Whether successful.
 */
styyjson_api_inline bool styyjson_mut_arr_add_true(styyjson_mut_doc *doc,
                                               styyjson_mut_val *arr);

/**
 Adds a `false` value at the end of the array.
 @param doc The `doc` is only used for memory allocation.
 @param arr The array to which the value is to be inserted.
    Returns false if it is NULL or not an array.
 @return Whether successful.
 */
styyjson_api_inline bool styyjson_mut_arr_add_false(styyjson_mut_doc *doc,
                                                styyjson_mut_val *arr);

/**
 Adds a bool value at the end of the array.
 @param doc The `doc` is only used for memory allocation.
 @param arr The array to which the value is to be inserted.
    Returns false if it is NULL or not an array.
 @param val The bool value to be added.
 @return Whether successful.
 */
styyjson_api_inline bool styyjson_mut_arr_add_bool(styyjson_mut_doc *doc,
                                               styyjson_mut_val *arr,
                                               bool val);

/**
 Adds an unsigned integer value at the end of the array.
 @param doc The `doc` is only used for memory allocation.
 @param arr The array to which the value is to be inserted.
    Returns false if it is NULL or not an array.
 @param num The number to be added.
 @return Whether successful.
 */
styyjson_api_inline bool styyjson_mut_arr_add_uint(styyjson_mut_doc *doc,
                                               styyjson_mut_val *arr,
                                               uint64_t num);

/**
 Adds a signed integer value at the end of the array.
 @param doc The `doc` is only used for memory allocation.
 @param arr The array to which the value is to be inserted.
    Returns false if it is NULL or not an array.
 @param num The number to be added.
 @return Whether successful.
 */
styyjson_api_inline bool styyjson_mut_arr_add_sint(styyjson_mut_doc *doc,
                                               styyjson_mut_val *arr,
                                               int64_t num);

/**
 Adds a integer value at the end of the array.
 @param doc The `doc` is only used for memory allocation.
 @param arr The array to which the value is to be inserted.
    Returns false if it is NULL or not an array.
 @param num The number to be added.
 @return Whether successful.
 */
styyjson_api_inline bool styyjson_mut_arr_add_int(styyjson_mut_doc *doc,
                                              styyjson_mut_val *arr,
                                              int64_t num);

/**
 Adds a double value at the end of the array.
 @param doc The `doc` is only used for memory allocation.
 @param arr The array to which the value is to be inserted.
    Returns false if it is NULL or not an array.
 @param num The number to be added.
 @return Whether successful.
 */
styyjson_api_inline bool styyjson_mut_arr_add_real(styyjson_mut_doc *doc,
                                               styyjson_mut_val *arr,
                                               double num);

/**
 Adds a string value at the end of the array (no copy).
 @param doc The `doc` is only used for memory allocation.
 @param arr The array to which the value is to be inserted.
    Returns false if it is NULL or not an array.
 @param str A null-terminated UTF-8 string.
 @return Whether successful.
 @warning The input string is not copied, you should keep this string unmodified
    for the lifetime of this JSON document.
 */
styyjson_api_inline bool styyjson_mut_arr_add_str(styyjson_mut_doc *doc,
                                              styyjson_mut_val *arr,
                                              const char *str);

/**
 Adds a string value at the end of the array (no copy).
 @param doc The `doc` is only used for memory allocation.
 @param arr The array to which the value is to be inserted.
    Returns false if it is NULL or not an array.
 @param str A UTF-8 string, null-terminator is not required.
 @param len The length of the string, in bytes.
 @return Whether successful.
 @warning The input string is not copied, you should keep this string unmodified
    for the lifetime of this JSON document.
 */
styyjson_api_inline bool styyjson_mut_arr_add_strn(styyjson_mut_doc *doc,
                                               styyjson_mut_val *arr,
                                               const char *str,
                                               size_t len);

/**
 Adds a string value at the end of the array (copied).
 @param doc The `doc` is only used for memory allocation.
 @param arr The array to which the value is to be inserted.
    Returns false if it is NULL or not an array.
 @param str A null-terminated UTF-8 string.
 @return Whether successful.
 */
styyjson_api_inline bool styyjson_mut_arr_add_strcpy(styyjson_mut_doc *doc,
                                                 styyjson_mut_val *arr,
                                                 const char *str);

/**
 Adds a string value at the end of the array (copied).
 @param doc The `doc` is only used for memory allocation.
 @param arr The array to which the value is to be inserted.
    Returns false if it is NULL or not an array.
 @param str A UTF-8 string, null-terminator is not required.
 @param len The length of the string, in bytes.
 @return Whether successful.
 */
styyjson_api_inline bool styyjson_mut_arr_add_strncpy(styyjson_mut_doc *doc,
                                                  styyjson_mut_val *arr,
                                                  const char *str,
                                                  size_t len);

/**
 Creates and adds a new array at the end of the array.
 @param doc The `doc` is only used for memory allocation.
 @param arr The array to which the value is to be inserted.
    Returns false if it is NULL or not an array.
 @return The new array, or NULL on error.
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_add_arr(styyjson_mut_doc *doc,
                                                         styyjson_mut_val *arr);

/**
 Creates and adds a new object at the end of the array.
 @param doc The `doc` is only used for memory allocation.
 @param arr The array to which the value is to be inserted.
    Returns false if it is NULL or not an array.
 @return The new object, or NULL on error.
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_add_obj(styyjson_mut_doc *doc,
                                                         styyjson_mut_val *arr);



/*==============================================================================
 * Mutable JSON Object API
 *============================================================================*/

/** Returns the number of key-value pairs in this object.
    Returns 0 if `obj` is NULL or type is not object. */
styyjson_api_inline size_t styyjson_mut_obj_size(styyjson_mut_val *obj);

/** Returns the value to which the specified key is mapped.
    Returns NULL if this object contains no mapping for the key.
    Returns NULL if `obj/key` is NULL, or type is not object.
    
    The `key` should be a null-terminated UTF-8 string.
    
    @warning This function takes a linear search time. */
styyjson_api_inline styyjson_mut_val *styyjson_mut_obj_get(styyjson_mut_val *obj,
                                                     const char *key);

/** Returns the value to which the specified key is mapped.
    Returns NULL if this object contains no mapping for the key.
    Returns NULL if `obj/key` is NULL, or type is not object.
    
    The `key` should be a UTF-8 string, null-terminator is not required.
    The `key_len` should be the length of the key, in bytes.
    
    @warning This function takes a linear search time. */
styyjson_api_inline styyjson_mut_val *styyjson_mut_obj_getn(styyjson_mut_val *obj,
                                                      const char *key,
                                                      size_t key_len);



/*==============================================================================
 * Mutable JSON Object Iterator API
 *============================================================================*/

/**
 A mutable JSON object iterator.
 
 @warning You should not modify the object while iterating over it, but you can
    use `styyjson_mut_obj_iter_remove()` to remove current value.
 
 @par Example
 @code
    styyjson_mut_val *key, *val;
    styyjson_mut_obj_iter iter = styyjson_mut_obj_iter_with(obj);
    while ((key = styyjson_mut_obj_iter_next(&iter))) {
        val = styyjson_mut_obj_iter_get_val(key);
        your_func(key, val);
        if (your_val_is_unused(key, val)) {
            styyjson_mut_obj_iter_remove(&iter);
        }
    }
 @endcode
 
 If the ordering of the keys is known at compile-time, you can use this method
 to speed up value lookups:
 @code
    // {"k1":1, "k2": 3, "k3": 3}
    styyjson_mut_val *key, *val;
    styyjson_mut_obj_iter iter = styyjson_mut_obj_iter_with(obj);
    styyjson_mut_val *v1 = styyjson_mut_obj_iter_get(&iter, "k1");
    styyjson_mut_val *v3 = styyjson_mut_obj_iter_get(&iter, "k3");
 @endcode
 @see `styyjson_mut_obj_iter_get()` and `styyjson_mut_obj_iter_getn()`
 */
typedef struct styyjson_mut_obj_iter {
    size_t idx; /**< next key's index */
    size_t max; /**< maximum key index (obj.size) */
    styyjson_mut_val *cur; /**< current key */
    styyjson_mut_val *pre; /**< previous key */
    styyjson_mut_val *obj; /**< the object being iterated */
} styyjson_mut_obj_iter;

/**
 Initialize an iterator for this object.
 
 @param obj The object to be iterated over.
    If this parameter is NULL or not an array, `iter` will be set to empty.
 @param iter The iterator to be initialized.
    If this parameter is NULL, the function will fail and return false.
 @return true if the `iter` has been successfully initialized.
 
 @note The iterator does not need to be destroyed.
 */
styyjson_api_inline bool styyjson_mut_obj_iter_init(styyjson_mut_val *obj,
    styyjson_mut_obj_iter *iter);

/**
 Create an iterator with an object, same as `styyjson_obj_iter_init()`.
 
 @param obj The object to be iterated over.
    If this parameter is NULL or not an object, an empty iterator will returned.
 @return A new iterator for the object.
 
 @note The iterator does not need to be destroyed.
 */
styyjson_api_inline styyjson_mut_obj_iter styyjson_mut_obj_iter_with(
    styyjson_mut_val *obj);

/**
 Returns whether the iteration has more elements.
 If `iter` is NULL, this function will return false.
 */
styyjson_api_inline bool styyjson_mut_obj_iter_has_next(
    styyjson_mut_obj_iter *iter);

/**
 Returns the next key in the iteration, or NULL on end.
 If `iter` is NULL, this function will return NULL.
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_obj_iter_next(
    styyjson_mut_obj_iter *iter);

/**
 Returns the value for key inside the iteration.
 If `iter` is NULL, this function will return NULL.
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_obj_iter_get_val(
    styyjson_mut_val *key);

/**
 Removes current key-value pair in the iteration, returns the removed value.
 If `iter` is NULL, this function will return NULL.
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_obj_iter_remove(
    styyjson_mut_obj_iter *iter);

/**
 Iterates to a specified key and returns the value.
 
 This function does the same thing as `styyjson_mut_obj_get()`, but is much faster
 if the ordering of the keys is known at compile-time and you are using the same
 order to look up the values. If the key exists in this object, then the
 iterator will stop at the next key, otherwise the iterator will not change and
 NULL is returned.
 
 @param iter The object iterator, should not be NULL.
 @param key The key, should be a UTF-8 string with null-terminator.
 @return The value to which the specified key is mapped.
    NULL if this object contains no mapping for the key or input is invalid.
 
 @warning This function takes a linear search time if the key is not nearby.
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_obj_iter_get(
    styyjson_mut_obj_iter *iter, const char *key);

/**
 Iterates to a specified key and returns the value.
 
 This function does the same thing as `styyjson_mut_obj_getn()` but is much faster
 if the ordering of the keys is known at compile-time and you are using the same
 order to look up the values. If the key exists in this object, then the
 iterator will stop at the next key, otherwise the iterator will not change and
 NULL is returned.
 
 @param iter The object iterator, should not be NULL.
 @param key The key, should be a UTF-8 string, null-terminator is not required.
 @param key_len The the length of `key`, in bytes.
 @return The value to which the specified key is mapped.
    NULL if this object contains no mapping for the key or input is invalid.
 
 @warning This function takes a linear search time if the key is not nearby.
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_obj_iter_getn(
    styyjson_mut_obj_iter *iter, const char *key, size_t key_len);

/**
 Macro for iterating over an object.
 It works like iterator, but with a more intuitive API.
 
 @warning You should not modify the object while iterating over it.
 
 @par Example
 @code
    size_t idx, max;
    styyjson_val *key, *val;
    styyjson_obj_foreach(obj, idx, max, key, val) {
        your_func(key, val);
    }
 @endcode
 */
#define styyjson_mut_obj_foreach(obj, idx, max, key, val) \
    for ((idx) = 0, \
        (max) = styyjson_mut_obj_size(obj), \
        (key) = (max) ? ((styyjson_mut_val *)(obj)->uni.ptr)->next->next : NULL, \
        (val) = (key) ? (key)->next : NULL; \
        (idx) < (max); \
        (idx)++, \
        (key) = (val)->next, \
        (val) = (key)->next)



/*==============================================================================
 * Mutable JSON Object Creation API
 *============================================================================*/

/** Creates and returns a mutable object, returns NULL on error. */
styyjson_api_inline styyjson_mut_val *styyjson_mut_obj(styyjson_mut_doc *doc);

/**
 Creates and returns a mutable object with keys and values, returns NULL on
 error. The keys and values are not copied. The strings should be a
 null-terminated UTF-8 string.
 
 @warning The input string is not copied, you should keep this string
    unmodified for the lifetime of this JSON document.
 
 @par Example
 @code
    const char *keys[2] = { "id", "name" };
    const char *vals[2] = { "01", "Harry" };
    styyjson_mut_val *obj = styyjson_mut_obj_with_str(doc, keys, vals, 2);
 @endcode
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_obj_with_str(styyjson_mut_doc *doc,
                                                          const char **keys,
                                                          const char **vals,
                                                          size_t count);

/**
 Creates and returns a mutable object with key-value pairs and pair count,
 returns NULL on error. The keys and values are not copied. The strings should
 be a null-terminated UTF-8 string.
 
 @warning The input string is not copied, you should keep this string
    unmodified for the lifetime of this JSON document.
 
 @par Example
 @code
    const char *kv_pairs[4] = { "id", "01", "name", "Harry" };
    styyjson_mut_val *obj = styyjson_mut_obj_with_kv(doc, kv_pairs, 2);
 @endcode
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_obj_with_kv(styyjson_mut_doc *doc,
                                                         const char **kv_pairs,
                                                         size_t pair_count);



/*==============================================================================
 * Mutable JSON Object Modification API
 *============================================================================*/

/**
 Adds a key-value pair at the end of the object.
 This function allows duplicated key in one object.
 @param obj The object to which the new key-value pair is to be added.
 @param key The key, should be a string which is created by `styyjson_mut_str()`,
    `styyjson_mut_strn()`, `styyjson_mut_strcpy()` or `styyjson_mut_strncpy()`.
 @param val The value to add to the object.
 @return Whether successful.
 */
styyjson_api_inline bool styyjson_mut_obj_add(styyjson_mut_val *obj,
                                          styyjson_mut_val *key,
                                          styyjson_mut_val *val);
/**
 Sets a key-value pair at the end of the object.
 This function may remove all key-value pairs for the given key before add.
 @param obj The object to which the new key-value pair is to be added.
 @param key The key, should be a string which is created by `styyjson_mut_str()`,
    `styyjson_mut_strn()`, `styyjson_mut_strcpy()` or `styyjson_mut_strncpy()`.
 @param val The value to add to the object. If this value is null, the behavior
    is same as `styyjson_mut_obj_remove()`.
 @return Whether successful.
 */
styyjson_api_inline bool styyjson_mut_obj_put(styyjson_mut_val *obj,
                                          styyjson_mut_val *key,
                                          styyjson_mut_val *val);

/**
 Inserts a key-value pair to the object at the given position.
 This function allows duplicated key in one object.
 @param obj The object to which the new key-value pair is to be added.
 @param key The key, should be a string which is created by `styyjson_mut_str()`,
    `styyjson_mut_strn()`, `styyjson_mut_strcpy()` or `styyjson_mut_strncpy()`.
 @param val The value to add to the object.
 @param idx The index to which to insert the new pair.
 @return Whether successful.
 */
styyjson_api_inline bool styyjson_mut_obj_insert(styyjson_mut_val *obj,
                                             styyjson_mut_val *key,
                                             styyjson_mut_val *val,
                                             size_t idx);

/**
 Removes all key-value pair from the object with given key.
 @param obj The object from which the key-value pair is to be removed.
 @param key The key, should be a string value.
 @return The first matched value, or NULL if no matched value.
 @warning This function takes a linear search time.
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_obj_remove(styyjson_mut_val *obj,
                                                        styyjson_mut_val *key);

/**
 Removes all key-value pair from the object with given key.
 @param obj The object from which the key-value pair is to be removed.
 @param key The key, should be a UTF-8 string with null-terminator.
 @return The first matched value, or NULL if no matched value.
 @warning This function takes a linear search time.
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_obj_remove_key(
    styyjson_mut_val *obj, const char *key);

/**
 Removes all key-value pair from the object with given key.
 @param obj The object from which the key-value pair is to be removed.
 @param key The key, should be a UTF-8 string, null-terminator is not required.
 @param key_len The length of the key.
 @return The first matched value, or NULL if no matched value.
 @warning This function takes a linear search time.
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_obj_remove_keyn(
    styyjson_mut_val *obj, const char *key, size_t key_len);

/**
 Removes all key-value pairs in this object.
 @param obj The object from which all of the values are to be removed.
 @return Whether successful.
 */
styyjson_api_inline bool styyjson_mut_obj_clear(styyjson_mut_val *obj);

/**
 Replaces value from the object with given key.
 If the key is not exist, or the value is NULL, it will fail.
 @param obj The object to which the value is to be replaced.
 @param key The key, should be a string value.
 @param val The value to replace into the object.
 @return Whether successful.
 @warning This function takes a linear search time.
 */
styyjson_api_inline bool styyjson_mut_obj_replace(styyjson_mut_val *obj,
                                              styyjson_mut_val *key,
                                              styyjson_mut_val *val);

/**
 Rotates key-value pairs in the object for the given number of times.
 For example: `{"a":1,"b":2,"c":3,"d":4}` rotate 1 is
 `{"b":2,"c":3,"d":4,"a":1}`.
 @param obj The object to be rotated.
 @param idx Index (or times) to rotate.
 @return Whether successful.
 @warning This function takes a linear search time.
 */
styyjson_api_inline bool styyjson_mut_obj_rotate(styyjson_mut_val *obj,
                                             size_t idx);



/*==============================================================================
 * Mutable JSON Object Modification Convenience API
 *============================================================================*/

/** Adds a `null` value at the end of the object.
    The `key` should be a null-terminated UTF-8 string.
    This function allows duplicated key in one object.
    
    @warning The key string is not copied, you should keep the string
        unmodified for the lifetime of this JSON document. */
styyjson_api_inline bool styyjson_mut_obj_add_null(styyjson_mut_doc *doc,
                                               styyjson_mut_val *obj,
                                               const char *key);

/** Adds a `true` value at the end of the object.
    The `key` should be a null-terminated UTF-8 string.
    This function allows duplicated key in one object.
    
    @warning The key string is not copied, you should keep the string
        unmodified for the lifetime of this JSON document. */
styyjson_api_inline bool styyjson_mut_obj_add_true(styyjson_mut_doc *doc,
                                               styyjson_mut_val *obj,
                                               const char *key);

/** Adds a `false` value at the end of the object.
    The `key` should be a null-terminated UTF-8 string.
    This function allows duplicated key in one object.
    
    @warning The key string is not copied, you should keep the string
        unmodified for the lifetime of this JSON document. */
styyjson_api_inline bool styyjson_mut_obj_add_false(styyjson_mut_doc *doc,
                                                styyjson_mut_val *obj,
                                                const char *key);

/** Adds a bool value at the end of the object.
    The `key` should be a null-terminated UTF-8 string.
    This function allows duplicated key in one object.
    
    @warning The key string is not copied, you should keep the string
        unmodified for the lifetime of this JSON document. */
styyjson_api_inline bool styyjson_mut_obj_add_bool(styyjson_mut_doc *doc,
                                               styyjson_mut_val *obj,
                                               const char *key, bool val);

/** Adds an unsigned integer value at the end of the object.
    The `key` should be a null-terminated UTF-8 string.
    This function allows duplicated key in one object.
    
    @warning The key string is not copied, you should keep the string
        unmodified for the lifetime of this JSON document. */
styyjson_api_inline bool styyjson_mut_obj_add_uint(styyjson_mut_doc *doc,
                                               styyjson_mut_val *obj,
                                               const char *key, uint64_t val);

/** Adds a signed integer value at the end of the object.
    The `key` should be a null-terminated UTF-8 string.
    This function allows duplicated key in one object.
    
    @warning The key string is not copied, you should keep the string
        unmodified for the lifetime of this JSON document. */
styyjson_api_inline bool styyjson_mut_obj_add_sint(styyjson_mut_doc *doc,
                                               styyjson_mut_val *obj,
                                               const char *key, int64_t val);

/** Adds an int value at the end of the object.
    The `key` should be a null-terminated UTF-8 string.
    This function allows duplicated key in one object.
    
    @warning The key string is not copied, you should keep the string
        unmodified for the lifetime of this JSON document. */
styyjson_api_inline bool styyjson_mut_obj_add_int(styyjson_mut_doc *doc,
                                              styyjson_mut_val *obj,
                                              const char *key, int64_t val);

/** Adds a double value at the end of the object.
    The `key` should be a null-terminated UTF-8 string.
    This function allows duplicated key in one object.
    
    @warning The key string is not copied, you should keep the string
        unmodified for the lifetime of this JSON document. */
styyjson_api_inline bool styyjson_mut_obj_add_real(styyjson_mut_doc *doc,
                                               styyjson_mut_val *obj,
                                               const char *key, double val);

/** Adds a string value at the end of the object.
    The `key` and `val` should be null-terminated UTF-8 strings.
    This function allows duplicated key in one object.
    
    @warning The key/value strings are not copied, you should keep these strings
        unmodified for the lifetime of this JSON document. */
styyjson_api_inline bool styyjson_mut_obj_add_str(styyjson_mut_doc *doc,
                                              styyjson_mut_val *obj,
                                              const char *key, const char *val);

/** Adds a string value at the end of the object.
    The `key` should be a null-terminated UTF-8 string.
    The `val` should be a UTF-8 string, null-terminator is not required.
    The `len` should be the length of the `val`, in bytes.
    This function allows duplicated key in one object.
    
    @warning The key/value strings are not copied, you should keep these strings
        unmodified for the lifetime of this JSON document. */
styyjson_api_inline bool styyjson_mut_obj_add_strn(styyjson_mut_doc *doc,
                                               styyjson_mut_val *obj,
                                               const char *key,
                                               const char *val, size_t len);

/** Adds a string value at the end of the object.
    The `key` and `val` should be null-terminated UTF-8 strings.
    The value string is copied.
    This function allows duplicated key in one object.
    
    @warning The key string is not copied, you should keep the string
        unmodified for the lifetime of this JSON document. */
styyjson_api_inline bool styyjson_mut_obj_add_strcpy(styyjson_mut_doc *doc,
                                                 styyjson_mut_val *obj,
                                                 const char *key,
                                                 const char *val);

/** Adds a string value at the end of the object.
    The `key` should be a null-terminated UTF-8 string.
    The `val` should be a UTF-8 string, null-terminator is not required.
    The `len` should be the length of the `val`, in bytes.
    This function allows duplicated key in one object.
    
    @warning The key strings are not copied, you should keep these strings
        unmodified for the lifetime of this JSON document. */
styyjson_api_inline bool styyjson_mut_obj_add_strncpy(styyjson_mut_doc *doc,
                                                  styyjson_mut_val *obj,
                                                  const char *key,
                                                  const char *val, size_t len);

/**
 Creates and adds a new array to the target object.
 The `key` should be a null-terminated UTF-8 string.
 This function allows duplicated key in one object.
 
 @warning The key string is not copied, you should keep these strings
          unmodified for the lifetime of this JSON document.
 @return The new array, or NULL on error.
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_obj_add_arr(styyjson_mut_doc *doc,
                                                         styyjson_mut_val *obj,
                                                         const char *key);

/**
 Creates and adds a new object to the target object.
 The `key` should be a null-terminated UTF-8 string.
 This function allows duplicated key in one object.
 
 @warning The key string is not copied, you should keep these strings
          unmodified for the lifetime of this JSON document.
 @return The new object, or NULL on error.
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_obj_add_obj(styyjson_mut_doc *doc,
                                                         styyjson_mut_val *obj,
                                                         const char *key);

/** Adds a JSON value at the end of the object.
    The `key` should be a null-terminated UTF-8 string.
    This function allows duplicated key in one object.
    
    @warning The key string is not copied, you should keep the string
        unmodified for the lifetime of this JSON document. */
styyjson_api_inline bool styyjson_mut_obj_add_val(styyjson_mut_doc *doc,
                                              styyjson_mut_val *obj,
                                              const char *key,
                                              styyjson_mut_val *val);

/** Removes all key-value pairs for the given key.
    Returns the first value to which the specified key is mapped or NULL if this
    object contains no mapping for the key.
    The `key` should be a null-terminated UTF-8 string.
    
    @warning This function takes a linear search time. */
styyjson_api_inline styyjson_mut_val *styyjson_mut_obj_remove_str(
    styyjson_mut_val *obj, const char *key);

/** Removes all key-value pairs for the given key.
    Returns the first value to which the specified key is mapped or NULL if this
    object contains no mapping for the key.
    The `key` should be a UTF-8 string, null-terminator is not required.
    The `len` should be the length of the key, in bytes.
    
    @warning This function takes a linear search time. */
styyjson_api_inline styyjson_mut_val *styyjson_mut_obj_remove_strn(
    styyjson_mut_val *obj, const char *key, size_t len);

/** Replaces all matching keys with the new key.
    Returns true if at least one key was renamed.
    The `key` and `new_key` should be a null-terminated UTF-8 string.
    The `new_key` is copied and held by doc.
    
    @warning This function takes a linear search time.
    If `new_key` already exists, it will cause duplicate keys.
 */
styyjson_api_inline bool styyjson_mut_obj_rename_key(styyjson_mut_doc *doc,
                                                 styyjson_mut_val *obj,
                                                 const char *key,
                                                 const char *new_key);

/** Replaces all matching keys with the new key.
    Returns true if at least one key was renamed.
    The `key` and `new_key` should be a UTF-8 string,
    null-terminator is not required. The `new_key` is copied and held by doc.
    
    @warning This function takes a linear search time.
    If `new_key` already exists, it will cause duplicate keys.
 */
styyjson_api_inline bool styyjson_mut_obj_rename_keyn(styyjson_mut_doc *doc,
                                                  styyjson_mut_val *obj,
                                                  const char *key,
                                                  size_t len,
                                                  const char *new_key,
                                                  size_t new_len);



/*==============================================================================
 * JSON Pointer API (RFC 6901)
 * https://tools.ietf.org/html/rfc6901
 *============================================================================*/

/** JSON Pointer error code. */
typedef uint32_t styyjson_ptr_code;

/** No JSON pointer error. */
static const styyjson_ptr_code YYJSON_PTR_ERR_NONE = 0;

/** Invalid input parameter, such as NULL input. */
static const styyjson_ptr_code YYJSON_PTR_ERR_PARAMETER = 1;

/** JSON pointer syntax error, such as invalid escape, token no prefix. */
static const styyjson_ptr_code YYJSON_PTR_ERR_SYNTAX = 2;

/** JSON pointer resolve failed, such as index out of range, key not found. */
static const styyjson_ptr_code YYJSON_PTR_ERR_RESOLVE = 3;

/** Document's root is NULL, but it is required for the function call. */
static const styyjson_ptr_code YYJSON_PTR_ERR_NULL_ROOT = 4;

/** Cannot set root as the target is not a document. */
static const styyjson_ptr_code YYJSON_PTR_ERR_SET_ROOT = 5;

/** The memory allocation failed and a new value could not be created. */
static const styyjson_ptr_code YYJSON_PTR_ERR_MEMORY_ALLOCATION = 6;

/** Error information for JSON pointer. */
typedef struct styyjson_ptr_err {
    /** Error code, see `styyjson_ptr_code` for all possible values. */
    styyjson_ptr_code code;
    /** Error message, constant, no need to free (NULL if no error). */
    const char *msg;
    /** Error byte position for input JSON pointer (0 if no error). */
    size_t pos;
} styyjson_ptr_err;

/**
 A context for JSON pointer operation.
 
 This struct stores the context of JSON Pointer operation result. The struct
 can be used with three helper functions: `ctx_append()`, `ctx_replace()`, and
 `ctx_remove()`, which perform the corresponding operations on the container
 without re-parsing the JSON Pointer.
 
 For example:
 @code
    // doc before: {"a":[0,1,null]}
    // ptr: "/a/2"
    val = styyjson_mut_doc_ptr_getx(doc, ptr, strlen(ptr), &ctx, &err);
    if (styyjson_is_null(val)) {
        styyjson_ptr_ctx_remove(&ctx);
    }
    // doc after: {"a":[0,1]}
 @endcode
 */
typedef struct styyjson_ptr_ctx {
    /**
     The container (parent) of the target value. It can be either an array or
     an object. If the target location has no value, but all its parent
     containers exist, and the target location can be used to insert a new
     value, then `ctn` is the parent container of the target location.
     Otherwise, `ctn` is NULL.
     */
    styyjson_mut_val *ctn;
    /**
     The previous sibling of the target value. It can be either a value in an
     array or a key in an object. As the container is a `circular linked list`
     of elements, `pre` is the previous node of the target value. If the
     operation is `add` or `set`, then `pre` is the previous node of the new
     value, not the original target value. If the target value does not exist,
     `pre` is NULL.
     */
    styyjson_mut_val *pre;
    /**
     The removed value if the operation is `set`, `replace` or `remove`. It can
     be used to restore the original state of the document if needed.
     */
    styyjson_mut_val *old;
} styyjson_ptr_ctx;

/**
 Get value by a JSON Pointer.
 @param doc The JSON document to be queried.
 @param ptr The JSON pointer string (UTF-8 with null-terminator).
 @return The value referenced by the JSON pointer.
    NULL if `doc` or `ptr` is NULL, or the JSON pointer cannot be resolved.
 */
styyjson_api_inline styyjson_val *styyjson_doc_ptr_get(styyjson_doc *doc,
                                                 const char *ptr);

/**
 Get value by a JSON Pointer.
 @param doc The JSON document to be queried.
 @param ptr The JSON pointer string (UTF-8, null-terminator is not required).
 @param len The length of `ptr` in bytes.
 @return The value referenced by the JSON pointer.
    NULL if `doc` or `ptr` is NULL, or the JSON pointer cannot be resolved.
 */
styyjson_api_inline styyjson_val *styyjson_doc_ptr_getn(styyjson_doc *doc,
                                                  const char *ptr, size_t len);

/**
 Get value by a JSON Pointer.
 @param doc The JSON document to be queried.
 @param ptr The JSON pointer string (UTF-8, null-terminator is not required).
 @param len The length of `ptr` in bytes.
 @param err A pointer to store the error information, or NULL if not needed.
 @return The value referenced by the JSON pointer.
    NULL if `doc` or `ptr` is NULL, or the JSON pointer cannot be resolved.
 */
styyjson_api_inline styyjson_val *styyjson_doc_ptr_getx(styyjson_doc *doc,
                                                  const char *ptr, size_t len,
                                                  styyjson_ptr_err *err);

/**
 Get value by a JSON Pointer.
 @param val The JSON value to be queried.
 @param ptr The JSON pointer string (UTF-8 with null-terminator).
 @return The value referenced by the JSON pointer.
    NULL if `val` or `ptr` is NULL, or the JSON pointer cannot be resolved.
 */
styyjson_api_inline styyjson_val *styyjson_ptr_get(styyjson_val *val,
                                             const char *ptr);

/**
 Get value by a JSON Pointer.
 @param val The JSON value to be queried.
 @param ptr The JSON pointer string (UTF-8, null-terminator is not required).
 @param len The length of `ptr` in bytes.
 @return The value referenced by the JSON pointer.
    NULL if `val` or `ptr` is NULL, or the JSON pointer cannot be resolved.
 */
styyjson_api_inline styyjson_val *styyjson_ptr_getn(styyjson_val *val,
                                              const char *ptr, size_t len);

/**
 Get value by a JSON Pointer.
 @param val The JSON value to be queried.
 @param ptr The JSON pointer string (UTF-8, null-terminator is not required).
 @param len The length of `ptr` in bytes.
 @param err A pointer to store the error information, or NULL if not needed.
 @return The value referenced by the JSON pointer.
    NULL if `val` or `ptr` is NULL, or the JSON pointer cannot be resolved.
 */
styyjson_api_inline styyjson_val *styyjson_ptr_getx(styyjson_val *val,
                                              const char *ptr, size_t len,
                                              styyjson_ptr_err *err);

/**
 Get value by a JSON Pointer.
 @param doc The JSON document to be queried.
 @param ptr The JSON pointer string (UTF-8 with null-terminator).
 @return The value referenced by the JSON pointer.
    NULL if `doc` or `ptr` is NULL, or the JSON pointer cannot be resolved.
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_doc_ptr_get(styyjson_mut_doc *doc,
                                                         const char *ptr);

/**
 Get value by a JSON Pointer.
 @param doc The JSON document to be queried.
 @param ptr The JSON pointer string (UTF-8, null-terminator is not required).
 @param len The length of `ptr` in bytes.
 @return The value referenced by the JSON pointer.
    NULL if `doc` or `ptr` is NULL, or the JSON pointer cannot be resolved.
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_doc_ptr_getn(styyjson_mut_doc *doc,
                                                          const char *ptr,
                                                          size_t len);

/**
 Get value by a JSON Pointer.
 @param doc The JSON document to be queried.
 @param ptr The JSON pointer string (UTF-8, null-terminator is not required).
 @param len The length of `ptr` in bytes.
 @param ctx A pointer to store the result context, or NULL if not needed.
 @param err A pointer to store the error information, or NULL if not needed.
 @return The value referenced by the JSON pointer.
    NULL if `doc` or `ptr` is NULL, or the JSON pointer cannot be resolved.
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_doc_ptr_getx(styyjson_mut_doc *doc,
                                                          const char *ptr,
                                                          size_t len,
                                                          styyjson_ptr_ctx *ctx,
                                                          styyjson_ptr_err *err);

/**
 Get value by a JSON Pointer.
 @param val The JSON value to be queried.
 @param ptr The JSON pointer string (UTF-8 with null-terminator).
 @return The value referenced by the JSON pointer.
    NULL if `val` or `ptr` is NULL, or the JSON pointer cannot be resolved.
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_ptr_get(styyjson_mut_val *val,
                                                     const char *ptr);

/**
 Get value by a JSON Pointer.
 @param val The JSON value to be queried.
 @param ptr The JSON pointer string (UTF-8, null-terminator is not required).
 @param len The length of `ptr` in bytes.
 @return The value referenced by the JSON pointer.
    NULL if `val` or `ptr` is NULL, or the JSON pointer cannot be resolved.
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_ptr_getn(styyjson_mut_val *val,
                                                      const char *ptr,
                                                      size_t len);

/**
 Get value by a JSON Pointer.
 @param val The JSON value to be queried.
 @param ptr The JSON pointer string (UTF-8, null-terminator is not required).
 @param len The length of `ptr` in bytes.
 @param ctx A pointer to store the result context, or NULL if not needed.
 @param err A pointer to store the error information, or NULL if not needed.
 @return The value referenced by the JSON pointer.
    NULL if `val` or `ptr` is NULL, or the JSON pointer cannot be resolved.
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_ptr_getx(styyjson_mut_val *val,
                                                      const char *ptr,
                                                      size_t len,
                                                      styyjson_ptr_ctx *ctx,
                                                      styyjson_ptr_err *err);

/**
 Add (insert) value by a JSON pointer.
 @param doc The target JSON document.
 @param ptr The JSON pointer string (UTF-8 with null-terminator).
 @param new_val The value to be added.
 @return true if JSON pointer is valid and new value is added, false otherwise.
 @note The parent nodes will be created if they do not exist.
 */
styyjson_api_inline bool styyjson_mut_doc_ptr_add(styyjson_mut_doc *doc,
                                              const char *ptr,
                                              styyjson_mut_val *new_val);

/**
 Add (insert) value by a JSON pointer.
 @param doc The target JSON document.
 @param ptr The JSON pointer string (UTF-8, null-terminator is not required).
 @param len The length of `ptr` in bytes.
 @param new_val The value to be added.
 @return true if JSON pointer is valid and new value is added, false otherwise.
 @note The parent nodes will be created if they do not exist.
 */
styyjson_api_inline bool styyjson_mut_doc_ptr_addn(styyjson_mut_doc *doc,
                                               const char *ptr, size_t len,
                                               styyjson_mut_val *new_val);

/**
 Add (insert) value by a JSON pointer.
 @param doc The target JSON document.
 @param ptr The JSON pointer string (UTF-8, null-terminator is not required).
 @param len The length of `ptr` in bytes.
 @param new_val The value to be added.
 @param create_parent Whether to create parent nodes if not exist.
 @param ctx A pointer to store the result context, or NULL if not needed.
 @param err A pointer to store the error information, or NULL if not needed.
 @return true if JSON pointer is valid and new value is added, false otherwise.
 */
styyjson_api_inline bool styyjson_mut_doc_ptr_addx(styyjson_mut_doc *doc,
                                               const char *ptr, size_t len,
                                               styyjson_mut_val *new_val,
                                               bool create_parent,
                                               styyjson_ptr_ctx *ctx,
                                               styyjson_ptr_err *err);

/**
 Add (insert) value by a JSON pointer.
 @param val The target JSON value.
 @param ptr The JSON pointer string (UTF-8 with null-terminator).
 @param doc Only used to create new values when needed.
 @param new_val The value to be added.
 @return true if JSON pointer is valid and new value is added, false otherwise.
 @note The parent nodes will be created if they do not exist.
 */
styyjson_api_inline bool styyjson_mut_ptr_add(styyjson_mut_val *val,
                                          const char *ptr,
                                          styyjson_mut_val *new_val,
                                          styyjson_mut_doc *doc);

/**
 Add (insert) value by a JSON pointer.
 @param val The target JSON value.
 @param ptr The JSON pointer string (UTF-8, null-terminator is not required).
 @param len The length of `ptr` in bytes.
 @param doc Only used to create new values when needed.
 @param new_val The value to be added.
 @return true if JSON pointer is valid and new value is added, false otherwise.
 @note The parent nodes will be created if they do not exist.
 */
styyjson_api_inline bool styyjson_mut_ptr_addn(styyjson_mut_val *val,
                                           const char *ptr, size_t len,
                                           styyjson_mut_val *new_val,
                                           styyjson_mut_doc *doc);

/**
 Add (insert) value by a JSON pointer.
 @param val The target JSON value.
 @param ptr The JSON pointer string (UTF-8, null-terminator is not required).
 @param len The length of `ptr` in bytes.
 @param doc Only used to create new values when needed.
 @param new_val The value to be added.
 @param create_parent Whether to create parent nodes if not exist.
 @param ctx A pointer to store the result context, or NULL if not needed.
 @param err A pointer to store the error information, or NULL if not needed.
 @return true if JSON pointer is valid and new value is added, false otherwise.
 */
styyjson_api_inline bool styyjson_mut_ptr_addx(styyjson_mut_val *val,
                                           const char *ptr, size_t len,
                                           styyjson_mut_val *new_val,
                                           styyjson_mut_doc *doc,
                                           bool create_parent,
                                           styyjson_ptr_ctx *ctx,
                                           styyjson_ptr_err *err);

/**
 Set value by a JSON pointer.
 @param doc The target JSON document.
 @param ptr The JSON pointer string (UTF-8 with null-terminator).
 @param new_val The value to be set, pass NULL to remove.
 @return true if JSON pointer is valid and new value is set, false otherwise.
 @note The parent nodes will be created if they do not exist.
    If the target value already exists, it will be replaced by the new value.
 */
styyjson_api_inline bool styyjson_mut_doc_ptr_set(styyjson_mut_doc *doc,
                                              const char *ptr,
                                              styyjson_mut_val *new_val);

/**
 Set value by a JSON pointer.
 @param doc The target JSON document.
 @param ptr The JSON pointer string (UTF-8, null-terminator is not required).
 @param len The length of `ptr` in bytes.
 @param new_val The value to be set, pass NULL to remove.
 @return true if JSON pointer is valid and new value is set, false otherwise.
 @note The parent nodes will be created if they do not exist.
    If the target value already exists, it will be replaced by the new value.
 */
styyjson_api_inline bool styyjson_mut_doc_ptr_setn(styyjson_mut_doc *doc,
                                               const char *ptr, size_t len,
                                               styyjson_mut_val *new_val);

/**
 Set value by a JSON pointer.
 @param doc The target JSON document.
 @param ptr The JSON pointer string (UTF-8, null-terminator is not required).
 @param len The length of `ptr` in bytes.
 @param new_val The value to be set, pass NULL to remove.
 @param create_parent Whether to create parent nodes if not exist.
 @param ctx A pointer to store the result context, or NULL if not needed.
 @param err A pointer to store the error information, or NULL if not needed.
 @return true if JSON pointer is valid and new value is set, false otherwise.
 @note If the target value already exists, it will be replaced by the new value.
 */
styyjson_api_inline bool styyjson_mut_doc_ptr_setx(styyjson_mut_doc *doc,
                                               const char *ptr, size_t len,
                                               styyjson_mut_val *new_val,
                                               bool create_parent,
                                               styyjson_ptr_ctx *ctx,
                                               styyjson_ptr_err *err);

/**
 Set value by a JSON pointer.
 @param val The target JSON value.
 @param ptr The JSON pointer string (UTF-8 with null-terminator).
 @param new_val The value to be set, pass NULL to remove.
 @param doc Only used to create new values when needed.
 @return true if JSON pointer is valid and new value is set, false otherwise.
 @note The parent nodes will be created if they do not exist.
    If the target value already exists, it will be replaced by the new value.
 */
styyjson_api_inline bool styyjson_mut_ptr_set(styyjson_mut_val *val,
                                          const char *ptr,
                                          styyjson_mut_val *new_val,
                                          styyjson_mut_doc *doc);

/**
 Set value by a JSON pointer.
 @param val The target JSON value.
 @param ptr The JSON pointer string (UTF-8, null-terminator is not required).
 @param len The length of `ptr` in bytes.
 @param new_val The value to be set, pass NULL to remove.
 @param doc Only used to create new values when needed.
 @return true if JSON pointer is valid and new value is set, false otherwise.
 @note The parent nodes will be created if they do not exist.
    If the target value already exists, it will be replaced by the new value.
 */
styyjson_api_inline bool styyjson_mut_ptr_setn(styyjson_mut_val *val,
                                           const char *ptr, size_t len,
                                           styyjson_mut_val *new_val,
                                           styyjson_mut_doc *doc);

/**
 Set value by a JSON pointer.
 @param val The target JSON value.
 @param ptr The JSON pointer string (UTF-8, null-terminator is not required).
 @param len The length of `ptr` in bytes.
 @param new_val The value to be set, pass NULL to remove.
 @param doc Only used to create new values when needed.
 @param create_parent Whether to create parent nodes if not exist.
 @param ctx A pointer to store the result context, or NULL if not needed.
 @param err A pointer to store the error information, or NULL if not needed.
 @return true if JSON pointer is valid and new value is set, false otherwise.
 @note If the target value already exists, it will be replaced by the new value.
 */
styyjson_api_inline bool styyjson_mut_ptr_setx(styyjson_mut_val *val,
                                           const char *ptr, size_t len,
                                           styyjson_mut_val *new_val,
                                           styyjson_mut_doc *doc,
                                           bool create_parent,
                                           styyjson_ptr_ctx *ctx,
                                           styyjson_ptr_err *err);

/**
 Replace value by a JSON pointer.
 @param doc The target JSON document.
 @param ptr The JSON pointer string (UTF-8 with null-terminator).
 @param new_val The new value to replace the old one.
 @return The old value that was replaced, or NULL if not found.
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_doc_ptr_replace(
    styyjson_mut_doc *doc, const char *ptr, styyjson_mut_val *new_val);

/**
 Replace value by a JSON pointer.
 @param doc The target JSON document.
 @param ptr The JSON pointer string (UTF-8, null-terminator is not required).
 @param len The length of `ptr` in bytes.
 @param new_val The new value to replace the old one.
 @return The old value that was replaced, or NULL if not found.
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_doc_ptr_replacen(
    styyjson_mut_doc *doc, const char *ptr, size_t len, styyjson_mut_val *new_val);

/**
 Replace value by a JSON pointer.
 @param doc The target JSON document.
 @param ptr The JSON pointer string (UTF-8, null-terminator is not required).
 @param len The length of `ptr` in bytes.
 @param new_val The new value to replace the old one.
 @param ctx A pointer to store the result context, or NULL if not needed.
 @param err A pointer to store the error information, or NULL if not needed.
 @return The old value that was replaced, or NULL if not found.
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_doc_ptr_replacex(
    styyjson_mut_doc *doc, const char *ptr, size_t len, styyjson_mut_val *new_val,
    styyjson_ptr_ctx *ctx, styyjson_ptr_err *err);

/**
 Replace value by a JSON pointer.
 @param val The target JSON value.
 @param ptr The JSON pointer string (UTF-8 with null-terminator).
 @param new_val The new value to replace the old one.
 @return The old value that was replaced, or NULL if not found.
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_ptr_replace(
    styyjson_mut_val *val, const char *ptr, styyjson_mut_val *new_val);

/**
 Replace value by a JSON pointer.
 @param val The target JSON value.
 @param ptr The JSON pointer string (UTF-8, null-terminator is not required).
 @param len The length of `ptr` in bytes.
 @param new_val The new value to replace the old one.
 @return The old value that was replaced, or NULL if not found.
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_ptr_replacen(
    styyjson_mut_val *val, const char *ptr, size_t len, styyjson_mut_val *new_val);

/**
 Replace value by a JSON pointer.
 @param val The target JSON value.
 @param ptr The JSON pointer string (UTF-8, null-terminator is not required).
 @param len The length of `ptr` in bytes.
 @param new_val The new value to replace the old one.
 @param ctx A pointer to store the result context, or NULL if not needed.
 @param err A pointer to store the error information, or NULL if not needed.
 @return The old value that was replaced, or NULL if not found.
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_ptr_replacex(
    styyjson_mut_val *val, const char *ptr, size_t len, styyjson_mut_val *new_val,
    styyjson_ptr_ctx *ctx, styyjson_ptr_err *err);

/**
 Remove value by a JSON pointer.
 @param doc The target JSON document.
 @param ptr The JSON pointer string (UTF-8 with null-terminator).
 @return The removed value, or NULL on error.
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_doc_ptr_remove(
    styyjson_mut_doc *doc, const char *ptr);

/**
 Remove value by a JSON pointer.
 @param doc The target JSON document.
 @param ptr The JSON pointer string (UTF-8, null-terminator is not required).
 @param len The length of `ptr` in bytes.
 @return The removed value, or NULL on error.
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_doc_ptr_removen(
    styyjson_mut_doc *doc, const char *ptr, size_t len);

/**
 Remove value by a JSON pointer.
 @param doc The target JSON document.
 @param ptr The JSON pointer string (UTF-8, null-terminator is not required).
 @param len The length of `ptr` in bytes.
 @param ctx A pointer to store the result context, or NULL if not needed.
 @param err A pointer to store the error information, or NULL if not needed.
 @return The removed value, or NULL on error.
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_doc_ptr_removex(
    styyjson_mut_doc *doc, const char *ptr, size_t len,
    styyjson_ptr_ctx *ctx, styyjson_ptr_err *err);

/**
 Remove value by a JSON pointer.
 @param val The target JSON value.
 @param ptr The JSON pointer string (UTF-8 with null-terminator).
 @return The removed value, or NULL on error.
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_ptr_remove(styyjson_mut_val *val,
                                                        const char *ptr);

/**
 Remove value by a JSON pointer.
 @param val The target JSON value.
 @param ptr The JSON pointer string (UTF-8, null-terminator is not required).
 @param len The length of `ptr` in bytes.
 @return The removed value, or NULL on error.
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_ptr_removen(styyjson_mut_val *val,
                                                         const char *ptr,
                                                         size_t len);

/**
 Remove value by a JSON pointer.
 @param val The target JSON value.
 @param ptr The JSON pointer string (UTF-8, null-terminator is not required).
 @param len The length of `ptr` in bytes.
 @param ctx A pointer to store the result context, or NULL if not needed.
 @param err A pointer to store the error information, or NULL if not needed.
 @return The removed value, or NULL on error.
 */
styyjson_api_inline styyjson_mut_val *styyjson_mut_ptr_removex(styyjson_mut_val *val,
                                                         const char *ptr,
                                                         size_t len,
                                                         styyjson_ptr_ctx *ctx,
                                                         styyjson_ptr_err *err);

/**
 Append value by JSON pointer context.
 @param ctx The context from the `styyjson_mut_ptr_xxx()` calls.
 @param key New key if `ctx->ctn` is object, or NULL if `ctx->ctn` is array.
 @param val New value to be added.
 @return true on success or false on fail.
 */
styyjson_api_inline bool styyjson_ptr_ctx_append(styyjson_ptr_ctx *ctx,
                                             styyjson_mut_val *key,
                                             styyjson_mut_val *val);

/**
 Replace value by JSON pointer context.
 @param ctx The context from the `styyjson_mut_ptr_xxx()` calls.
 @param val New value to be replaced.
 @return true on success or false on fail.
 @note If success, the old value will be returned via `ctx->old`.
 */
styyjson_api_inline bool styyjson_ptr_ctx_replace(styyjson_ptr_ctx *ctx,
                                              styyjson_mut_val *val);

/**
 Remove value by JSON pointer context.
 @param ctx The context from the `styyjson_mut_ptr_xxx()` calls.
 @return true on success or false on fail.
 @note If success, the old value will be returned via `ctx->old`.
 */
styyjson_api_inline bool styyjson_ptr_ctx_remove(styyjson_ptr_ctx *ctx);



/*==============================================================================
 * JSON Patch API (RFC 6902)
 * https://tools.ietf.org/html/rfc6902
 *============================================================================*/

/** Result code for JSON patch. */
typedef uint32_t styyjson_patch_code;

/** Success, no error. */
static const styyjson_patch_code YYJSON_PATCH_SUCCESS = 0;

/** Invalid parameter, such as NULL input or non-array patch. */
static const styyjson_patch_code YYJSON_PATCH_ERROR_INVALID_PARAMETER = 1;

/** Memory allocation failure occurs. */
static const styyjson_patch_code YYJSON_PATCH_ERROR_MEMORY_ALLOCATION = 2;

/** JSON patch operation is not object type. */
static const styyjson_patch_code YYJSON_PATCH_ERROR_INVALID_OPERATION = 3;

/** JSON patch operation is missing a required key. */
static const styyjson_patch_code YYJSON_PATCH_ERROR_MISSING_KEY = 4;

/** JSON patch operation member is invalid. */
static const styyjson_patch_code YYJSON_PATCH_ERROR_INVALID_MEMBER = 5;

/** JSON patch operation `test` not equal. */
static const styyjson_patch_code YYJSON_PATCH_ERROR_EQUAL = 6;

/** JSON patch operation failed on JSON pointer. */
static const styyjson_patch_code YYJSON_PATCH_ERROR_POINTER = 7;

/** Error information for JSON patch. */
typedef struct styyjson_patch_err {
    /** Error code, see `styyjson_patch_code` for all possible values. */
    styyjson_patch_code code;
    /** Index of the error operation (0 if no error). */
    size_t idx;
    /** Error message, constant, no need to free (NULL if no error). */
    const char *msg;
    /** JSON pointer error if `code == YYJSON_PATCH_ERROR_POINTER`. */
    styyjson_ptr_err ptr;
} styyjson_patch_err;

/**
 Creates and returns a patched JSON value (RFC 6902).
 The memory of the returned value is allocated by the `doc`.
 The `err` is used to receive error information, pass NULL if not needed.
 Returns NULL if the patch could not be applied.
 */
styyjson_api styyjson_mut_val *styyjson_patch(styyjson_mut_doc *doc,
                                        styyjson_val *orig,
                                        styyjson_val *patch,
                                        styyjson_patch_err *err);

/**
 Creates and returns a patched JSON value (RFC 6902).
 The memory of the returned value is allocated by the `doc`.
 The `err` is used to receive error information, pass NULL if not needed.
 Returns NULL if the patch could not be applied.
 */
styyjson_api styyjson_mut_val *styyjson_mut_patch(styyjson_mut_doc *doc,
                                            styyjson_mut_val *orig,
                                            styyjson_mut_val *patch,
                                            styyjson_patch_err *err);



/*==============================================================================
 * JSON Merge-Patch API (RFC 7386)
 * https://tools.ietf.org/html/rfc7386
 *============================================================================*/

/**
 Creates and returns a merge-patched JSON value (RFC 7386).
 The memory of the returned value is allocated by the `doc`.
 Returns NULL if the patch could not be applied.

 @warning This function is recursive and may cause a stack overflow if the
    object level is too deep.
 */
styyjson_api styyjson_mut_val *styyjson_merge_patch(styyjson_mut_doc *doc,
                                              styyjson_val *orig,
                                              styyjson_val *patch);

/**
 Creates and returns a merge-patched JSON value (RFC 7386).
 The memory of the returned value is allocated by the `doc`.
 Returns NULL if the patch could not be applied.

 @warning This function is recursive and may cause a stack overflow if the
    object level is too deep.
 */
styyjson_api styyjson_mut_val *styyjson_mut_merge_patch(styyjson_mut_doc *doc,
                                                  styyjson_mut_val *orig,
                                                  styyjson_mut_val *patch);



/*==============================================================================
 * JSON Structure (Implementation)
 *============================================================================*/

/** Payload of a JSON value (8 bytes). */
typedef union styyjson_val_uni {
    uint64_t    u64;
    int64_t     i64;
    double      f64;
    const char *str;
    void       *ptr;
    size_t      ofs;
} styyjson_val_uni;

/**
 Immutable JSON value, 16 bytes.
 */
struct styyjson_val {
    uint64_t tag; /**< type, subtype and length */
    styyjson_val_uni uni; /**< payload */
};

struct styyjson_doc {
    /** Root value of the document (nonnull). */
    styyjson_val *root;
    /** Allocator used by document (nonnull). */
    styyjson_alc alc;
    /** The total number of bytes read when parsing JSON (nonzero). */
    size_t dat_read;
    /** The total number of value read when parsing JSON (nonzero). */
    size_t val_read;
    /** The string pool used by JSON values (nullable). */
    char *str_pool;
};



/*==============================================================================
 * Unsafe JSON Value API (Implementation)
 *============================================================================*/

/*
 Whether the string does not need to be escaped for serialization.
 This function is used to optimize the writing speed of small constant strings.
 This function works only if the compiler can evaluate it at compile time.
 
 Clang supports it since v8.0,
    earlier versions do not support constant_p(strlen) and return false.
 GCC supports it since at least v4.4,
    earlier versions may compile it as run-time instructions.
 ICC supports it since at least v16,
    earlier versions are uncertain.
 
 @param str The C string.
 @param len The returnd value from strlen(str).
 */
styyjson_api_inline bool unsafe_styyjson_is_str_noesc(const char *str, size_t len) {
#if YYJSON_HAS_CONSTANT_P && \
    (!YYJSON_IS_REAL_GCC || styyjson_gcc_available(4, 4, 0))
    if (styyjson_constant_p(len) && len <= 32) {
        /*
         Same as the following loop:
         
         for (size_t i = 0; i < len; i++) {
             char c = str[i];
             if (c < ' ' || c > '~' || c == '"' || c == '\\') return false;
         }
         
         GCC evaluates it at compile time only if the string length is within 17
         and -O3 (which turns on the -fpeel-loops flag) is used.
         So the loop is unrolled for GCC.
         */
#       define styyjson_repeat32_incr(x) \
            x(0)  x(1)  x(2)  x(3)  x(4)  x(5)  x(6)  x(7)  \
            x(8)  x(9)  x(10) x(11) x(12) x(13) x(14) x(15) \
            x(16) x(17) x(18) x(19) x(20) x(21) x(22) x(23) \
            x(24) x(25) x(26) x(27) x(28) x(29) x(30) x(31)
#       define styyjson_check_char_noesc(i) \
            if (i < len) { \
                char c = str[i]; \
                if (c < ' ' || c > '~' || c == '"' || c == '\\') return false; }
        styyjson_repeat32_incr(styyjson_check_char_noesc)
#       undef styyjson_repeat32_incr
#       undef styyjson_check_char_noesc
        return true;
    }
#else
    (void)str;
    (void)len;
#endif
    return false;
}

styyjson_api_inline styyjson_type unsafe_styyjson_get_type(void *val) {
    uint8_t tag = (uint8_t)((styyjson_val *)val)->tag;
    return (styyjson_type)(tag & YYJSON_TYPE_MASK);
}

styyjson_api_inline styyjson_subtype unsafe_styyjson_get_subtype(void *val) {
    uint8_t tag = (uint8_t)((styyjson_val *)val)->tag;
    return (styyjson_subtype)(tag & YYJSON_SUBTYPE_MASK);
}

styyjson_api_inline uint8_t unsafe_styyjson_get_tag(void *val) {
    uint8_t tag = (uint8_t)((styyjson_val *)val)->tag;
    return (uint8_t)(tag & YYJSON_TAG_MASK);
}

styyjson_api_inline bool unsafe_styyjson_is_raw(void *val) {
    return unsafe_styyjson_get_type(val) == YYJSON_TYPE_RAW;
}

styyjson_api_inline bool unsafe_styyjson_is_null(void *val) {
    return unsafe_styyjson_get_type(val) == YYJSON_TYPE_NULL;
}

styyjson_api_inline bool unsafe_styyjson_is_bool(void *val) {
    return unsafe_styyjson_get_type(val) == YYJSON_TYPE_BOOL;
}

styyjson_api_inline bool unsafe_styyjson_is_num(void *val) {
    return unsafe_styyjson_get_type(val) == YYJSON_TYPE_NUM;
}

styyjson_api_inline bool unsafe_styyjson_is_str(void *val) {
    return unsafe_styyjson_get_type(val) == YYJSON_TYPE_STR;
}

styyjson_api_inline bool unsafe_styyjson_is_arr(void *val) {
    return unsafe_styyjson_get_type(val) == YYJSON_TYPE_ARR;
}

styyjson_api_inline bool unsafe_styyjson_is_obj(void *val) {
    return unsafe_styyjson_get_type(val) == YYJSON_TYPE_OBJ;
}

styyjson_api_inline bool unsafe_styyjson_is_ctn(void *val) {
    uint8_t mask = YYJSON_TYPE_ARR & YYJSON_TYPE_OBJ;
    return (unsafe_styyjson_get_tag(val) & mask) == mask;
}

styyjson_api_inline bool unsafe_styyjson_is_uint(void *val) {
    const uint8_t patt = YYJSON_TYPE_NUM | YYJSON_SUBTYPE_UINT;
    return unsafe_styyjson_get_tag(val) == patt;
}

styyjson_api_inline bool unsafe_styyjson_is_sint(void *val) {
    const uint8_t patt = YYJSON_TYPE_NUM | YYJSON_SUBTYPE_SINT;
    return unsafe_styyjson_get_tag(val) == patt;
}

styyjson_api_inline bool unsafe_styyjson_is_int(void *val) {
    const uint8_t mask = YYJSON_TAG_MASK & (~YYJSON_SUBTYPE_SINT);
    const uint8_t patt = YYJSON_TYPE_NUM | YYJSON_SUBTYPE_UINT;
    return (unsafe_styyjson_get_tag(val) & mask) == patt;
}

styyjson_api_inline bool unsafe_styyjson_is_real(void *val) {
    const uint8_t patt = YYJSON_TYPE_NUM | YYJSON_SUBTYPE_REAL;
    return unsafe_styyjson_get_tag(val) == patt;
}

styyjson_api_inline bool unsafe_styyjson_is_true(void *val) {
    const uint8_t patt = YYJSON_TYPE_BOOL | YYJSON_SUBTYPE_TRUE;
    return unsafe_styyjson_get_tag(val) == patt;
}

styyjson_api_inline bool unsafe_styyjson_is_false(void *val) {
    const uint8_t patt = YYJSON_TYPE_BOOL | YYJSON_SUBTYPE_FALSE;
    return unsafe_styyjson_get_tag(val) == patt;
}

styyjson_api_inline bool unsafe_styyjson_arr_is_flat(styyjson_val *val) {
    size_t ofs = val->uni.ofs;
    size_t len = (size_t)(val->tag >> YYJSON_TAG_BIT);
    return len * sizeof(styyjson_val) + sizeof(styyjson_val) == ofs;
}

styyjson_api_inline const char *unsafe_styyjson_get_raw(void *val) {
    return ((styyjson_val *)val)->uni.str;
}

styyjson_api_inline bool unsafe_styyjson_get_bool(void *val) {
    uint8_t tag = unsafe_styyjson_get_tag(val);
    return (bool)((tag & YYJSON_SUBTYPE_MASK) >> YYJSON_TYPE_BIT);
}

styyjson_api_inline uint64_t unsafe_styyjson_get_uint(void *val) {
    return ((styyjson_val *)val)->uni.u64;
}

styyjson_api_inline int64_t unsafe_styyjson_get_sint(void *val) {
    return ((styyjson_val *)val)->uni.i64;
}

styyjson_api_inline int unsafe_styyjson_get_int(void *val) {
    return (int)((styyjson_val *)val)->uni.i64;
}

styyjson_api_inline double unsafe_styyjson_get_real(void *val) {
    return ((styyjson_val *)val)->uni.f64;
}

styyjson_api_inline double unsafe_styyjson_get_num(void *val) {
    uint8_t tag = unsafe_styyjson_get_tag(val);
    if (tag == (YYJSON_TYPE_NUM | YYJSON_SUBTYPE_REAL)) {
        return ((styyjson_val *)val)->uni.f64;
    } else if (tag == (YYJSON_TYPE_NUM | YYJSON_SUBTYPE_SINT)) {
        return (double)((styyjson_val *)val)->uni.i64;
    } else if (tag == (YYJSON_TYPE_NUM | YYJSON_SUBTYPE_UINT)) {
#if YYJSON_U64_TO_F64_NO_IMPL
        uint64_t msb = ((uint64_t)1) << 63;
        uint64_t num = ((styyjson_val *)val)->uni.u64;
        if ((num & msb) == 0) {
            return (double)(int64_t)num;
        } else {
            return ((double)(int64_t)((num >> 1) | (num & 1))) * (double)2.0;
        }
#else
        return (double)((styyjson_val *)val)->uni.u64;
#endif
    }
    return 0.0;
}

styyjson_api_inline const char *unsafe_styyjson_get_str(void *val) {
    return ((styyjson_val *)val)->uni.str;
}

styyjson_api_inline size_t unsafe_styyjson_get_len(void *val) {
    return (size_t)(((styyjson_val *)val)->tag >> YYJSON_TAG_BIT);
}

styyjson_api_inline styyjson_val *unsafe_styyjson_get_first(styyjson_val *ctn) {
    return ctn + 1;
}

styyjson_api_inline styyjson_val *unsafe_styyjson_get_next(styyjson_val *val) {
    bool is_ctn = unsafe_styyjson_is_ctn(val);
    size_t ctn_ofs = val->uni.ofs;
    size_t ofs = (is_ctn ? ctn_ofs : sizeof(styyjson_val));
    return (styyjson_val *)(void *)((uint8_t *)val + ofs);
}

styyjson_api_inline bool unsafe_styyjson_equals_strn(void *val, const char *str,
                                                 size_t len) {
    return unsafe_styyjson_get_len(val) == len &&
           memcmp(((styyjson_val *)val)->uni.str, str, len) == 0;
}

styyjson_api_inline bool unsafe_styyjson_equals_str(void *val, const char *str) {
    return unsafe_styyjson_equals_strn(val, str, strlen(str));
}

styyjson_api_inline void unsafe_styyjson_set_type(void *val, styyjson_type type,
                                              styyjson_subtype subtype) {
    uint8_t tag = (type | subtype);
    uint64_t new_tag = ((styyjson_val *)val)->tag;
    new_tag = (new_tag & (~(uint64_t)YYJSON_TAG_MASK)) | (uint64_t)tag;
    ((styyjson_val *)val)->tag = new_tag;
}

styyjson_api_inline void unsafe_styyjson_set_len(void *val, size_t len) {
    uint64_t tag = ((styyjson_val *)val)->tag & YYJSON_TAG_MASK;
    tag |= (uint64_t)len << YYJSON_TAG_BIT;
    ((styyjson_val *)val)->tag = tag;
}

styyjson_api_inline void unsafe_styyjson_inc_len(void *val) {
    uint64_t tag = ((styyjson_val *)val)->tag;
    tag += (uint64_t)(1 << YYJSON_TAG_BIT);
    ((styyjson_val *)val)->tag = tag;
}

styyjson_api_inline void unsafe_styyjson_set_raw(void *val, const char *raw,
                                             size_t len) {
    unsafe_styyjson_set_type(val, YYJSON_TYPE_RAW, YYJSON_SUBTYPE_NONE);
    unsafe_styyjson_set_len(val, len);
    ((styyjson_val *)val)->uni.str = raw;
}

styyjson_api_inline void unsafe_styyjson_set_null(void *val) {
    unsafe_styyjson_set_type(val, YYJSON_TYPE_NULL, YYJSON_SUBTYPE_NONE);
    unsafe_styyjson_set_len(val, 0);
}

styyjson_api_inline void unsafe_styyjson_set_bool(void *val, bool num) {
    styyjson_subtype subtype = num ? YYJSON_SUBTYPE_TRUE : YYJSON_SUBTYPE_FALSE;
    unsafe_styyjson_set_type(val, YYJSON_TYPE_BOOL, subtype);
    unsafe_styyjson_set_len(val, 0);
}

styyjson_api_inline void unsafe_styyjson_set_uint(void *val, uint64_t num) {
    unsafe_styyjson_set_type(val, YYJSON_TYPE_NUM, YYJSON_SUBTYPE_UINT);
    unsafe_styyjson_set_len(val, 0);
    ((styyjson_val *)val)->uni.u64 = num;
}

styyjson_api_inline void unsafe_styyjson_set_sint(void *val, int64_t num) {
    unsafe_styyjson_set_type(val, YYJSON_TYPE_NUM, YYJSON_SUBTYPE_SINT);
    unsafe_styyjson_set_len(val, 0);
    ((styyjson_val *)val)->uni.i64 = num;
}

styyjson_api_inline void unsafe_styyjson_set_real(void *val, double num) {
    unsafe_styyjson_set_type(val, YYJSON_TYPE_NUM, YYJSON_SUBTYPE_REAL);
    unsafe_styyjson_set_len(val, 0);
    ((styyjson_val *)val)->uni.f64 = num;
}

styyjson_api_inline void unsafe_styyjson_set_str(void *val, const char *str) {
    size_t len = strlen(str);
    bool noesc = unsafe_styyjson_is_str_noesc(str, len);
    styyjson_subtype sub = noesc ? YYJSON_SUBTYPE_NOESC : YYJSON_SUBTYPE_NONE;
    unsafe_styyjson_set_type(val, YYJSON_TYPE_STR, sub);
    unsafe_styyjson_set_len(val, len);
    ((styyjson_val *)val)->uni.str = str;
}

styyjson_api_inline void unsafe_styyjson_set_strn(void *val, const char *str,
                                              size_t len) {
    unsafe_styyjson_set_type(val, YYJSON_TYPE_STR, YYJSON_SUBTYPE_NONE);
    unsafe_styyjson_set_len(val, len);
    ((styyjson_val *)val)->uni.str = str;
}

styyjson_api_inline void unsafe_styyjson_set_arr(void *val, size_t size) {
    unsafe_styyjson_set_type(val, YYJSON_TYPE_ARR, YYJSON_SUBTYPE_NONE);
    unsafe_styyjson_set_len(val, size);
}

styyjson_api_inline void unsafe_styyjson_set_obj(void *val, size_t size) {
    unsafe_styyjson_set_type(val, YYJSON_TYPE_OBJ, YYJSON_SUBTYPE_NONE);
    unsafe_styyjson_set_len(val, size);
}



/*==============================================================================
 * JSON Document API (Implementation)
 *============================================================================*/

styyjson_api_inline styyjson_val *styyjson_doc_get_root(styyjson_doc *doc) {
    return doc ? doc->root : NULL;
}

styyjson_api_inline size_t styyjson_doc_get_read_size(styyjson_doc *doc) {
    return doc ? doc->dat_read : 0;
}

styyjson_api_inline size_t styyjson_doc_get_val_count(styyjson_doc *doc) {
    return doc ? doc->val_read : 0;
}

styyjson_api_inline void styyjson_doc_free(styyjson_doc *doc) {
    if (doc) {
        styyjson_alc alc = doc->alc;
        memset(&doc->alc, 0, sizeof(alc));
        if (doc->str_pool) alc.free(alc.ctx, doc->str_pool);
        alc.free(alc.ctx, doc);
    }
}



/*==============================================================================
 * JSON Value Type API (Implementation)
 *============================================================================*/

styyjson_api_inline bool styyjson_is_raw(styyjson_val *val) {
    return val ? unsafe_styyjson_is_raw(val) : false;
}

styyjson_api_inline bool styyjson_is_null(styyjson_val *val) {
    return val ? unsafe_styyjson_is_null(val) : false;
}

styyjson_api_inline bool styyjson_is_true(styyjson_val *val) {
    return val ? unsafe_styyjson_is_true(val) : false;
}

styyjson_api_inline bool styyjson_is_false(styyjson_val *val) {
    return val ? unsafe_styyjson_is_false(val) : false;
}

styyjson_api_inline bool styyjson_is_bool(styyjson_val *val) {
    return val ? unsafe_styyjson_is_bool(val) : false;
}

styyjson_api_inline bool styyjson_is_uint(styyjson_val *val) {
    return val ? unsafe_styyjson_is_uint(val) : false;
}

styyjson_api_inline bool styyjson_is_sint(styyjson_val *val) {
    return val ? unsafe_styyjson_is_sint(val) : false;
}

styyjson_api_inline bool styyjson_is_int(styyjson_val *val) {
    return val ? unsafe_styyjson_is_int(val) : false;
}

styyjson_api_inline bool styyjson_is_real(styyjson_val *val) {
    return val ? unsafe_styyjson_is_real(val) : false;
}

styyjson_api_inline bool styyjson_is_num(styyjson_val *val) {
    return val ? unsafe_styyjson_is_num(val) : false;
}

styyjson_api_inline bool styyjson_is_str(styyjson_val *val) {
    return val ? unsafe_styyjson_is_str(val) : false;
}

styyjson_api_inline bool styyjson_is_arr(styyjson_val *val) {
    return val ? unsafe_styyjson_is_arr(val) : false;
}

styyjson_api_inline bool styyjson_is_obj(styyjson_val *val) {
    return val ? unsafe_styyjson_is_obj(val) : false;
}

styyjson_api_inline bool styyjson_is_ctn(styyjson_val *val) {
    return val ? unsafe_styyjson_is_ctn(val) : false;
}



/*==============================================================================
 * JSON Value Content API (Implementation)
 *============================================================================*/

styyjson_api_inline styyjson_type styyjson_get_type(styyjson_val *val) {
    return val ? unsafe_styyjson_get_type(val) : YYJSON_TYPE_NONE;
}

styyjson_api_inline styyjson_subtype styyjson_get_subtype(styyjson_val *val) {
    return val ? unsafe_styyjson_get_subtype(val) : YYJSON_SUBTYPE_NONE;
}

styyjson_api_inline uint8_t styyjson_get_tag(styyjson_val *val) {
    return val ? unsafe_styyjson_get_tag(val) : 0;
}

styyjson_api_inline const char *styyjson_get_type_desc(styyjson_val *val) {
    switch (styyjson_get_tag(val)) {
        case YYJSON_TYPE_RAW  | YYJSON_SUBTYPE_NONE:  return "raw";
        case YYJSON_TYPE_NULL | YYJSON_SUBTYPE_NONE:  return "null";
        case YYJSON_TYPE_STR  | YYJSON_SUBTYPE_NONE:  return "string";
        case YYJSON_TYPE_STR  | YYJSON_SUBTYPE_NOESC: return "string";
        case YYJSON_TYPE_ARR  | YYJSON_SUBTYPE_NONE:  return "array";
        case YYJSON_TYPE_OBJ  | YYJSON_SUBTYPE_NONE:  return "object";
        case YYJSON_TYPE_BOOL | YYJSON_SUBTYPE_TRUE:  return "true";
        case YYJSON_TYPE_BOOL | YYJSON_SUBTYPE_FALSE: return "false";
        case YYJSON_TYPE_NUM  | YYJSON_SUBTYPE_UINT:  return "uint";
        case YYJSON_TYPE_NUM  | YYJSON_SUBTYPE_SINT:  return "sint";
        case YYJSON_TYPE_NUM  | YYJSON_SUBTYPE_REAL:  return "real";
        default:                                      return "unknown";
    }
}

styyjson_api_inline const char *styyjson_get_raw(styyjson_val *val) {
    return styyjson_is_raw(val) ? unsafe_styyjson_get_raw(val) : NULL;
}

styyjson_api_inline bool styyjson_get_bool(styyjson_val *val) {
    return styyjson_is_bool(val) ? unsafe_styyjson_get_bool(val) : false;
}

styyjson_api_inline uint64_t styyjson_get_uint(styyjson_val *val) {
    return styyjson_is_int(val) ? unsafe_styyjson_get_uint(val) : 0;
}

styyjson_api_inline int64_t styyjson_get_sint(styyjson_val *val) {
    return styyjson_is_int(val) ? unsafe_styyjson_get_sint(val) : 0;
}

styyjson_api_inline int styyjson_get_int(styyjson_val *val) {
    return styyjson_is_int(val) ? unsafe_styyjson_get_int(val) : 0;
}

styyjson_api_inline double styyjson_get_real(styyjson_val *val) {
    return styyjson_is_real(val) ? unsafe_styyjson_get_real(val) : 0.0;
}

styyjson_api_inline double styyjson_get_num(styyjson_val *val) {
    return val ? unsafe_styyjson_get_num(val) : 0.0;
}

styyjson_api_inline const char *styyjson_get_str(styyjson_val *val) {
    return styyjson_is_str(val) ? unsafe_styyjson_get_str(val) : NULL;
}

styyjson_api_inline size_t styyjson_get_len(styyjson_val *val) {
    return val ? unsafe_styyjson_get_len(val) : 0;
}

styyjson_api_inline bool styyjson_equals_str(styyjson_val *val, const char *str) {
    if (styyjson_likely(val && str)) {
        return unsafe_styyjson_is_str(val) &&
               unsafe_styyjson_equals_str(val, str);
    }
    return false;
}

styyjson_api_inline bool styyjson_equals_strn(styyjson_val *val, const char *str,
                                          size_t len) {
    if (styyjson_likely(val && str)) {
        return unsafe_styyjson_is_str(val) &&
               unsafe_styyjson_equals_strn(val, str, len);
    }
    return false;
}

styyjson_api bool unsafe_styyjson_equals(styyjson_val *lhs, styyjson_val *rhs);

styyjson_api_inline bool styyjson_equals(styyjson_val *lhs, styyjson_val *rhs) {
    if (styyjson_unlikely(!lhs || !rhs)) return false;
    return unsafe_styyjson_equals(lhs, rhs);
}

styyjson_api_inline bool styyjson_set_raw(styyjson_val *val,
                                      const char *raw, size_t len) {
    if (styyjson_unlikely(!val || unsafe_styyjson_is_ctn(val))) return false;
    unsafe_styyjson_set_raw(val, raw, len);
    return true;
}

styyjson_api_inline bool styyjson_set_null(styyjson_val *val) {
    if (styyjson_unlikely(!val || unsafe_styyjson_is_ctn(val))) return false;
    unsafe_styyjson_set_null(val);
    return true;
}

styyjson_api_inline bool styyjson_set_bool(styyjson_val *val, bool num) {
    if (styyjson_unlikely(!val || unsafe_styyjson_is_ctn(val))) return false;
    unsafe_styyjson_set_bool(val, num);
    return true;
}

styyjson_api_inline bool styyjson_set_uint(styyjson_val *val, uint64_t num) {
    if (styyjson_unlikely(!val || unsafe_styyjson_is_ctn(val))) return false;
    unsafe_styyjson_set_uint(val, num);
    return true;
}

styyjson_api_inline bool styyjson_set_sint(styyjson_val *val, int64_t num) {
    if (styyjson_unlikely(!val || unsafe_styyjson_is_ctn(val))) return false;
    unsafe_styyjson_set_sint(val, num);
    return true;
}

styyjson_api_inline bool styyjson_set_int(styyjson_val *val, int num) {
    if (styyjson_unlikely(!val || unsafe_styyjson_is_ctn(val))) return false;
    unsafe_styyjson_set_sint(val, (int64_t)num);
    return true;
}

styyjson_api_inline bool styyjson_set_real(styyjson_val *val, double num) {
    if (styyjson_unlikely(!val || unsafe_styyjson_is_ctn(val))) return false;
    unsafe_styyjson_set_real(val, num);
    return true;
}

styyjson_api_inline bool styyjson_set_str(styyjson_val *val, const char *str) {
    if (styyjson_unlikely(!val || unsafe_styyjson_is_ctn(val))) return false;
    if (styyjson_unlikely(!str)) return false;
    unsafe_styyjson_set_str(val, str);
    return true;
}

styyjson_api_inline bool styyjson_set_strn(styyjson_val *val,
                                       const char *str, size_t len) {
    if (styyjson_unlikely(!val || unsafe_styyjson_is_ctn(val))) return false;
    if (styyjson_unlikely(!str)) return false;
    unsafe_styyjson_set_strn(val, str, len);
    return true;
}



/*==============================================================================
 * JSON Array API (Implementation)
 *============================================================================*/

styyjson_api_inline size_t styyjson_arr_size(styyjson_val *arr) {
    return styyjson_is_arr(arr) ? unsafe_styyjson_get_len(arr) : 0;
}

styyjson_api_inline styyjson_val *styyjson_arr_get(styyjson_val *arr, size_t idx) {
    if (styyjson_likely(styyjson_is_arr(arr))) {
        if (styyjson_likely(unsafe_styyjson_get_len(arr) > idx)) {
            styyjson_val *val = unsafe_styyjson_get_first(arr);
            if (unsafe_styyjson_arr_is_flat(arr)) {
                return val + idx;
            } else {
                while (idx-- > 0) val = unsafe_styyjson_get_next(val);
                return val;
            }
        }
    }
    return NULL;
}

styyjson_api_inline styyjson_val *styyjson_arr_get_first(styyjson_val *arr) {
    if (styyjson_likely(styyjson_is_arr(arr))) {
        if (styyjson_likely(unsafe_styyjson_get_len(arr) > 0)) {
            return unsafe_styyjson_get_first(arr);
        }
    }
    return NULL;
}

styyjson_api_inline styyjson_val *styyjson_arr_get_last(styyjson_val *arr) {
    if (styyjson_likely(styyjson_is_arr(arr))) {
        size_t len = unsafe_styyjson_get_len(arr);
        if (styyjson_likely(len > 0)) {
            styyjson_val *val = unsafe_styyjson_get_first(arr);
            if (unsafe_styyjson_arr_is_flat(arr)) {
                return val + (len - 1);
            } else {
                while (len-- > 1) val = unsafe_styyjson_get_next(val);
                return val;
            }
        }
    }
    return NULL;
}



/*==============================================================================
 * JSON Array Iterator API (Implementation)
 *============================================================================*/

styyjson_api_inline bool styyjson_arr_iter_init(styyjson_val *arr,
                                            styyjson_arr_iter *iter) {
    if (styyjson_likely(styyjson_is_arr(arr) && iter)) {
        iter->idx = 0;
        iter->max = unsafe_styyjson_get_len(arr);
        iter->cur = unsafe_styyjson_get_first(arr);
        return true;
    }
    if (iter) memset(iter, 0, sizeof(styyjson_arr_iter));
    return false;
}

styyjson_api_inline styyjson_arr_iter styyjson_arr_iter_with(styyjson_val *arr) {
    styyjson_arr_iter iter;
    styyjson_arr_iter_init(arr, &iter);
    return iter;
}

styyjson_api_inline bool styyjson_arr_iter_has_next(styyjson_arr_iter *iter) {
    return iter ? iter->idx < iter->max : false;
}

styyjson_api_inline styyjson_val *styyjson_arr_iter_next(styyjson_arr_iter *iter) {
    styyjson_val *val;
    if (iter && iter->idx < iter->max) {
        val = iter->cur;
        iter->cur = unsafe_styyjson_get_next(val);
        iter->idx++;
        return val;
    }
    return NULL;
}



/*==============================================================================
 * JSON Object API (Implementation)
 *============================================================================*/

styyjson_api_inline size_t styyjson_obj_size(styyjson_val *obj) {
    return styyjson_is_obj(obj) ? unsafe_styyjson_get_len(obj) : 0;
}

styyjson_api_inline styyjson_val *styyjson_obj_get(styyjson_val *obj,
                                             const char *key) {
    return styyjson_obj_getn(obj, key, key ? strlen(key) : 0);
}

styyjson_api_inline styyjson_val *styyjson_obj_getn(styyjson_val *obj,
                                              const char *_key,
                                              size_t key_len) {
    if (styyjson_likely(styyjson_is_obj(obj) && _key)) {
        size_t len = unsafe_styyjson_get_len(obj);
        styyjson_val *key = unsafe_styyjson_get_first(obj);
        while (len-- > 0) {
            if (unsafe_styyjson_equals_strn(key, _key, key_len)) return key + 1;
            key = unsafe_styyjson_get_next(key + 1);
        }
    }
    return NULL;
}



/*==============================================================================
 * JSON Object Iterator API (Implementation)
 *============================================================================*/

styyjson_api_inline bool styyjson_obj_iter_init(styyjson_val *obj,
                                            styyjson_obj_iter *iter) {
    if (styyjson_likely(styyjson_is_obj(obj) && iter)) {
        iter->idx = 0;
        iter->max = unsafe_styyjson_get_len(obj);
        iter->cur = unsafe_styyjson_get_first(obj);
        iter->obj = obj;
        return true;
    }
    if (iter) memset(iter, 0, sizeof(styyjson_obj_iter));
    return false;
}

styyjson_api_inline styyjson_obj_iter styyjson_obj_iter_with(styyjson_val *obj) {
    styyjson_obj_iter iter;
    styyjson_obj_iter_init(obj, &iter);
    return iter;
}

styyjson_api_inline bool styyjson_obj_iter_has_next(styyjson_obj_iter *iter) {
    return iter ? iter->idx < iter->max : false;
}

styyjson_api_inline styyjson_val *styyjson_obj_iter_next(styyjson_obj_iter *iter) {
    if (iter && iter->idx < iter->max) {
        styyjson_val *key = iter->cur;
        iter->idx++;
        iter->cur = unsafe_styyjson_get_next(key + 1);
        return key;
    }
    return NULL;
}

styyjson_api_inline styyjson_val *styyjson_obj_iter_get_val(styyjson_val *key) {
    return key ? key + 1 : NULL;
}

styyjson_api_inline styyjson_val *styyjson_obj_iter_get(styyjson_obj_iter *iter,
                                                  const char *key) {
    return styyjson_obj_iter_getn(iter, key, key ? strlen(key) : 0);
}

styyjson_api_inline styyjson_val *styyjson_obj_iter_getn(styyjson_obj_iter *iter,
                                                   const char *key,
                                                   size_t key_len) {
    if (iter && key) {
        size_t idx = iter->idx;
        size_t max = iter->max;
        styyjson_val *cur = iter->cur;
        if (styyjson_unlikely(idx == max)) {
            idx = 0;
            cur = unsafe_styyjson_get_first(iter->obj);
        }
        while (idx++ < max) {
            styyjson_val *next = unsafe_styyjson_get_next(cur + 1);
            if (unsafe_styyjson_equals_strn(cur, key, key_len)) {
                iter->idx = idx;
                iter->cur = next;
                return cur + 1;
            }
            cur = next;
            if (idx == iter->max && iter->idx < iter->max) {
                idx = 0;
                max = iter->idx;
                cur = unsafe_styyjson_get_first(iter->obj);
            }
        }
    }
    return NULL;
}



/*==============================================================================
 * Mutable JSON Structure (Implementation)
 *============================================================================*/

/**
 Mutable JSON value, 24 bytes.
 The 'tag' and 'uni' field is same as immutable value.
 The 'next' field links all elements inside the container to be a cycle.
 */
struct styyjson_mut_val {
    uint64_t tag; /**< type, subtype and length */
    styyjson_val_uni uni; /**< payload */
    styyjson_mut_val *next; /**< the next value in circular linked list */
};

/**
 A memory chunk in string memory pool.
 */
typedef struct styyjson_str_chunk {
    struct styyjson_str_chunk *next; /* next chunk linked list */
    size_t chunk_size; /* chunk size in bytes */
    /* char str[]; flexible array member */
} styyjson_str_chunk;

/**
 A memory pool to hold all strings in a mutable document.
 */
typedef struct styyjson_str_pool {
    char *cur; /* cursor inside current chunk */
    char *end; /* the end of current chunk */
    size_t chunk_size; /* chunk size in bytes while creating new chunk */
    size_t chunk_size_max; /* maximum chunk size in bytes */
    styyjson_str_chunk *chunks; /* a linked list of chunks, nullable */
} styyjson_str_pool;

/**
 A memory chunk in value memory pool.
 `sizeof(styyjson_val_chunk)` should not larger than `sizeof(styyjson_mut_val)`.
 */
typedef struct styyjson_val_chunk {
    struct styyjson_val_chunk *next; /* next chunk linked list */
    size_t chunk_size; /* chunk size in bytes */
    /* char pad[sizeof(styyjson_mut_val) - sizeof(styyjson_val_chunk)]; padding */
    /* styyjson_mut_val vals[]; flexible array member */
} styyjson_val_chunk;

/**
 A memory pool to hold all values in a mutable document.
 */
typedef struct styyjson_val_pool {
    styyjson_mut_val *cur; /* cursor inside current chunk */
    styyjson_mut_val *end; /* the end of current chunk */
    size_t chunk_size; /* chunk size in bytes while creating new chunk */
    size_t chunk_size_max; /* maximum chunk size in bytes */
    styyjson_val_chunk *chunks; /* a linked list of chunks, nullable */
} styyjson_val_pool;

struct styyjson_mut_doc {
    styyjson_mut_val *root; /**< root value of the JSON document, nullable */
    styyjson_alc alc; /**< a valid allocator, nonnull */
    styyjson_str_pool str_pool; /**< string memory pool */
    styyjson_val_pool val_pool; /**< value memory pool */
};

/* Ensures the capacity to at least equal to the specified byte length. */
styyjson_api bool unsafe_styyjson_str_pool_grow(styyjson_str_pool *pool,
                                            const styyjson_alc *alc,
                                            size_t len);

/* Ensures the capacity to at least equal to the specified value count. */
styyjson_api bool unsafe_styyjson_val_pool_grow(styyjson_val_pool *pool,
                                            const styyjson_alc *alc,
                                            size_t count);

/* Allocate memory for string. */
styyjson_api_inline char *unsafe_styyjson_mut_str_alc(styyjson_mut_doc *doc,
                                                  size_t len) {
    char *mem;
    const styyjson_alc *alc = &doc->alc;
    styyjson_str_pool *pool = &doc->str_pool;
    if (styyjson_unlikely((size_t)(pool->end - pool->cur) <= len)) {
        if (styyjson_unlikely(!unsafe_styyjson_str_pool_grow(pool, alc, len + 1))) {
            return NULL;
        }
    }
    mem = pool->cur;
    pool->cur = mem + len + 1;
    return mem;
}

styyjson_api_inline char *unsafe_styyjson_mut_strncpy(styyjson_mut_doc *doc,
                                                  const char *str, size_t len) {
    char *mem = unsafe_styyjson_mut_str_alc(doc, len);
    if (styyjson_unlikely(!mem)) return NULL;
    memcpy((void *)mem, (const void *)str, len);
    mem[len] = '\0';
    return mem;
}

styyjson_api_inline styyjson_mut_val *unsafe_styyjson_mut_val(styyjson_mut_doc *doc,
                                                        size_t count) {
    styyjson_mut_val *val;
    styyjson_alc *alc = &doc->alc;
    styyjson_val_pool *pool = &doc->val_pool;
    if (styyjson_unlikely((size_t)(pool->end - pool->cur) < count)) {
        if (styyjson_unlikely(!unsafe_styyjson_val_pool_grow(pool, alc, count))) {
            return NULL;
        }
    }
    val = pool->cur;
    pool->cur += count;
    return val;
}



/*==============================================================================
 * Mutable JSON Document API (Implementation)
 *============================================================================*/

styyjson_api_inline styyjson_mut_val *styyjson_mut_doc_get_root(styyjson_mut_doc *doc) {
    return doc ? doc->root : NULL;
}

styyjson_api_inline void styyjson_mut_doc_set_root(styyjson_mut_doc *doc,
                                               styyjson_mut_val *root) {
    if (doc) doc->root = root;
}



/*==============================================================================
 * Mutable JSON Value Type API (Implementation)
 *============================================================================*/

styyjson_api_inline bool styyjson_mut_is_raw(styyjson_mut_val *val) {
    return val ? unsafe_styyjson_is_raw(val) : false;
}

styyjson_api_inline bool styyjson_mut_is_null(styyjson_mut_val *val) {
    return val ? unsafe_styyjson_is_null(val) : false;
}

styyjson_api_inline bool styyjson_mut_is_true(styyjson_mut_val *val) {
    return val ? unsafe_styyjson_is_true(val) : false;
}

styyjson_api_inline bool styyjson_mut_is_false(styyjson_mut_val *val) {
    return val ? unsafe_styyjson_is_false(val) : false;
}

styyjson_api_inline bool styyjson_mut_is_bool(styyjson_mut_val *val) {
    return val ? unsafe_styyjson_is_bool(val) : false;
}

styyjson_api_inline bool styyjson_mut_is_uint(styyjson_mut_val *val) {
    return val ? unsafe_styyjson_is_uint(val) : false;
}

styyjson_api_inline bool styyjson_mut_is_sint(styyjson_mut_val *val) {
    return val ? unsafe_styyjson_is_sint(val) : false;
}

styyjson_api_inline bool styyjson_mut_is_int(styyjson_mut_val *val) {
    return val ? unsafe_styyjson_is_int(val) : false;
}

styyjson_api_inline bool styyjson_mut_is_real(styyjson_mut_val *val) {
    return val ? unsafe_styyjson_is_real(val) : false;
}

styyjson_api_inline bool styyjson_mut_is_num(styyjson_mut_val *val) {
    return val ? unsafe_styyjson_is_num(val) : false;
}

styyjson_api_inline bool styyjson_mut_is_str(styyjson_mut_val *val) {
    return val ? unsafe_styyjson_is_str(val) : false;
}

styyjson_api_inline bool styyjson_mut_is_arr(styyjson_mut_val *val) {
    return val ? unsafe_styyjson_is_arr(val) : false;
}

styyjson_api_inline bool styyjson_mut_is_obj(styyjson_mut_val *val) {
    return val ? unsafe_styyjson_is_obj(val) : false;
}

styyjson_api_inline bool styyjson_mut_is_ctn(styyjson_mut_val *val) {
    return val ? unsafe_styyjson_is_ctn(val) : false;
}



/*==============================================================================
 * Mutable JSON Value Content API (Implementation)
 *============================================================================*/

styyjson_api_inline styyjson_type styyjson_mut_get_type(styyjson_mut_val *val) {
    return styyjson_get_type((styyjson_val *)val);
}

styyjson_api_inline styyjson_subtype styyjson_mut_get_subtype(styyjson_mut_val *val) {
    return styyjson_get_subtype((styyjson_val *)val);
}

styyjson_api_inline uint8_t styyjson_mut_get_tag(styyjson_mut_val *val) {
    return styyjson_get_tag((styyjson_val *)val);
}

styyjson_api_inline const char *styyjson_mut_get_type_desc(styyjson_mut_val *val) {
    return styyjson_get_type_desc((styyjson_val *)val);
}

styyjson_api_inline const char *styyjson_mut_get_raw(styyjson_mut_val *val) {
    return styyjson_get_raw((styyjson_val *)val);
}

styyjson_api_inline bool styyjson_mut_get_bool(styyjson_mut_val *val) {
    return styyjson_get_bool((styyjson_val *)val);
}

styyjson_api_inline uint64_t styyjson_mut_get_uint(styyjson_mut_val *val) {
    return styyjson_get_uint((styyjson_val *)val);
}

styyjson_api_inline int64_t styyjson_mut_get_sint(styyjson_mut_val *val) {
    return styyjson_get_sint((styyjson_val *)val);
}

styyjson_api_inline int styyjson_mut_get_int(styyjson_mut_val *val) {
    return styyjson_get_int((styyjson_val *)val);
}

styyjson_api_inline double styyjson_mut_get_real(styyjson_mut_val *val) {
    return styyjson_get_real((styyjson_val *)val);
}

styyjson_api_inline double styyjson_mut_get_num(styyjson_mut_val *val) {
    return styyjson_get_num((styyjson_val *)val);
}

styyjson_api_inline const char *styyjson_mut_get_str(styyjson_mut_val *val) {
    return styyjson_get_str((styyjson_val *)val);
}

styyjson_api_inline size_t styyjson_mut_get_len(styyjson_mut_val *val) {
    return styyjson_get_len((styyjson_val *)val);
}

styyjson_api_inline bool styyjson_mut_equals_str(styyjson_mut_val *val,
                                             const char *str) {
    return styyjson_equals_str((styyjson_val *)val, str);
}

styyjson_api_inline bool styyjson_mut_equals_strn(styyjson_mut_val *val,
                                              const char *str, size_t len) {
    return styyjson_equals_strn((styyjson_val *)val, str, len);
}

styyjson_api bool unsafe_styyjson_mut_equals(styyjson_mut_val *lhs,
                                         styyjson_mut_val *rhs);

styyjson_api_inline bool styyjson_mut_equals(styyjson_mut_val *lhs,
                                         styyjson_mut_val *rhs) {
    if (styyjson_unlikely(!lhs || !rhs)) return false;
    return unsafe_styyjson_mut_equals(lhs, rhs);
}

styyjson_api_inline bool styyjson_mut_set_raw(styyjson_mut_val *val,
                                          const char *raw, size_t len) {
    if (styyjson_unlikely(!val || !raw)) return false;
    unsafe_styyjson_set_raw(val, raw, len);
    return true;
}

styyjson_api_inline bool styyjson_mut_set_null(styyjson_mut_val *val) {
    if (styyjson_unlikely(!val)) return false;
    unsafe_styyjson_set_null(val);
    return true;
}

styyjson_api_inline bool styyjson_mut_set_bool(styyjson_mut_val *val, bool num) {
    if (styyjson_unlikely(!val)) return false;
    unsafe_styyjson_set_bool(val, num);
    return true;
}

styyjson_api_inline bool styyjson_mut_set_uint(styyjson_mut_val *val, uint64_t num) {
    if (styyjson_unlikely(!val)) return false;
    unsafe_styyjson_set_uint(val, num);
    return true;
}

styyjson_api_inline bool styyjson_mut_set_sint(styyjson_mut_val *val, int64_t num) {
    if (styyjson_unlikely(!val)) return false;
    unsafe_styyjson_set_sint(val, num);
    return true;
}

styyjson_api_inline bool styyjson_mut_set_int(styyjson_mut_val *val, int num) {
    if (styyjson_unlikely(!val)) return false;
    unsafe_styyjson_set_sint(val, (int64_t)num);
    return true;
}

styyjson_api_inline bool styyjson_mut_set_real(styyjson_mut_val *val, double num) {
    if (styyjson_unlikely(!val)) return false;
    unsafe_styyjson_set_real(val, num);
    return true;
}

styyjson_api_inline bool styyjson_mut_set_str(styyjson_mut_val *val,
                                          const char *str) {
    if (styyjson_unlikely(!val || !str)) return false;
    unsafe_styyjson_set_str(val, str);
    return true;
}

styyjson_api_inline bool styyjson_mut_set_strn(styyjson_mut_val *val,
                                           const char *str, size_t len) {
    if (styyjson_unlikely(!val || !str)) return false;
    unsafe_styyjson_set_strn(val, str, len);
    return true;
}

styyjson_api_inline bool styyjson_mut_set_arr(styyjson_mut_val *val) {
    if (styyjson_unlikely(!val)) return false;
    unsafe_styyjson_set_arr(val, 0);
    return true;
}

styyjson_api_inline bool styyjson_mut_set_obj(styyjson_mut_val *val) {
    if (styyjson_unlikely(!val)) return false;
    unsafe_styyjson_set_obj(val, 0);
    return true;
}



/*==============================================================================
 * Mutable JSON Value Creation API (Implementation)
 *============================================================================*/

styyjson_api_inline styyjson_mut_val *styyjson_mut_raw(styyjson_mut_doc *doc,
                                                 const char *str) {
    if (styyjson_likely(str)) return styyjson_mut_rawn(doc, str, strlen(str));
    return NULL;
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_rawn(styyjson_mut_doc *doc,
                                                  const char *str,
                                                  size_t len) {
    if (styyjson_likely(doc && str)) {
        styyjson_mut_val *val = unsafe_styyjson_mut_val(doc, 1);
        if (styyjson_likely(val)) {
            val->tag = ((uint64_t)len << YYJSON_TAG_BIT) | YYJSON_TYPE_RAW;
            val->uni.str = str;
            return val;
        }
    }
    return NULL;
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_rawcpy(styyjson_mut_doc *doc,
                                                    const char *str) {
    if (styyjson_likely(str)) return styyjson_mut_rawncpy(doc, str, strlen(str));
    return NULL;
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_rawncpy(styyjson_mut_doc *doc,
                                                     const char *str,
                                                     size_t len) {
    if (styyjson_likely(doc && str)) {
        styyjson_mut_val *val = unsafe_styyjson_mut_val(doc, 1);
        char *new_str = unsafe_styyjson_mut_strncpy(doc, str, len);
        if (styyjson_likely(val && new_str)) {
            val->tag = ((uint64_t)len << YYJSON_TAG_BIT) | YYJSON_TYPE_RAW;
            val->uni.str = new_str;
            return val;
        }
    }
    return NULL;
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_null(styyjson_mut_doc *doc) {
    if (styyjson_likely(doc)) {
        styyjson_mut_val *val = unsafe_styyjson_mut_val(doc, 1);
        if (styyjson_likely(val)) {
            val->tag = YYJSON_TYPE_NULL | YYJSON_SUBTYPE_NONE;
            return val;
        }
    }
    return NULL;
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_true(styyjson_mut_doc *doc) {
    if (styyjson_likely(doc)) {
        styyjson_mut_val *val = unsafe_styyjson_mut_val(doc, 1);
        if (styyjson_likely(val)) {
            val->tag = YYJSON_TYPE_BOOL | YYJSON_SUBTYPE_TRUE;
            return val;
        }
    }
    return NULL;
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_false(styyjson_mut_doc *doc) {
    if (styyjson_likely(doc)) {
        styyjson_mut_val *val = unsafe_styyjson_mut_val(doc, 1);
        if (styyjson_likely(val)) {
            val->tag = YYJSON_TYPE_BOOL | YYJSON_SUBTYPE_FALSE;
            return val;
        }
    }
    return NULL;
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_bool(styyjson_mut_doc *doc,
                                                  bool _val) {
    if (styyjson_likely(doc)) {
        styyjson_mut_val *val = unsafe_styyjson_mut_val(doc, 1);
        if (styyjson_likely(val)) {
            _val = !!_val;
            val->tag = YYJSON_TYPE_BOOL | (uint8_t)((uint8_t)_val << 3);
            return val;
        }
    }
    return NULL;
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_uint(styyjson_mut_doc *doc,
                                                  uint64_t num) {
    if (styyjson_likely(doc)) {
        styyjson_mut_val *val = unsafe_styyjson_mut_val(doc, 1);
        if (styyjson_likely(val)) {
            val->tag = YYJSON_TYPE_NUM | YYJSON_SUBTYPE_UINT;
            val->uni.u64 = num;
            return val;
        }
    }
    return NULL;
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_sint(styyjson_mut_doc *doc,
                                                  int64_t num) {
    if (styyjson_likely(doc)) {
        styyjson_mut_val *val = unsafe_styyjson_mut_val(doc, 1);
        if (styyjson_likely(val)) {
            val->tag = YYJSON_TYPE_NUM | YYJSON_SUBTYPE_SINT;
            val->uni.i64 = num;
            return val;
        }
    }
    return NULL;
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_int(styyjson_mut_doc *doc,
                                                 int64_t num) {
    return styyjson_mut_sint(doc, num);
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_real(styyjson_mut_doc *doc,
                                                  double num) {
    if (styyjson_likely(doc)) {
        styyjson_mut_val *val = unsafe_styyjson_mut_val(doc, 1);
        if (styyjson_likely(val)) {
            val->tag = YYJSON_TYPE_NUM | YYJSON_SUBTYPE_REAL;
            val->uni.f64 = num;
            return val;
        }
    }
    return NULL;
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_str(styyjson_mut_doc *doc,
                                                 const char *str) {
    if (styyjson_likely(doc && str)) {
        size_t len = strlen(str);
        bool noesc = unsafe_styyjson_is_str_noesc(str, len);
        styyjson_subtype sub = noesc ? YYJSON_SUBTYPE_NOESC : YYJSON_SUBTYPE_NONE;
        styyjson_mut_val *val = unsafe_styyjson_mut_val(doc, 1);
        if (styyjson_likely(val)) {
            val->tag = ((uint64_t)len << YYJSON_TAG_BIT) |
                        (uint64_t)(YYJSON_TYPE_STR | sub);
            val->uni.str = str;
            return val;
        }
    }
    return NULL;
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_strn(styyjson_mut_doc *doc,
                                                  const char *str,
                                                  size_t len) {
    if (styyjson_likely(doc && str)) {
        styyjson_mut_val *val = unsafe_styyjson_mut_val(doc, 1);
        if (styyjson_likely(val)) {
            val->tag = ((uint64_t)len << YYJSON_TAG_BIT) | YYJSON_TYPE_STR;
            val->uni.str = str;
            return val;
        }
    }
    return NULL;
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_strcpy(styyjson_mut_doc *doc,
                                                    const char *str) {
    if (styyjson_likely(doc && str)) {
        size_t len = strlen(str);
        bool noesc = unsafe_styyjson_is_str_noesc(str, len);
        styyjson_subtype sub = noesc ? YYJSON_SUBTYPE_NOESC : YYJSON_SUBTYPE_NONE;
        styyjson_mut_val *val = unsafe_styyjson_mut_val(doc, 1);
        char *new_str = unsafe_styyjson_mut_strncpy(doc, str, len);
        if (styyjson_likely(val && new_str)) {
            val->tag = ((uint64_t)len << YYJSON_TAG_BIT) |
                        (uint64_t)(YYJSON_TYPE_STR | sub);
            val->uni.str = new_str;
            return val;
        }
    }
    return NULL;
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_strncpy(styyjson_mut_doc *doc,
                                                     const char *str,
                                                     size_t len) {
    if (styyjson_likely(doc && str)) {
        styyjson_mut_val *val = unsafe_styyjson_mut_val(doc, 1);
        char *new_str = unsafe_styyjson_mut_strncpy(doc, str, len);
        if (styyjson_likely(val && new_str)) {
            val->tag = ((uint64_t)len << YYJSON_TAG_BIT) | YYJSON_TYPE_STR;
            val->uni.str = new_str;
            return val;
        }
    }
    return NULL;
}



/*==============================================================================
 * Mutable JSON Array API (Implementation)
 *============================================================================*/

styyjson_api_inline size_t styyjson_mut_arr_size(styyjson_mut_val *arr) {
    return styyjson_mut_is_arr(arr) ? unsafe_styyjson_get_len(arr) : 0;
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_get(styyjson_mut_val *arr,
                                                     size_t idx) {
    if (styyjson_likely(idx < styyjson_mut_arr_size(arr))) {
        styyjson_mut_val *val = (styyjson_mut_val *)arr->uni.ptr;
        while (idx-- > 0) val = val->next;
        return val->next;
    }
    return NULL;
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_get_first(
    styyjson_mut_val *arr) {
    if (styyjson_likely(styyjson_mut_arr_size(arr) > 0)) {
        return ((styyjson_mut_val *)arr->uni.ptr)->next;
    }
    return NULL;
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_get_last(
    styyjson_mut_val *arr) {
    if (styyjson_likely(styyjson_mut_arr_size(arr) > 0)) {
        return ((styyjson_mut_val *)arr->uni.ptr);
    }
    return NULL;
}



/*==============================================================================
 * Mutable JSON Array Iterator API (Implementation)
 *============================================================================*/

styyjson_api_inline bool styyjson_mut_arr_iter_init(styyjson_mut_val *arr,
                                                styyjson_mut_arr_iter *iter) {
    if (styyjson_likely(styyjson_mut_is_arr(arr) && iter)) {
        iter->idx = 0;
        iter->max = unsafe_styyjson_get_len(arr);
        iter->cur = iter->max ? (styyjson_mut_val *)arr->uni.ptr : NULL;
        iter->pre = NULL;
        iter->arr = arr;
        return true;
    }
    if (iter) memset(iter, 0, sizeof(styyjson_mut_arr_iter));
    return false;
}

styyjson_api_inline styyjson_mut_arr_iter styyjson_mut_arr_iter_with(
    styyjson_mut_val *arr) {
    styyjson_mut_arr_iter iter;
    styyjson_mut_arr_iter_init(arr, &iter);
    return iter;
}

styyjson_api_inline bool styyjson_mut_arr_iter_has_next(styyjson_mut_arr_iter *iter) {
    return iter ? iter->idx < iter->max : false;
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_iter_next(
    styyjson_mut_arr_iter *iter) {
    if (iter && iter->idx < iter->max) {
        styyjson_mut_val *val = iter->cur;
        iter->pre = val;
        iter->cur = val->next;
        iter->idx++;
        return iter->cur;
    }
    return NULL;
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_iter_remove(
    styyjson_mut_arr_iter *iter) {
    if (styyjson_likely(iter && 0 < iter->idx && iter->idx <= iter->max)) {
        styyjson_mut_val *prev = iter->pre;
        styyjson_mut_val *cur = iter->cur;
        styyjson_mut_val *next = cur->next;
        if (styyjson_unlikely(iter->idx == iter->max)) iter->arr->uni.ptr = prev;
        iter->idx--;
        iter->max--;
        unsafe_styyjson_set_len(iter->arr, iter->max);
        prev->next = next;
        iter->cur = next;
        return cur;
    }
    return NULL;
}



/*==============================================================================
 * Mutable JSON Array Creation API (Implementation)
 *============================================================================*/

styyjson_api_inline styyjson_mut_val *styyjson_mut_arr(styyjson_mut_doc *doc) {
    if (styyjson_likely(doc)) {
        styyjson_mut_val *val = unsafe_styyjson_mut_val(doc, 1);
        if (styyjson_likely(val)) {
            val->tag = YYJSON_TYPE_ARR | YYJSON_SUBTYPE_NONE;
            return val;
        }
    }
    return NULL;
}

#define styyjson_mut_arr_with_func(func) \
    if (styyjson_likely(doc && ((0 < count && count < \
        (~(size_t)0) / sizeof(styyjson_mut_val) && vals) || count == 0))) { \
        styyjson_mut_val *arr = unsafe_styyjson_mut_val(doc, 1 + count); \
        if (styyjson_likely(arr)) { \
            arr->tag = ((uint64_t)count << YYJSON_TAG_BIT) | YYJSON_TYPE_ARR; \
            if (count > 0) { \
                size_t i; \
                for (i = 0; i < count; i++) { \
                    styyjson_mut_val *val = arr + i + 1; \
                    func \
                    val->next = val + 1; \
                } \
                arr[count].next = arr + 1; \
                arr->uni.ptr = arr + count; \
            } \
            return arr; \
        } \
    } \
    return NULL

styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_with_bool(
    styyjson_mut_doc *doc, const bool *vals, size_t count) {
    styyjson_mut_arr_with_func({
        bool _val = !!vals[i];
        val->tag = YYJSON_TYPE_BOOL | (uint8_t)((uint8_t)_val << 3);
    });
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_with_sint(
    styyjson_mut_doc *doc, const int64_t *vals, size_t count) {
    return styyjson_mut_arr_with_sint64(doc, vals, count);
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_with_uint(
    styyjson_mut_doc *doc, const uint64_t *vals, size_t count) {
    return styyjson_mut_arr_with_uint64(doc, vals, count);
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_with_real(
    styyjson_mut_doc *doc, const double *vals, size_t count) {
    return styyjson_mut_arr_with_double(doc, vals, count);
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_with_sint8(
    styyjson_mut_doc *doc, const int8_t *vals, size_t count) {
    styyjson_mut_arr_with_func({
        val->tag = YYJSON_TYPE_NUM | YYJSON_SUBTYPE_SINT;
        val->uni.i64 = (int64_t)vals[i];
    });
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_with_sint16(
    styyjson_mut_doc *doc, const int16_t *vals, size_t count) {
    styyjson_mut_arr_with_func({
        val->tag = YYJSON_TYPE_NUM | YYJSON_SUBTYPE_SINT;
        val->uni.i64 = vals[i];
    });
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_with_sint32(
    styyjson_mut_doc *doc, const int32_t *vals, size_t count) {
    styyjson_mut_arr_with_func({
        val->tag = YYJSON_TYPE_NUM | YYJSON_SUBTYPE_SINT;
        val->uni.i64 = vals[i];
    });
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_with_sint64(
    styyjson_mut_doc *doc, const int64_t *vals, size_t count) {
    styyjson_mut_arr_with_func({
        val->tag = YYJSON_TYPE_NUM | YYJSON_SUBTYPE_SINT;
        val->uni.i64 = vals[i];
    });
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_with_uint8(
    styyjson_mut_doc *doc, const uint8_t *vals, size_t count) {
    styyjson_mut_arr_with_func({
        val->tag = YYJSON_TYPE_NUM | YYJSON_SUBTYPE_UINT;
        val->uni.u64 = vals[i];
    });
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_with_uint16(
    styyjson_mut_doc *doc, const uint16_t *vals, size_t count) {
    styyjson_mut_arr_with_func({
        val->tag = YYJSON_TYPE_NUM | YYJSON_SUBTYPE_UINT;
        val->uni.u64 = vals[i];
    });
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_with_uint32(
    styyjson_mut_doc *doc, const uint32_t *vals, size_t count) {
    styyjson_mut_arr_with_func({
        val->tag = YYJSON_TYPE_NUM | YYJSON_SUBTYPE_UINT;
        val->uni.u64 = vals[i];
    });
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_with_uint64(
    styyjson_mut_doc *doc, const uint64_t *vals, size_t count) {
    styyjson_mut_arr_with_func({
        val->tag = YYJSON_TYPE_NUM | YYJSON_SUBTYPE_UINT;
        val->uni.u64 = vals[i];
    });
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_with_float(
    styyjson_mut_doc *doc, const float *vals, size_t count) {
    styyjson_mut_arr_with_func({
        val->tag = YYJSON_TYPE_NUM | YYJSON_SUBTYPE_REAL;
        val->uni.f64 = (double)vals[i];
    });
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_with_double(
    styyjson_mut_doc *doc, const double *vals, size_t count) {
    styyjson_mut_arr_with_func({
        val->tag = YYJSON_TYPE_NUM | YYJSON_SUBTYPE_REAL;
        val->uni.f64 = vals[i];
    });
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_with_str(
    styyjson_mut_doc *doc, const char **vals, size_t count) {
    styyjson_mut_arr_with_func({
        uint64_t len = (uint64_t)strlen(vals[i]);
        val->tag = (len << YYJSON_TAG_BIT) | YYJSON_TYPE_STR;
        val->uni.str = vals[i];
        if (styyjson_unlikely(!val->uni.str)) return NULL;
    });
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_with_strn(
    styyjson_mut_doc *doc, const char **vals, const size_t *lens, size_t count) {
    if (styyjson_unlikely(count > 0 && !lens)) return NULL;
    styyjson_mut_arr_with_func({
        val->tag = ((uint64_t)lens[i] << YYJSON_TAG_BIT) | YYJSON_TYPE_STR;
        val->uni.str = vals[i];
        if (styyjson_unlikely(!val->uni.str)) return NULL;
    });
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_with_strcpy(
    styyjson_mut_doc *doc, const char **vals, size_t count) {
    size_t len;
    const char *str;
    styyjson_mut_arr_with_func({
        str = vals[i];
        if (!str) return NULL;
        len = strlen(str);
        val->tag = ((uint64_t)len << YYJSON_TAG_BIT) | YYJSON_TYPE_STR;
        val->uni.str = unsafe_styyjson_mut_strncpy(doc, str, len);
        if (styyjson_unlikely(!val->uni.str)) return NULL;
    });
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_with_strncpy(
    styyjson_mut_doc *doc, const char **vals, const size_t *lens, size_t count) {
    size_t len;
    const char *str;
    if (styyjson_unlikely(count > 0 && !lens)) return NULL;
    styyjson_mut_arr_with_func({
        str = vals[i];
        len = lens[i];
        val->tag = ((uint64_t)len << YYJSON_TAG_BIT) | YYJSON_TYPE_STR;
        val->uni.str = unsafe_styyjson_mut_strncpy(doc, str, len);
        if (styyjson_unlikely(!val->uni.str)) return NULL;
    });
}

#undef styyjson_mut_arr_with_func



/*==============================================================================
 * Mutable JSON Array Modification API (Implementation)
 *============================================================================*/

styyjson_api_inline bool styyjson_mut_arr_insert(styyjson_mut_val *arr,
                                             styyjson_mut_val *val, size_t idx) {
    if (styyjson_likely(styyjson_mut_is_arr(arr) && val)) {
        size_t len = unsafe_styyjson_get_len(arr);
        if (styyjson_likely(idx <= len)) {
            unsafe_styyjson_set_len(arr, len + 1);
            if (len == 0) {
                val->next = val;
                arr->uni.ptr = val;
            } else {
                styyjson_mut_val *prev = ((styyjson_mut_val *)arr->uni.ptr);
                styyjson_mut_val *next = prev->next;
                if (idx == len) {
                    prev->next = val;
                    val->next = next;
                    arr->uni.ptr = val;
                } else {
                    while (idx-- > 0) {
                        prev = next;
                        next = next->next;
                    }
                    prev->next = val;
                    val->next = next;
                }
            }
            return true;
        }
    }
    return false;
}

styyjson_api_inline bool styyjson_mut_arr_append(styyjson_mut_val *arr,
                                             styyjson_mut_val *val) {
    if (styyjson_likely(styyjson_mut_is_arr(arr) && val)) {
        size_t len = unsafe_styyjson_get_len(arr);
        unsafe_styyjson_set_len(arr, len + 1);
        if (len == 0) {
            val->next = val;
        } else {
            styyjson_mut_val *prev = ((styyjson_mut_val *)arr->uni.ptr);
            styyjson_mut_val *next = prev->next;
            prev->next = val;
            val->next = next;
        }
        arr->uni.ptr = val;
        return true;
    }
    return false;
}

styyjson_api_inline bool styyjson_mut_arr_prepend(styyjson_mut_val *arr,
                                              styyjson_mut_val *val) {
    if (styyjson_likely(styyjson_mut_is_arr(arr) && val)) {
        size_t len = unsafe_styyjson_get_len(arr);
        unsafe_styyjson_set_len(arr, len + 1);
        if (len == 0) {
            val->next = val;
            arr->uni.ptr = val;
        } else {
            styyjson_mut_val *prev = ((styyjson_mut_val *)arr->uni.ptr);
            styyjson_mut_val *next = prev->next;
            prev->next = val;
            val->next = next;
        }
        return true;
    }
    return false;
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_replace(styyjson_mut_val *arr,
                                                         size_t idx,
                                                         styyjson_mut_val *val) {
    if (styyjson_likely(styyjson_mut_is_arr(arr) && val)) {
        size_t len = unsafe_styyjson_get_len(arr);
        if (styyjson_likely(idx < len)) {
            if (styyjson_likely(len > 1)) {
                styyjson_mut_val *prev = ((styyjson_mut_val *)arr->uni.ptr);
                styyjson_mut_val *next = prev->next;
                while (idx-- > 0) {
                    prev = next;
                    next = next->next;
                }
                prev->next = val;
                val->next = next->next;
                if ((void *)next == arr->uni.ptr) arr->uni.ptr = val;
                return next;
            } else {
                styyjson_mut_val *prev = ((styyjson_mut_val *)arr->uni.ptr);
                val->next = val;
                arr->uni.ptr = val;
                return prev;
            }
        }
    }
    return NULL;
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_remove(styyjson_mut_val *arr,
                                                        size_t idx) {
    if (styyjson_likely(styyjson_mut_is_arr(arr))) {
        size_t len = unsafe_styyjson_get_len(arr);
        if (styyjson_likely(idx < len)) {
            unsafe_styyjson_set_len(arr, len - 1);
            if (styyjson_likely(len > 1)) {
                styyjson_mut_val *prev = ((styyjson_mut_val *)arr->uni.ptr);
                styyjson_mut_val *next = prev->next;
                while (idx-- > 0) {
                    prev = next;
                    next = next->next;
                }
                prev->next = next->next;
                if ((void *)next == arr->uni.ptr) arr->uni.ptr = prev;
                return next;
            } else {
                return ((styyjson_mut_val *)arr->uni.ptr);
            }
        }
    }
    return NULL;
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_remove_first(
    styyjson_mut_val *arr) {
    if (styyjson_likely(styyjson_mut_is_arr(arr))) {
        size_t len = unsafe_styyjson_get_len(arr);
        if (len > 1) {
            styyjson_mut_val *prev = ((styyjson_mut_val *)arr->uni.ptr);
            styyjson_mut_val *next = prev->next;
            prev->next = next->next;
            unsafe_styyjson_set_len(arr, len - 1);
            return next;
        } else if (len == 1) {
            styyjson_mut_val *prev = ((styyjson_mut_val *)arr->uni.ptr);
            unsafe_styyjson_set_len(arr, 0);
            return prev;
        }
    }
    return NULL;
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_remove_last(
    styyjson_mut_val *arr) {
    if (styyjson_likely(styyjson_mut_is_arr(arr))) {
        size_t len = unsafe_styyjson_get_len(arr);
        if (styyjson_likely(len > 1)) {
            styyjson_mut_val *prev = ((styyjson_mut_val *)arr->uni.ptr);
            styyjson_mut_val *next = prev->next;
            unsafe_styyjson_set_len(arr, len - 1);
            while (--len > 0) prev = prev->next;
            prev->next = next;
            next = (styyjson_mut_val *)arr->uni.ptr;
            arr->uni.ptr = prev;
            return next;
        } else if (len == 1) {
            styyjson_mut_val *prev = ((styyjson_mut_val *)arr->uni.ptr);
            unsafe_styyjson_set_len(arr, 0);
            return prev;
        }
    }
    return NULL;
}

styyjson_api_inline bool styyjson_mut_arr_remove_range(styyjson_mut_val *arr,
                                                   size_t _idx, size_t _len) {
    if (styyjson_likely(styyjson_mut_is_arr(arr))) {
        styyjson_mut_val *prev, *next;
        bool tail_removed;
        size_t len = unsafe_styyjson_get_len(arr);
        if (styyjson_unlikely(_idx + _len > len)) return false;
        if (styyjson_unlikely(_len == 0)) return true;
        unsafe_styyjson_set_len(arr, len - _len);
        if (styyjson_unlikely(len == _len)) return true;
        tail_removed = (_idx + _len == len);
        prev = ((styyjson_mut_val *)arr->uni.ptr);
        while (_idx-- > 0) prev = prev->next;
        next = prev->next;
        while (_len-- > 0) next = next->next;
        prev->next = next;
        if (styyjson_unlikely(tail_removed)) arr->uni.ptr = prev;
        return true;
    }
    return false;
}

styyjson_api_inline bool styyjson_mut_arr_clear(styyjson_mut_val *arr) {
    if (styyjson_likely(styyjson_mut_is_arr(arr))) {
        unsafe_styyjson_set_len(arr, 0);
        return true;
    }
    return false;
}

styyjson_api_inline bool styyjson_mut_arr_rotate(styyjson_mut_val *arr,
                                             size_t idx) {
    if (styyjson_likely(styyjson_mut_is_arr(arr) &&
                      unsafe_styyjson_get_len(arr) > idx)) {
        styyjson_mut_val *val = (styyjson_mut_val *)arr->uni.ptr;
        while (idx-- > 0) val = val->next;
        arr->uni.ptr = (void *)val;
        return true;
    }
    return false;
}



/*==============================================================================
 * Mutable JSON Array Modification Convenience API (Implementation)
 *============================================================================*/

styyjson_api_inline bool styyjson_mut_arr_add_val(styyjson_mut_val *arr,
                                              styyjson_mut_val *val) {
    return styyjson_mut_arr_append(arr, val);
}

styyjson_api_inline bool styyjson_mut_arr_add_null(styyjson_mut_doc *doc,
                                               styyjson_mut_val *arr) {
    if (styyjson_likely(doc && styyjson_mut_is_arr(arr))) {
        styyjson_mut_val *val = styyjson_mut_null(doc);
        return styyjson_mut_arr_append(arr, val);
    }
    return false;
}

styyjson_api_inline bool styyjson_mut_arr_add_true(styyjson_mut_doc *doc,
                                               styyjson_mut_val *arr) {
    if (styyjson_likely(doc && styyjson_mut_is_arr(arr))) {
        styyjson_mut_val *val = styyjson_mut_true(doc);
        return styyjson_mut_arr_append(arr, val);
    }
    return false;
}

styyjson_api_inline bool styyjson_mut_arr_add_false(styyjson_mut_doc *doc,
                                                styyjson_mut_val *arr) {
    if (styyjson_likely(doc && styyjson_mut_is_arr(arr))) {
        styyjson_mut_val *val = styyjson_mut_false(doc);
        return styyjson_mut_arr_append(arr, val);
    }
    return false;
}

styyjson_api_inline bool styyjson_mut_arr_add_bool(styyjson_mut_doc *doc,
                                               styyjson_mut_val *arr,
                                               bool _val) {
    if (styyjson_likely(doc && styyjson_mut_is_arr(arr))) {
        styyjson_mut_val *val = styyjson_mut_bool(doc, _val);
        return styyjson_mut_arr_append(arr, val);
    }
    return false;
}

styyjson_api_inline bool styyjson_mut_arr_add_uint(styyjson_mut_doc *doc,
                                               styyjson_mut_val *arr,
                                               uint64_t num) {
    if (styyjson_likely(doc && styyjson_mut_is_arr(arr))) {
        styyjson_mut_val *val = styyjson_mut_uint(doc, num);
        return styyjson_mut_arr_append(arr, val);
    }
    return false;
}

styyjson_api_inline bool styyjson_mut_arr_add_sint(styyjson_mut_doc *doc,
                                               styyjson_mut_val *arr,
                                               int64_t num) {
    if (styyjson_likely(doc && styyjson_mut_is_arr(arr))) {
        styyjson_mut_val *val = styyjson_mut_sint(doc, num);
        return styyjson_mut_arr_append(arr, val);
    }
    return false;
}

styyjson_api_inline bool styyjson_mut_arr_add_int(styyjson_mut_doc *doc,
                                              styyjson_mut_val *arr,
                                              int64_t num) {
    if (styyjson_likely(doc && styyjson_mut_is_arr(arr))) {
        styyjson_mut_val *val = styyjson_mut_sint(doc, num);
        return styyjson_mut_arr_append(arr, val);
    }
    return false;
}

styyjson_api_inline bool styyjson_mut_arr_add_real(styyjson_mut_doc *doc,
                                               styyjson_mut_val *arr,
                                               double num) {
    if (styyjson_likely(doc && styyjson_mut_is_arr(arr))) {
        styyjson_mut_val *val = styyjson_mut_real(doc, num);
        return styyjson_mut_arr_append(arr, val);
    }
    return false;
}

styyjson_api_inline bool styyjson_mut_arr_add_str(styyjson_mut_doc *doc,
                                              styyjson_mut_val *arr,
                                              const char *str) {
    if (styyjson_likely(doc && styyjson_mut_is_arr(arr))) {
        styyjson_mut_val *val = styyjson_mut_str(doc, str);
        return styyjson_mut_arr_append(arr, val);
    }
    return false;
}

styyjson_api_inline bool styyjson_mut_arr_add_strn(styyjson_mut_doc *doc,
                                               styyjson_mut_val *arr,
                                               const char *str, size_t len) {
    if (styyjson_likely(doc && styyjson_mut_is_arr(arr))) {
        styyjson_mut_val *val = styyjson_mut_strn(doc, str, len);
        return styyjson_mut_arr_append(arr, val);
    }
    return false;
}

styyjson_api_inline bool styyjson_mut_arr_add_strcpy(styyjson_mut_doc *doc,
                                                 styyjson_mut_val *arr,
                                                 const char *str) {
    if (styyjson_likely(doc && styyjson_mut_is_arr(arr))) {
        styyjson_mut_val *val = styyjson_mut_strcpy(doc, str);
        return styyjson_mut_arr_append(arr, val);
    }
    return false;
}

styyjson_api_inline bool styyjson_mut_arr_add_strncpy(styyjson_mut_doc *doc,
                                                  styyjson_mut_val *arr,
                                                  const char *str, size_t len) {
    if (styyjson_likely(doc && styyjson_mut_is_arr(arr))) {
        styyjson_mut_val *val = styyjson_mut_strncpy(doc, str, len);
        return styyjson_mut_arr_append(arr, val);
    }
    return false;
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_add_arr(styyjson_mut_doc *doc,
                                                         styyjson_mut_val *arr) {
    if (styyjson_likely(doc && styyjson_mut_is_arr(arr))) {
        styyjson_mut_val *val = styyjson_mut_arr(doc);
        return styyjson_mut_arr_append(arr, val) ? val : NULL;
    }
    return NULL;
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_arr_add_obj(styyjson_mut_doc *doc,
                                                         styyjson_mut_val *arr) {
    if (styyjson_likely(doc && styyjson_mut_is_arr(arr))) {
        styyjson_mut_val *val = styyjson_mut_obj(doc);
        return styyjson_mut_arr_append(arr, val) ? val : NULL;
    }
    return NULL;
}



/*==============================================================================
 * Mutable JSON Object API (Implementation)
 *============================================================================*/

styyjson_api_inline size_t styyjson_mut_obj_size(styyjson_mut_val *obj) {
    return styyjson_mut_is_obj(obj) ? unsafe_styyjson_get_len(obj) : 0;
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_obj_get(styyjson_mut_val *obj,
                                                     const char *key) {
    return styyjson_mut_obj_getn(obj, key, key ? strlen(key) : 0);
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_obj_getn(styyjson_mut_val *obj,
                                                      const char *_key,
                                                      size_t key_len) {
    size_t len = styyjson_mut_obj_size(obj);
    if (styyjson_likely(len && _key)) {
        styyjson_mut_val *key = ((styyjson_mut_val *)obj->uni.ptr)->next->next;
        while (len-- > 0) {
            if (unsafe_styyjson_equals_strn(key, _key, key_len)) return key->next;
            key = key->next->next;
        }
    }
    return NULL;
}



/*==============================================================================
 * Mutable JSON Object Iterator API (Implementation)
 *============================================================================*/

styyjson_api_inline bool styyjson_mut_obj_iter_init(styyjson_mut_val *obj,
                                                styyjson_mut_obj_iter *iter) {
    if (styyjson_likely(styyjson_mut_is_obj(obj) && iter)) {
        iter->idx = 0;
        iter->max = unsafe_styyjson_get_len(obj);
        iter->cur = iter->max ? (styyjson_mut_val *)obj->uni.ptr : NULL;
        iter->pre = NULL;
        iter->obj = obj;
        return true;
    }
    if (iter) memset(iter, 0, sizeof(styyjson_mut_obj_iter));
    return false;
}

styyjson_api_inline styyjson_mut_obj_iter styyjson_mut_obj_iter_with(
    styyjson_mut_val *obj) {
    styyjson_mut_obj_iter iter;
    styyjson_mut_obj_iter_init(obj, &iter);
    return iter;
}

styyjson_api_inline bool styyjson_mut_obj_iter_has_next(styyjson_mut_obj_iter *iter) {
    return iter ? iter->idx < iter->max : false;
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_obj_iter_next(
    styyjson_mut_obj_iter *iter) {
    if (iter && iter->idx < iter->max) {
        styyjson_mut_val *key = iter->cur;
        iter->pre = key;
        iter->cur = key->next->next;
        iter->idx++;
        return iter->cur;
    }
    return NULL;
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_obj_iter_get_val(
    styyjson_mut_val *key) {
    return key ? key->next : NULL;
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_obj_iter_remove(
    styyjson_mut_obj_iter *iter) {
    if (styyjson_likely(iter && 0 < iter->idx && iter->idx <= iter->max)) {
        styyjson_mut_val *prev = iter->pre;
        styyjson_mut_val *cur = iter->cur;
        styyjson_mut_val *next = cur->next->next;
        if (styyjson_unlikely(iter->idx == iter->max)) iter->obj->uni.ptr = prev;
        iter->idx--;
        iter->max--;
        unsafe_styyjson_set_len(iter->obj, iter->max);
        prev->next->next = next;
        iter->cur = prev;
        return cur->next;
    }
    return NULL;
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_obj_iter_get(
    styyjson_mut_obj_iter *iter, const char *key) {
    return styyjson_mut_obj_iter_getn(iter, key, key ? strlen(key) : 0);
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_obj_iter_getn(
    styyjson_mut_obj_iter *iter, const char *key, size_t key_len) {
    if (iter && key) {
        size_t idx = 0;
        size_t max = iter->max;
        styyjson_mut_val *pre, *cur = iter->cur;
        while (idx++ < max) {
            pre = cur;
            cur = cur->next->next;
            if (unsafe_styyjson_equals_strn(cur, key, key_len)) {
                iter->idx += idx;
                if (iter->idx > max) iter->idx -= max + 1;
                iter->pre = pre;
                iter->cur = cur;
                return cur->next;
            }
        }
    }
    return NULL;
}



/*==============================================================================
 * Mutable JSON Object Creation API (Implementation)
 *============================================================================*/

styyjson_api_inline styyjson_mut_val *styyjson_mut_obj(styyjson_mut_doc *doc) {
    if (styyjson_likely(doc)) {
        styyjson_mut_val *val = unsafe_styyjson_mut_val(doc, 1);
        if (styyjson_likely(val)) {
            val->tag = YYJSON_TYPE_OBJ | YYJSON_SUBTYPE_NONE;
            return val;
        }
    }
    return NULL;
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_obj_with_str(styyjson_mut_doc *doc,
                                                          const char **keys,
                                                          const char **vals,
                                                          size_t count) {
    if (styyjson_likely(doc && ((count > 0 && keys && vals) || (count == 0)))) {
        styyjson_mut_val *obj = unsafe_styyjson_mut_val(doc, 1 + count * 2);
        if (styyjson_likely(obj)) {
            obj->tag = ((uint64_t)count << YYJSON_TAG_BIT) | YYJSON_TYPE_OBJ;
            if (count > 0) {
                size_t i;
                for (i = 0; i < count; i++) {
                    styyjson_mut_val *key = obj + (i * 2 + 1);
                    styyjson_mut_val *val = obj + (i * 2 + 2);
                    uint64_t key_len = (uint64_t)strlen(keys[i]);
                    uint64_t val_len = (uint64_t)strlen(vals[i]);
                    key->tag = (key_len << YYJSON_TAG_BIT) | YYJSON_TYPE_STR;
                    val->tag = (val_len << YYJSON_TAG_BIT) | YYJSON_TYPE_STR;
                    key->uni.str = keys[i];
                    val->uni.str = vals[i];
                    key->next = val;
                    val->next = val + 1;
                }
                obj[count * 2].next = obj + 1;
                obj->uni.ptr = obj + (count * 2 - 1);
            }
            return obj;
        }
    }
    return NULL;
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_obj_with_kv(styyjson_mut_doc *doc,
                                                         const char **pairs,
                                                         size_t count) {
    if (styyjson_likely(doc && ((count > 0 && pairs) || (count == 0)))) {
        styyjson_mut_val *obj = unsafe_styyjson_mut_val(doc, 1 + count * 2);
        if (styyjson_likely(obj)) {
            obj->tag = ((uint64_t)count << YYJSON_TAG_BIT) | YYJSON_TYPE_OBJ;
            if (count > 0) {
                size_t i;
                for (i = 0; i < count; i++) {
                    styyjson_mut_val *key = obj + (i * 2 + 1);
                    styyjson_mut_val *val = obj + (i * 2 + 2);
                    const char *key_str = pairs[i * 2 + 0];
                    const char *val_str = pairs[i * 2 + 1];
                    uint64_t key_len = (uint64_t)strlen(key_str);
                    uint64_t val_len = (uint64_t)strlen(val_str);
                    key->tag = (key_len << YYJSON_TAG_BIT) | YYJSON_TYPE_STR;
                    val->tag = (val_len << YYJSON_TAG_BIT) | YYJSON_TYPE_STR;
                    key->uni.str = key_str;
                    val->uni.str = val_str;
                    key->next = val;
                    val->next = val + 1;
                }
                obj[count * 2].next = obj + 1;
                obj->uni.ptr = obj + (count * 2 - 1);
            }
            return obj;
        }
    }
    return NULL;
}



/*==============================================================================
 * Mutable JSON Object Modification API (Implementation)
 *============================================================================*/

styyjson_api_inline void unsafe_styyjson_mut_obj_add(styyjson_mut_val *obj,
                                                 styyjson_mut_val *key,
                                                 styyjson_mut_val *val,
                                                 size_t len) {
    if (styyjson_likely(len)) {
        styyjson_mut_val *prev_val = ((styyjson_mut_val *)obj->uni.ptr)->next;
        styyjson_mut_val *next_key = prev_val->next;
        prev_val->next = key;
        val->next = next_key;
    } else {
        val->next = key;
    }
    key->next = val;
    obj->uni.ptr = (void *)key;
    unsafe_styyjson_set_len(obj, len + 1);
}

styyjson_api_inline styyjson_mut_val *unsafe_styyjson_mut_obj_remove(
    styyjson_mut_val *obj, const char *key, size_t key_len) {
    size_t obj_len = unsafe_styyjson_get_len(obj);
    if (obj_len) {
        styyjson_mut_val *pre_key = (styyjson_mut_val *)obj->uni.ptr;
        styyjson_mut_val *cur_key = pre_key->next->next;
        styyjson_mut_val *removed_item = NULL;
        size_t i;
        for (i = 0; i < obj_len; i++) {
            if (unsafe_styyjson_equals_strn(cur_key, key, key_len)) {
                if (!removed_item) removed_item = cur_key->next;
                cur_key = cur_key->next->next;
                pre_key->next->next = cur_key;
                if (i + 1 == obj_len) obj->uni.ptr = pre_key;
                i--;
                obj_len--;
            } else {
                pre_key = cur_key;
                cur_key = cur_key->next->next;
            }
        }
        unsafe_styyjson_set_len(obj, obj_len);
        return removed_item;
    } else {
        return NULL;
    }
}

styyjson_api_inline bool unsafe_styyjson_mut_obj_replace(styyjson_mut_val *obj,
                                                     styyjson_mut_val *key,
                                                     styyjson_mut_val *val) {
    size_t key_len = unsafe_styyjson_get_len(key);
    size_t obj_len = unsafe_styyjson_get_len(obj);
    if (obj_len) {
        styyjson_mut_val *pre_key = (styyjson_mut_val *)obj->uni.ptr;
        styyjson_mut_val *cur_key = pre_key->next->next;
        size_t i;
        for (i = 0; i < obj_len; i++) {
            if (unsafe_styyjson_equals_strn(cur_key, key->uni.str, key_len)) {
                cur_key->next->tag = val->tag;
                cur_key->next->uni.u64 = val->uni.u64;
                return true;
            } else {
                cur_key = cur_key->next->next;
            }
        }
    }
    return false;
}

styyjson_api_inline void unsafe_styyjson_mut_obj_rotate(styyjson_mut_val *obj,
                                                    size_t idx) {
    styyjson_mut_val *key = (styyjson_mut_val *)obj->uni.ptr;
    while (idx-- > 0) key = key->next->next;
    obj->uni.ptr = (void *)key;
}

styyjson_api_inline bool styyjson_mut_obj_add(styyjson_mut_val *obj,
                                          styyjson_mut_val *key,
                                          styyjson_mut_val *val) {
    if (styyjson_likely(styyjson_mut_is_obj(obj) &&
                      styyjson_mut_is_str(key) && val)) {
        unsafe_styyjson_mut_obj_add(obj, key, val, unsafe_styyjson_get_len(obj));
        return true;
    }
    return false;
}

styyjson_api_inline bool styyjson_mut_obj_put(styyjson_mut_val *obj,
                                          styyjson_mut_val *key,
                                          styyjson_mut_val *val) {
    bool replaced = false;
    size_t key_len;
    styyjson_mut_obj_iter iter;
    styyjson_mut_val *cur_key;
    if (styyjson_unlikely(!styyjson_mut_is_obj(obj) ||
                        !styyjson_mut_is_str(key))) return false;
    key_len = unsafe_styyjson_get_len(key);
    styyjson_mut_obj_iter_init(obj, &iter);
    while ((cur_key = styyjson_mut_obj_iter_next(&iter)) != 0) {
        if (unsafe_styyjson_equals_strn(cur_key, key->uni.str, key_len)) {
            if (!replaced && val) {
                replaced = true;
                val->next = cur_key->next->next;
                cur_key->next = val;
            } else {
                styyjson_mut_obj_iter_remove(&iter);
            }
        }
    }
    if (!replaced && val) unsafe_styyjson_mut_obj_add(obj, key, val, iter.max);
    return true;
}

styyjson_api_inline bool styyjson_mut_obj_insert(styyjson_mut_val *obj,
                                             styyjson_mut_val *key,
                                             styyjson_mut_val *val,
                                             size_t idx) {
    if (styyjson_likely(styyjson_mut_is_obj(obj) &&
                      styyjson_mut_is_str(key) && val)) {
        size_t len = unsafe_styyjson_get_len(obj);
        if (styyjson_likely(len >= idx)) {
            if (len > idx) {
                void *ptr = obj->uni.ptr;
                unsafe_styyjson_mut_obj_rotate(obj, idx);
                unsafe_styyjson_mut_obj_add(obj, key, val, len);
                obj->uni.ptr = ptr;
            } else {
                unsafe_styyjson_mut_obj_add(obj, key, val, len);
            }
            return true;
        }
    }
    return false;
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_obj_remove(styyjson_mut_val *obj,
    styyjson_mut_val *key) {
    if (styyjson_likely(styyjson_mut_is_obj(obj) && styyjson_mut_is_str(key))) {
        return unsafe_styyjson_mut_obj_remove(obj, key->uni.str,
                                            unsafe_styyjson_get_len(key));
    }
    return NULL;
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_obj_remove_key(
    styyjson_mut_val *obj, const char *key) {
    if (styyjson_likely(styyjson_mut_is_obj(obj) && key)) {
        size_t key_len = strlen(key);
        return unsafe_styyjson_mut_obj_remove(obj, key, key_len);
    }
    return NULL;
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_obj_remove_keyn(
    styyjson_mut_val *obj, const char *key, size_t key_len) {
    if (styyjson_likely(styyjson_mut_is_obj(obj) && key)) {
        return unsafe_styyjson_mut_obj_remove(obj, key, key_len);
    }
    return NULL;
}

styyjson_api_inline bool styyjson_mut_obj_clear(styyjson_mut_val *obj) {
    if (styyjson_likely(styyjson_mut_is_obj(obj))) {
        unsafe_styyjson_set_len(obj, 0);
        return true;
    }
    return false;
}

styyjson_api_inline bool styyjson_mut_obj_replace(styyjson_mut_val *obj,
                                              styyjson_mut_val *key,
                                              styyjson_mut_val *val) {
    if (styyjson_likely(styyjson_mut_is_obj(obj) &&
                      styyjson_mut_is_str(key) && val)) {
        return unsafe_styyjson_mut_obj_replace(obj, key, val);
    }
    return false;
}

styyjson_api_inline bool styyjson_mut_obj_rotate(styyjson_mut_val *obj,
                                             size_t idx) {
    if (styyjson_likely(styyjson_mut_is_obj(obj) &&
                      unsafe_styyjson_get_len(obj) > idx)) {
        unsafe_styyjson_mut_obj_rotate(obj, idx);
        return true;
    }
    return false;
}



/*==============================================================================
 * Mutable JSON Object Modification Convenience API (Implementation)
 *============================================================================*/

#define styyjson_mut_obj_add_func(func) \
    if (styyjson_likely(doc && styyjson_mut_is_obj(obj) && _key)) { \
        styyjson_mut_val *key = unsafe_styyjson_mut_val(doc, 2); \
        if (styyjson_likely(key)) { \
            size_t len = unsafe_styyjson_get_len(obj); \
            styyjson_mut_val *val = key + 1; \
            size_t key_len = strlen(_key); \
            bool noesc = unsafe_styyjson_is_str_noesc(_key, key_len); \
            key->tag = YYJSON_TYPE_STR; \
            key->tag |= noesc ? YYJSON_SUBTYPE_NOESC : YYJSON_SUBTYPE_NONE; \
            key->tag |= (uint64_t)strlen(_key) << YYJSON_TAG_BIT; \
            key->uni.str = _key; \
            func \
            unsafe_styyjson_mut_obj_add(obj, key, val, len); \
            return true; \
        } \
    } \
    return false

styyjson_api_inline bool styyjson_mut_obj_add_null(styyjson_mut_doc *doc,
                                               styyjson_mut_val *obj,
                                               const char *_key) {
    styyjson_mut_obj_add_func({
        val->tag = YYJSON_TYPE_NULL | YYJSON_SUBTYPE_NONE;
    });
}

styyjson_api_inline bool styyjson_mut_obj_add_true(styyjson_mut_doc *doc,
                                               styyjson_mut_val *obj,
                                               const char *_key) {
    styyjson_mut_obj_add_func({
        val->tag = YYJSON_TYPE_BOOL | YYJSON_SUBTYPE_TRUE;
    });
}

styyjson_api_inline bool styyjson_mut_obj_add_false(styyjson_mut_doc *doc,
                                                styyjson_mut_val *obj,
                                                const char *_key) {
    styyjson_mut_obj_add_func({
        val->tag = YYJSON_TYPE_BOOL | YYJSON_SUBTYPE_FALSE;
    });
}

styyjson_api_inline bool styyjson_mut_obj_add_bool(styyjson_mut_doc *doc,
                                               styyjson_mut_val *obj,
                                               const char *_key,
                                               bool _val) {
    styyjson_mut_obj_add_func({
        _val = !!_val;
        val->tag = YYJSON_TYPE_BOOL | (uint8_t)((uint8_t)(_val) << 3);
    });
}

styyjson_api_inline bool styyjson_mut_obj_add_uint(styyjson_mut_doc *doc,
                                               styyjson_mut_val *obj,
                                               const char *_key,
                                               uint64_t _val) {
    styyjson_mut_obj_add_func({
        val->tag = YYJSON_TYPE_NUM | YYJSON_SUBTYPE_UINT;
        val->uni.u64 = _val;
    });
}

styyjson_api_inline bool styyjson_mut_obj_add_sint(styyjson_mut_doc *doc,
                                               styyjson_mut_val *obj,
                                               const char *_key,
                                               int64_t _val) {
    styyjson_mut_obj_add_func({
        val->tag = YYJSON_TYPE_NUM | YYJSON_SUBTYPE_SINT;
        val->uni.i64 = _val;
    });
}

styyjson_api_inline bool styyjson_mut_obj_add_int(styyjson_mut_doc *doc,
                                              styyjson_mut_val *obj,
                                              const char *_key,
                                              int64_t _val) {
    styyjson_mut_obj_add_func({
        val->tag = YYJSON_TYPE_NUM | YYJSON_SUBTYPE_SINT;
        val->uni.i64 = _val;
    });
}

styyjson_api_inline bool styyjson_mut_obj_add_real(styyjson_mut_doc *doc,
                                               styyjson_mut_val *obj,
                                               const char *_key,
                                               double _val) {
    styyjson_mut_obj_add_func({
        val->tag = YYJSON_TYPE_NUM | YYJSON_SUBTYPE_REAL;
        val->uni.f64 = _val;
    });
}

styyjson_api_inline bool styyjson_mut_obj_add_str(styyjson_mut_doc *doc,
                                              styyjson_mut_val *obj,
                                              const char *_key,
                                              const char *_val) {
    if (styyjson_unlikely(!_val)) return false;
    styyjson_mut_obj_add_func({
        size_t val_len = strlen(_val);
        bool val_noesc = unsafe_styyjson_is_str_noesc(_val, val_len);
        val->tag = ((uint64_t)strlen(_val) << YYJSON_TAG_BIT) | YYJSON_TYPE_STR;
        val->tag |= val_noesc ? YYJSON_SUBTYPE_NOESC : YYJSON_SUBTYPE_NONE;
        val->uni.str = _val;
    });
}

styyjson_api_inline bool styyjson_mut_obj_add_strn(styyjson_mut_doc *doc,
                                               styyjson_mut_val *obj,
                                               const char *_key,
                                               const char *_val,
                                               size_t _len) {
    if (styyjson_unlikely(!_val)) return false;
    styyjson_mut_obj_add_func({
        val->tag = ((uint64_t)_len << YYJSON_TAG_BIT) | YYJSON_TYPE_STR;
        val->uni.str = _val;
    });
}

styyjson_api_inline bool styyjson_mut_obj_add_strcpy(styyjson_mut_doc *doc,
                                                 styyjson_mut_val *obj,
                                                 const char *_key,
                                                 const char *_val) {
    if (styyjson_unlikely(!_val)) return false;
    styyjson_mut_obj_add_func({
        size_t _len = strlen(_val);
        val->uni.str = unsafe_styyjson_mut_strncpy(doc, _val, _len);
        if (styyjson_unlikely(!val->uni.str)) return false;
        val->tag = ((uint64_t)_len << YYJSON_TAG_BIT) | YYJSON_TYPE_STR;
    });
}

styyjson_api_inline bool styyjson_mut_obj_add_strncpy(styyjson_mut_doc *doc,
                                                  styyjson_mut_val *obj,
                                                  const char *_key,
                                                  const char *_val,
                                                  size_t _len) {
    if (styyjson_unlikely(!_val)) return false;
    styyjson_mut_obj_add_func({
        val->uni.str = unsafe_styyjson_mut_strncpy(doc, _val, _len);
        if (styyjson_unlikely(!val->uni.str)) return false;
        val->tag = ((uint64_t)_len << YYJSON_TAG_BIT) | YYJSON_TYPE_STR;
    });
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_obj_add_arr(styyjson_mut_doc *doc,
                                                         styyjson_mut_val *obj,
                                                         const char *_key) {
    styyjson_mut_val *key = styyjson_mut_str(doc, _key);
    styyjson_mut_val *val = styyjson_mut_arr(doc);
    return styyjson_mut_obj_add(obj, key, val) ? val : NULL;
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_obj_add_obj(styyjson_mut_doc *doc,
                                                         styyjson_mut_val *obj,
                                                         const char *_key) {
    styyjson_mut_val *key = styyjson_mut_str(doc, _key);
    styyjson_mut_val *val = styyjson_mut_obj(doc);
    return styyjson_mut_obj_add(obj, key, val) ? val : NULL;
}

styyjson_api_inline bool styyjson_mut_obj_add_val(styyjson_mut_doc *doc,
                                              styyjson_mut_val *obj,
                                              const char *_key,
                                              styyjson_mut_val *_val) {
    if (styyjson_unlikely(!_val)) return false;
    styyjson_mut_obj_add_func({
        val = _val;
    });
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_obj_remove_str(styyjson_mut_val *obj,
                                                            const char *key) {
    return styyjson_mut_obj_remove_strn(obj, key, key ? strlen(key) : 0);
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_obj_remove_strn(
    styyjson_mut_val *obj, const char *_key, size_t _len) {
    if (styyjson_likely(styyjson_mut_is_obj(obj) && _key)) {
        styyjson_mut_val *key;
        styyjson_mut_obj_iter iter;
        styyjson_mut_val *val_removed = NULL;
        styyjson_mut_obj_iter_init(obj, &iter);
        while ((key = styyjson_mut_obj_iter_next(&iter)) != NULL) {
            if (unsafe_styyjson_equals_strn(key, _key, _len)) {
                if (!val_removed) val_removed = key->next;
                styyjson_mut_obj_iter_remove(&iter);
            }
        }
        return val_removed;
    }
    return NULL;
}

styyjson_api_inline bool styyjson_mut_obj_rename_key(styyjson_mut_doc *doc,
                                                 styyjson_mut_val *obj,
                                                 const char *key,
                                                 const char *new_key) {
    if (!key || !new_key) return false;
    return styyjson_mut_obj_rename_keyn(doc, obj, key, strlen(key),
                                      new_key, strlen(new_key));
}

styyjson_api_inline bool styyjson_mut_obj_rename_keyn(styyjson_mut_doc *doc,
                                                  styyjson_mut_val *obj,
                                                  const char *key,
                                                  size_t len,
                                                  const char *new_key,
                                                  size_t new_len) {
    char *cpy_key = NULL;
    styyjson_mut_val *old_key;
    styyjson_mut_obj_iter iter;
    if (!doc || !obj || !key || !new_key) return false;
    styyjson_mut_obj_iter_init(obj, &iter);
    while ((old_key = styyjson_mut_obj_iter_next(&iter))) {
        if (unsafe_styyjson_equals_strn((void *)old_key, key, len)) {
            if (!cpy_key) {
                cpy_key = unsafe_styyjson_mut_strncpy(doc, new_key, new_len);
                if (!cpy_key) return false;
            }
            styyjson_mut_set_strn(old_key, cpy_key, new_len);
        }
    }
    return cpy_key != NULL;
}



/*==============================================================================
 * JSON Pointer API (Implementation)
 *============================================================================*/

#define styyjson_ptr_set_err(_code, _msg) do { \
    if (err) { \
        err->code = YYJSON_PTR_ERR_##_code; \
        err->msg = _msg; \
        err->pos = 0; \
    } \
} while(false)

/* require: val != NULL, *ptr == '/', len > 0 */
styyjson_api styyjson_val *unsafe_styyjson_ptr_getx(styyjson_val *val,
                                              const char *ptr, size_t len,
                                              styyjson_ptr_err *err);

/* require: val != NULL, *ptr == '/', len > 0 */
styyjson_api styyjson_mut_val *unsafe_styyjson_mut_ptr_getx(styyjson_mut_val *val,
                                                      const char *ptr,
                                                      size_t len,
                                                      styyjson_ptr_ctx *ctx,
                                                      styyjson_ptr_err *err);

/* require: val/new_val/doc != NULL, *ptr == '/', len > 0 */
styyjson_api bool unsafe_styyjson_mut_ptr_putx(styyjson_mut_val *val,
                                           const char *ptr, size_t len,
                                           styyjson_mut_val *new_val,
                                           styyjson_mut_doc *doc,
                                           bool create_parent, bool insert_new,
                                           styyjson_ptr_ctx *ctx,
                                           styyjson_ptr_err *err);

/* require: val/err != NULL, *ptr == '/', len > 0 */
styyjson_api styyjson_mut_val *unsafe_styyjson_mut_ptr_replacex(
    styyjson_mut_val *val, const char *ptr, size_t len, styyjson_mut_val *new_val,
    styyjson_ptr_ctx *ctx, styyjson_ptr_err *err);

/* require: val/err != NULL, *ptr == '/', len > 0 */
styyjson_api styyjson_mut_val *unsafe_styyjson_mut_ptr_removex(styyjson_mut_val *val,
                                                         const char *ptr,
                                                         size_t len,
                                                         styyjson_ptr_ctx *ctx,
                                                         styyjson_ptr_err *err);

styyjson_api_inline styyjson_val *styyjson_doc_ptr_get(styyjson_doc *doc,
                                                 const char *ptr) {
    if (styyjson_unlikely(!ptr)) return NULL;
    return styyjson_doc_ptr_getn(doc, ptr, strlen(ptr));
}

styyjson_api_inline styyjson_val *styyjson_doc_ptr_getn(styyjson_doc *doc,
                                                  const char *ptr, size_t len) {
    return styyjson_doc_ptr_getx(doc, ptr, len, NULL);
}

styyjson_api_inline styyjson_val *styyjson_doc_ptr_getx(styyjson_doc *doc,
                                                  const char *ptr, size_t len,
                                                  styyjson_ptr_err *err) {
    styyjson_ptr_set_err(NONE, NULL);
    if (styyjson_unlikely(!doc || !ptr)) {
        styyjson_ptr_set_err(PARAMETER, "input parameter is NULL");
        return NULL;
    }
    if (styyjson_unlikely(!doc->root)) {
        styyjson_ptr_set_err(NULL_ROOT, "document's root is NULL");
        return NULL;
    }
    if (styyjson_unlikely(len == 0)) {
        return doc->root;
    }
    if (styyjson_unlikely(*ptr != '/')) {
        styyjson_ptr_set_err(SYNTAX, "no prefix '/'");
        return NULL;
    }
    return unsafe_styyjson_ptr_getx(doc->root, ptr, len, err);
}

styyjson_api_inline styyjson_val *styyjson_ptr_get(styyjson_val *val,
                                             const char *ptr) {
    if (styyjson_unlikely(!ptr)) return NULL;
    return styyjson_ptr_getn(val, ptr, strlen(ptr));
}

styyjson_api_inline styyjson_val *styyjson_ptr_getn(styyjson_val *val,
                                              const char *ptr, size_t len) {
    return styyjson_ptr_getx(val, ptr, len, NULL);
}

styyjson_api_inline styyjson_val *styyjson_ptr_getx(styyjson_val *val,
                                              const char *ptr, size_t len,
                                              styyjson_ptr_err *err) {
    styyjson_ptr_set_err(NONE, NULL);
    if (styyjson_unlikely(!val || !ptr)) {
        styyjson_ptr_set_err(PARAMETER, "input parameter is NULL");
        return NULL;
    }
    if (styyjson_unlikely(len == 0)) {
        return val;
    }
    if (styyjson_unlikely(*ptr != '/')) {
        styyjson_ptr_set_err(SYNTAX, "no prefix '/'");
        return NULL;
    }
    return unsafe_styyjson_ptr_getx(val, ptr, len, err);
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_doc_ptr_get(styyjson_mut_doc *doc,
                                                         const char *ptr) {
    if (!ptr) return NULL;
    return styyjson_mut_doc_ptr_getn(doc, ptr, strlen(ptr));
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_doc_ptr_getn(styyjson_mut_doc *doc,
                                                          const char *ptr,
                                                          size_t len) {
    return styyjson_mut_doc_ptr_getx(doc, ptr, len, NULL, NULL);
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_doc_ptr_getx(styyjson_mut_doc *doc,
                                                          const char *ptr,
                                                          size_t len,
                                                          styyjson_ptr_ctx *ctx,
                                                          styyjson_ptr_err *err) {
    styyjson_ptr_set_err(NONE, NULL);
    if (ctx) memset(ctx, 0, sizeof(*ctx));
    
    if (styyjson_unlikely(!doc || !ptr)) {
        styyjson_ptr_set_err(PARAMETER, "input parameter is NULL");
        return NULL;
    }
    if (styyjson_unlikely(!doc->root)) {
        styyjson_ptr_set_err(NULL_ROOT, "document's root is NULL");
        return NULL;
    }
    if (styyjson_unlikely(len == 0)) {
        return doc->root;
    }
    if (styyjson_unlikely(*ptr != '/')) {
        styyjson_ptr_set_err(SYNTAX, "no prefix '/'");
        return NULL;
    }
    return unsafe_styyjson_mut_ptr_getx(doc->root, ptr, len, ctx, err);
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_ptr_get(styyjson_mut_val *val,
                                                     const char *ptr) {
    if (!ptr) return NULL;
    return styyjson_mut_ptr_getn(val, ptr, strlen(ptr));
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_ptr_getn(styyjson_mut_val *val,
                                                      const char *ptr,
                                                      size_t len) {
    return styyjson_mut_ptr_getx(val, ptr, len, NULL, NULL);
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_ptr_getx(styyjson_mut_val *val,
                                                      const char *ptr,
                                                      size_t len,
                                                      styyjson_ptr_ctx *ctx,
                                                      styyjson_ptr_err *err) {
    styyjson_ptr_set_err(NONE, NULL);
    if (ctx) memset(ctx, 0, sizeof(*ctx));
    
    if (styyjson_unlikely(!val || !ptr)) {
        styyjson_ptr_set_err(PARAMETER, "input parameter is NULL");
        return NULL;
    }
    if (styyjson_unlikely(len == 0)) {
        return val;
    }
    if (styyjson_unlikely(*ptr != '/')) {
        styyjson_ptr_set_err(SYNTAX, "no prefix '/'");
        return NULL;
    }
    return unsafe_styyjson_mut_ptr_getx(val, ptr, len, ctx, err);
}

styyjson_api_inline bool styyjson_mut_doc_ptr_add(styyjson_mut_doc *doc,
                                              const char *ptr,
                                              styyjson_mut_val *new_val) {
    if (styyjson_unlikely(!ptr)) return false;
    return styyjson_mut_doc_ptr_addn(doc, ptr, strlen(ptr), new_val);
}

styyjson_api_inline bool styyjson_mut_doc_ptr_addn(styyjson_mut_doc *doc,
                                               const char *ptr,
                                               size_t len,
                                               styyjson_mut_val *new_val) {
    return styyjson_mut_doc_ptr_addx(doc, ptr, len, new_val, true, NULL, NULL);
}

styyjson_api_inline bool styyjson_mut_doc_ptr_addx(styyjson_mut_doc *doc,
                                               const char *ptr, size_t len,
                                               styyjson_mut_val *new_val,
                                               bool create_parent,
                                               styyjson_ptr_ctx *ctx,
                                               styyjson_ptr_err *err) {
    styyjson_ptr_set_err(NONE, NULL);
    if (ctx) memset(ctx, 0, sizeof(*ctx));
    
    if (styyjson_unlikely(!doc || !ptr || !new_val)) {
        styyjson_ptr_set_err(PARAMETER, "input parameter is NULL");
        return false;
    }
    if (styyjson_unlikely(len == 0)) {
        if (doc->root) {
            styyjson_ptr_set_err(SET_ROOT, "cannot set document's root");
            return false;
        } else {
            doc->root = new_val;
            return true;
        }
    }
    if (styyjson_unlikely(*ptr != '/')) {
        styyjson_ptr_set_err(SYNTAX, "no prefix '/'");
        return false;
    }
    if (styyjson_unlikely(!doc->root && !create_parent)) {
        styyjson_ptr_set_err(NULL_ROOT, "document's root is NULL");
        return false;
    }
    if (styyjson_unlikely(!doc->root)) {
        styyjson_mut_val *root = styyjson_mut_obj(doc);
        if (styyjson_unlikely(!root)) {
            styyjson_ptr_set_err(MEMORY_ALLOCATION, "failed to create value");
            return false;
        }
        if (unsafe_styyjson_mut_ptr_putx(root, ptr, len, new_val, doc,
                                       create_parent, true, ctx, err)) {
            doc->root = root;
            return true;
        }
        return false;
    }
    return unsafe_styyjson_mut_ptr_putx(doc->root, ptr, len, new_val, doc,
                                      create_parent, true, ctx, err);
}

styyjson_api_inline bool styyjson_mut_ptr_add(styyjson_mut_val *val,
                                          const char *ptr,
                                          styyjson_mut_val *new_val,
                                          styyjson_mut_doc *doc) {
    if (styyjson_unlikely(!ptr)) return false;
    return styyjson_mut_ptr_addn(val, ptr, strlen(ptr), new_val, doc);
}

styyjson_api_inline bool styyjson_mut_ptr_addn(styyjson_mut_val *val,
                                           const char *ptr, size_t len,
                                           styyjson_mut_val *new_val,
                                           styyjson_mut_doc *doc) {
    return styyjson_mut_ptr_addx(val, ptr, len, new_val, doc, true, NULL, NULL);
}

styyjson_api_inline bool styyjson_mut_ptr_addx(styyjson_mut_val *val,
                                           const char *ptr, size_t len,
                                           styyjson_mut_val *new_val,
                                           styyjson_mut_doc *doc,
                                           bool create_parent,
                                           styyjson_ptr_ctx *ctx,
                                           styyjson_ptr_err *err) {
    styyjson_ptr_set_err(NONE, NULL);
    if (ctx) memset(ctx, 0, sizeof(*ctx));
    
    if (styyjson_unlikely(!val || !ptr || !new_val || !doc)) {
        styyjson_ptr_set_err(PARAMETER, "input parameter is NULL");
        return false;
    }
    if (styyjson_unlikely(len == 0)) {
        styyjson_ptr_set_err(SET_ROOT, "cannot set root");
        return false;
    }
    if (styyjson_unlikely(*ptr != '/')) {
        styyjson_ptr_set_err(SYNTAX, "no prefix '/'");
        return false;
    }
    return unsafe_styyjson_mut_ptr_putx(val, ptr, len, new_val,
                                       doc, create_parent, true, ctx, err);
}

styyjson_api_inline bool styyjson_mut_doc_ptr_set(styyjson_mut_doc *doc,
                                              const char *ptr,
                                              styyjson_mut_val *new_val) {
    if (styyjson_unlikely(!ptr)) return false;
    return styyjson_mut_doc_ptr_setn(doc, ptr, strlen(ptr), new_val);
}

styyjson_api_inline bool styyjson_mut_doc_ptr_setn(styyjson_mut_doc *doc,
                                               const char *ptr, size_t len,
                                               styyjson_mut_val *new_val) {
    return styyjson_mut_doc_ptr_setx(doc, ptr, len, new_val, true, NULL, NULL);
}

styyjson_api_inline bool styyjson_mut_doc_ptr_setx(styyjson_mut_doc *doc,
                                               const char *ptr, size_t len,
                                               styyjson_mut_val *new_val,
                                               bool create_parent,
                                               styyjson_ptr_ctx *ctx,
                                               styyjson_ptr_err *err) {
    styyjson_ptr_set_err(NONE, NULL);
    if (ctx) memset(ctx, 0, sizeof(*ctx));
    
    if (styyjson_unlikely(!doc || !ptr)) {
        styyjson_ptr_set_err(PARAMETER, "input parameter is NULL");
        return false;
    }
    if (styyjson_unlikely(len == 0)) {
        if (ctx) ctx->old = doc->root;
        doc->root = new_val;
        return true;
    }
    if (styyjson_unlikely(*ptr != '/')) {
        styyjson_ptr_set_err(SYNTAX, "no prefix '/'");
        return false;
    }
    if (!new_val) {
        if (!doc->root) {
            styyjson_ptr_set_err(RESOLVE, "JSON pointer cannot be resolved");
            return false;
        }
        return !!unsafe_styyjson_mut_ptr_removex(doc->root, ptr, len, ctx, err);
    }
    if (styyjson_unlikely(!doc->root && !create_parent)) {
        styyjson_ptr_set_err(NULL_ROOT, "document's root is NULL");
        return false;
    }
    if (styyjson_unlikely(!doc->root)) {
        styyjson_mut_val *root = styyjson_mut_obj(doc);
        if (styyjson_unlikely(!root)) {
            styyjson_ptr_set_err(MEMORY_ALLOCATION, "failed to create value");
            return false;
        }
        if (unsafe_styyjson_mut_ptr_putx(root, ptr, len, new_val, doc,
                                       create_parent, false, ctx, err)) {
            doc->root = root;
            return true;
        }
        return false;
    }
    return unsafe_styyjson_mut_ptr_putx(doc->root, ptr, len, new_val, doc,
                                      create_parent, false, ctx, err);
}

styyjson_api_inline bool styyjson_mut_ptr_set(styyjson_mut_val *val,
                                          const char *ptr,
                                          styyjson_mut_val *new_val,
                                          styyjson_mut_doc *doc) {
    if (styyjson_unlikely(!ptr)) return false;
    return styyjson_mut_ptr_setn(val, ptr, strlen(ptr), new_val, doc);
}

styyjson_api_inline bool styyjson_mut_ptr_setn(styyjson_mut_val *val,
                                           const char *ptr, size_t len,
                                           styyjson_mut_val *new_val,
                                           styyjson_mut_doc *doc) {
    return styyjson_mut_ptr_setx(val, ptr, len, new_val, doc, true, NULL, NULL);
}

styyjson_api_inline bool styyjson_mut_ptr_setx(styyjson_mut_val *val,
                                           const char *ptr, size_t len,
                                           styyjson_mut_val *new_val,
                                           styyjson_mut_doc *doc,
                                           bool create_parent,
                                           styyjson_ptr_ctx *ctx,
                                           styyjson_ptr_err *err) {
    styyjson_ptr_set_err(NONE, NULL);
    if (ctx) memset(ctx, 0, sizeof(*ctx));
    
    if (styyjson_unlikely(!val || !ptr || !doc)) {
        styyjson_ptr_set_err(PARAMETER, "input parameter is NULL");
        return false;
    }
    if (styyjson_unlikely(len == 0)) {
        styyjson_ptr_set_err(SET_ROOT, "cannot set root");
        return false;
    }
    if (styyjson_unlikely(*ptr != '/')) {
        styyjson_ptr_set_err(SYNTAX, "no prefix '/'");
        return false;
    }
    if (!new_val) {
        return !!unsafe_styyjson_mut_ptr_removex(val, ptr, len, ctx, err);
    }
    return unsafe_styyjson_mut_ptr_putx(val, ptr, len, new_val, doc,
                                      create_parent, false, ctx, err);
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_doc_ptr_replace(
    styyjson_mut_doc *doc, const char *ptr, styyjson_mut_val *new_val) {
    if (!ptr) return NULL;
    return styyjson_mut_doc_ptr_replacen(doc, ptr, strlen(ptr), new_val);
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_doc_ptr_replacen(
    styyjson_mut_doc *doc, const char *ptr, size_t len, styyjson_mut_val *new_val) {
    return styyjson_mut_doc_ptr_replacex(doc, ptr, len, new_val, NULL, NULL);
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_doc_ptr_replacex(
    styyjson_mut_doc *doc, const char *ptr, size_t len, styyjson_mut_val *new_val,
    styyjson_ptr_ctx *ctx, styyjson_ptr_err *err) {
    
    styyjson_ptr_set_err(NONE, NULL);
    if (ctx) memset(ctx, 0, sizeof(*ctx));
    
    if (styyjson_unlikely(!doc || !ptr || !new_val)) {
        styyjson_ptr_set_err(PARAMETER, "input parameter is NULL");
        return NULL;
    }
    if (styyjson_unlikely(len == 0)) {
        styyjson_mut_val *root = doc->root;
        if (styyjson_unlikely(!root)) {
            styyjson_ptr_set_err(RESOLVE, "JSON pointer cannot be resolved");
            return NULL;
        }
        if (ctx) ctx->old = root;
        doc->root = new_val;
        return root;
    }
    if (styyjson_unlikely(!doc->root)) {
        styyjson_ptr_set_err(NULL_ROOT, "document's root is NULL");
        return NULL;
    }
    if (styyjson_unlikely(*ptr != '/')) {
        styyjson_ptr_set_err(SYNTAX, "no prefix '/'");
        return NULL;
    }
    return unsafe_styyjson_mut_ptr_replacex(doc->root, ptr, len, new_val,
                                          ctx, err);
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_ptr_replace(
    styyjson_mut_val *val, const char *ptr, styyjson_mut_val *new_val) {
    if (!ptr) return NULL;
    return styyjson_mut_ptr_replacen(val, ptr, strlen(ptr), new_val);
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_ptr_replacen(
    styyjson_mut_val *val, const char *ptr, size_t len, styyjson_mut_val *new_val) {
    return styyjson_mut_ptr_replacex(val, ptr, len, new_val, NULL, NULL);
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_ptr_replacex(
    styyjson_mut_val *val, const char *ptr, size_t len, styyjson_mut_val *new_val,
    styyjson_ptr_ctx *ctx, styyjson_ptr_err *err) {
    
    styyjson_ptr_set_err(NONE, NULL);
    if (ctx) memset(ctx, 0, sizeof(*ctx));
    
    if (styyjson_unlikely(!val || !ptr || !new_val)) {
        styyjson_ptr_set_err(PARAMETER, "input parameter is NULL");
        return NULL;
    }
    if (styyjson_unlikely(len == 0)) {
        styyjson_ptr_set_err(SET_ROOT, "cannot set root");
        return NULL;
    }
    if (styyjson_unlikely(*ptr != '/')) {
        styyjson_ptr_set_err(SYNTAX, "no prefix '/'");
        return NULL;
    }
    return unsafe_styyjson_mut_ptr_replacex(val, ptr, len, new_val, ctx, err);
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_doc_ptr_remove(
    styyjson_mut_doc *doc, const char *ptr) {
    if (!ptr) return NULL;
    return styyjson_mut_doc_ptr_removen(doc, ptr, strlen(ptr));
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_doc_ptr_removen(
    styyjson_mut_doc *doc, const char *ptr, size_t len) {
    return styyjson_mut_doc_ptr_removex(doc, ptr, len, NULL, NULL);
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_doc_ptr_removex(
    styyjson_mut_doc *doc, const char *ptr, size_t len,
    styyjson_ptr_ctx *ctx, styyjson_ptr_err *err) {
    
    styyjson_ptr_set_err(NONE, NULL);
    if (ctx) memset(ctx, 0, sizeof(*ctx));
    
    if (styyjson_unlikely(!doc || !ptr)) {
        styyjson_ptr_set_err(PARAMETER, "input parameter is NULL");
        return NULL;
    }
    if (styyjson_unlikely(!doc->root)) {
        styyjson_ptr_set_err(NULL_ROOT, "document's root is NULL");
        return NULL;
    }
    if (styyjson_unlikely(len == 0)) {
        styyjson_mut_val *root = doc->root;
        if (ctx) ctx->old = root;
        doc->root = NULL;
        return root;
    }
    if (styyjson_unlikely(*ptr != '/')) {
        styyjson_ptr_set_err(SYNTAX, "no prefix '/'");
        return NULL;
    }
    return unsafe_styyjson_mut_ptr_removex(doc->root, ptr, len, ctx, err);
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_ptr_remove(styyjson_mut_val *val,
                                                        const char *ptr) {
    if (!ptr) return NULL;
    return styyjson_mut_ptr_removen(val, ptr, strlen(ptr));
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_ptr_removen(styyjson_mut_val *val,
                                                         const char *ptr,
                                                         size_t len) {
    return styyjson_mut_ptr_removex(val, ptr, len, NULL, NULL);
}

styyjson_api_inline styyjson_mut_val *styyjson_mut_ptr_removex(styyjson_mut_val *val,
                                                         const char *ptr,
                                                         size_t len,
                                                         styyjson_ptr_ctx *ctx,
                                                         styyjson_ptr_err *err) {
    styyjson_ptr_set_err(NONE, NULL);
    if (ctx) memset(ctx, 0, sizeof(*ctx));
    
    if (styyjson_unlikely(!val || !ptr)) {
        styyjson_ptr_set_err(PARAMETER, "input parameter is NULL");
        return NULL;
    }
    if (styyjson_unlikely(len == 0)) {
        styyjson_ptr_set_err(SET_ROOT, "cannot set root");
        return NULL;
    }
    if (styyjson_unlikely(*ptr != '/')) {
        styyjson_ptr_set_err(SYNTAX, "no prefix '/'");
        return NULL;
    }
    return unsafe_styyjson_mut_ptr_removex(val, ptr, len, ctx, err);
}

styyjson_api_inline bool styyjson_ptr_ctx_append(styyjson_ptr_ctx *ctx,
                                             styyjson_mut_val *key,
                                             styyjson_mut_val *val) {
    styyjson_mut_val *ctn, *pre_key, *pre_val, *cur_key, *cur_val;
    if (!ctx || !ctx->ctn || !val) return false;
    ctn = ctx->ctn;
    
    if (styyjson_mut_is_obj(ctn)) {
        if (!key) return false;
        key->next = val;
        pre_key = ctx->pre;
        if (unsafe_styyjson_get_len(ctn) == 0) {
            val->next = key;
            ctn->uni.ptr = key;
            ctx->pre = key;
        } else if (!pre_key) {
            pre_key = (styyjson_mut_val *)ctn->uni.ptr;
            pre_val = pre_key->next;
            val->next = pre_val->next;
            pre_val->next = key;
            ctn->uni.ptr = key;
            ctx->pre = pre_key;
        } else {
            cur_key = pre_key->next->next;
            cur_val = cur_key->next;
            val->next = cur_val->next;
            cur_val->next = key;
            if (ctn->uni.ptr == cur_key) ctn->uni.ptr = key;
            ctx->pre = cur_key;
        }
    } else {
        pre_val = ctx->pre;
        if (unsafe_styyjson_get_len(ctn) == 0) {
            val->next = val;
            ctn->uni.ptr = val;
            ctx->pre = val;
        } else if (!pre_val) {
            pre_val = (styyjson_mut_val *)ctn->uni.ptr;
            val->next = pre_val->next;
            pre_val->next = val;
            ctn->uni.ptr = val;
            ctx->pre = pre_val;
        } else {
            cur_val = pre_val->next;
            val->next = cur_val->next;
            cur_val->next = val;
            if (ctn->uni.ptr == cur_val) ctn->uni.ptr = val;
            ctx->pre = cur_val;
        }
    }
    unsafe_styyjson_inc_len(ctn);
    return true;
}

styyjson_api_inline bool styyjson_ptr_ctx_replace(styyjson_ptr_ctx *ctx,
                                              styyjson_mut_val *val) {
    styyjson_mut_val *ctn, *pre_key, *cur_key, *pre_val, *cur_val;
    if (!ctx || !ctx->ctn || !ctx->pre || !val) return false;
    ctn = ctx->ctn;
    if (styyjson_mut_is_obj(ctn)) {
        pre_key = ctx->pre;
        pre_val = pre_key->next;
        cur_key = pre_val->next;
        cur_val = cur_key->next;
        /* replace current value */
        cur_key->next = val;
        val->next = cur_val->next;
        ctx->old = cur_val;
    } else {
        pre_val = ctx->pre;
        cur_val = pre_val->next;
        /* replace current value */
        if (pre_val != cur_val) {
            val->next = cur_val->next;
            pre_val->next = val;
            if (ctn->uni.ptr == cur_val) ctn->uni.ptr = val;
        } else {
            val->next = val;
            ctn->uni.ptr = val;
            ctx->pre = val;
        }
        ctx->old = cur_val;
    }
    return true;
}

styyjson_api_inline bool styyjson_ptr_ctx_remove(styyjson_ptr_ctx *ctx) {
    styyjson_mut_val *ctn, *pre_key, *pre_val, *cur_key, *cur_val;
    size_t len;
    if (!ctx || !ctx->ctn || !ctx->pre) return false;
    ctn = ctx->ctn;
    if (styyjson_mut_is_obj(ctn)) {
        pre_key = ctx->pre;
        pre_val = pre_key->next;
        cur_key = pre_val->next;
        cur_val = cur_key->next;
        /* remove current key-value */
        pre_val->next = cur_val->next;
        if (ctn->uni.ptr == cur_key) ctn->uni.ptr = pre_key;
        ctx->pre = NULL;
        ctx->old = cur_val;
    } else {
        pre_val = ctx->pre;
        cur_val = pre_val->next;
        /* remove current key-value */
        pre_val->next = cur_val->next;
        if (ctn->uni.ptr == cur_val) ctn->uni.ptr = pre_val;
        ctx->pre = NULL;
        ctx->old = cur_val;
    }
    len = unsafe_styyjson_get_len(ctn) - 1;
    if (len == 0) ctn->uni.ptr = NULL;
    unsafe_styyjson_set_len(ctn, len);
    return true;
}

#undef styyjson_ptr_set_err



/*==============================================================================
 * JSON Value at Pointer API (Implementation)
 *============================================================================*/

/**
 Set provided `value` if the JSON Pointer (RFC 6901) exists and is type bool.
 Returns true if value at `ptr` exists and is the correct type, otherwise false.
 */
styyjson_api_inline bool styyjson_ptr_get_bool(
    styyjson_val *root, const char *ptr, bool *value) {
    styyjson_val *val = styyjson_ptr_get(root, ptr);
    if (value && styyjson_is_bool(val)) {
        *value = unsafe_styyjson_get_bool(val);
        return true;
    } else {
        return false;
    }
}

/**
 Set provided `value` if the JSON Pointer (RFC 6901) exists and is an integer
 that fits in `uint64_t`. Returns true if successful, otherwise false.
 */
styyjson_api_inline bool styyjson_ptr_get_uint(
    styyjson_val *root, const char *ptr, uint64_t *value) {
    styyjson_val *val = styyjson_ptr_get(root, ptr);
    if (value && val) {
        uint64_t ret = val->uni.u64;
        if (unsafe_styyjson_is_uint(val) ||
            (unsafe_styyjson_is_sint(val) && !(ret >> 63))) {
            *value = ret;
            return true;
        }
    }
    return false;
}

/**
 Set provided `value` if the JSON Pointer (RFC 6901) exists and is an integer
 that fits in `int64_t`. Returns true if successful, otherwise false.
 */
styyjson_api_inline bool styyjson_ptr_get_sint(
    styyjson_val *root, const char *ptr, int64_t *value) {
    styyjson_val *val = styyjson_ptr_get(root, ptr);
    if (value && val) {
        int64_t ret = val->uni.i64;
        if (unsafe_styyjson_is_sint(val) ||
            (unsafe_styyjson_is_uint(val) && ret >= 0)) {
            *value = ret;
            return true;
        }
    }
    return false;
}

/**
 Set provided `value` if the JSON Pointer (RFC 6901) exists and is type real.
 Returns true if value at `ptr` exists and is the correct type, otherwise false.
 */
styyjson_api_inline bool styyjson_ptr_get_real(
    styyjson_val *root, const char *ptr, double *value) {
    styyjson_val *val = styyjson_ptr_get(root, ptr);
    if (value && styyjson_is_real(val)) {
        *value = unsafe_styyjson_get_real(val);
        return true;
    } else {
        return false;
    }
}

/**
 Set provided `value` if the JSON Pointer (RFC 6901) exists and is type sint,
 uint or real.
 Returns true if value at `ptr` exists and is the correct type, otherwise false.
 */
styyjson_api_inline bool styyjson_ptr_get_num(
    styyjson_val *root, const char *ptr, double *value) {
    styyjson_val *val = styyjson_ptr_get(root, ptr);
    if (value && styyjson_is_num(val)) {
        *value = unsafe_styyjson_get_num(val);
        return true;
    } else {
        return false;
    }
}

/**
 Set provided `value` if the JSON Pointer (RFC 6901) exists and is type string.
 Returns true if value at `ptr` exists and is the correct type, otherwise false.
 */
styyjson_api_inline bool styyjson_ptr_get_str(
    styyjson_val *root, const char *ptr, const char **value) {
    styyjson_val *val = styyjson_ptr_get(root, ptr);
    if (value && styyjson_is_str(val)) {
        *value = unsafe_styyjson_get_str(val);
        return true;
    } else {
        return false;
    }
}



/*==============================================================================
 * Deprecated
 *============================================================================*/

/** @deprecated renamed to `styyjson_doc_ptr_get` */
styyjson_deprecated("renamed to styyjson_doc_ptr_get")
styyjson_api_inline styyjson_val *styyjson_doc_get_pointer(styyjson_doc *doc,
                                                     const char *ptr) {
    return styyjson_doc_ptr_get(doc, ptr);
}

/** @deprecated renamed to `styyjson_doc_ptr_getn` */
styyjson_deprecated("renamed to styyjson_doc_ptr_getn")
styyjson_api_inline styyjson_val *styyjson_doc_get_pointern(styyjson_doc *doc,
                                                      const char *ptr,
                                                      size_t len) {
    return styyjson_doc_ptr_getn(doc, ptr, len);
}

/** @deprecated renamed to `styyjson_mut_doc_ptr_get` */
styyjson_deprecated("renamed to styyjson_mut_doc_ptr_get")
styyjson_api_inline styyjson_mut_val *styyjson_mut_doc_get_pointer(
    styyjson_mut_doc *doc, const char *ptr) {
    return styyjson_mut_doc_ptr_get(doc, ptr);
}

/** @deprecated renamed to `styyjson_mut_doc_ptr_getn` */
styyjson_deprecated("renamed to styyjson_mut_doc_ptr_getn")
styyjson_api_inline styyjson_mut_val *styyjson_mut_doc_get_pointern(
    styyjson_mut_doc *doc, const char *ptr, size_t len) {
    return styyjson_mut_doc_ptr_getn(doc, ptr, len);
}

/** @deprecated renamed to `styyjson_ptr_get` */
styyjson_deprecated("renamed to styyjson_ptr_get")
styyjson_api_inline styyjson_val *styyjson_get_pointer(styyjson_val *val,
                                                 const char *ptr) {
    return styyjson_ptr_get(val, ptr);
}

/** @deprecated renamed to `styyjson_ptr_getn` */
styyjson_deprecated("renamed to styyjson_ptr_getn")
styyjson_api_inline styyjson_val *styyjson_get_pointern(styyjson_val *val,
                                                  const char *ptr,
                                                  size_t len) {
    return styyjson_ptr_getn(val, ptr, len);
}

/** @deprecated renamed to `styyjson_mut_ptr_get` */
styyjson_deprecated("renamed to styyjson_mut_ptr_get")
styyjson_api_inline styyjson_mut_val *styyjson_mut_get_pointer(styyjson_mut_val *val,
                                                         const char *ptr) {
    return styyjson_mut_ptr_get(val, ptr);
}

/** @deprecated renamed to `styyjson_mut_ptr_getn` */
styyjson_deprecated("renamed to styyjson_mut_ptr_getn")
styyjson_api_inline styyjson_mut_val *styyjson_mut_get_pointern(styyjson_mut_val *val,
                                                          const char *ptr,
                                                          size_t len) {
    return styyjson_mut_ptr_getn(val, ptr, len);
}

/** @deprecated renamed to `styyjson_mut_ptr_getn` */
styyjson_deprecated("renamed to unsafe_styyjson_ptr_getn")
styyjson_api_inline styyjson_val *unsafe_styyjson_get_pointer(styyjson_val *val,
                                                        const char *ptr,
                                                        size_t len) {
    styyjson_ptr_err err;
    return unsafe_styyjson_ptr_getx(val, ptr, len, &err);
}

/** @deprecated renamed to `unsafe_styyjson_mut_ptr_getx` */
styyjson_deprecated("renamed to unsafe_styyjson_mut_ptr_getx")
styyjson_api_inline styyjson_mut_val *unsafe_styyjson_mut_get_pointer(
    styyjson_mut_val *val, const char *ptr, size_t len) {
    styyjson_ptr_err err;
    return unsafe_styyjson_mut_ptr_getx(val, ptr, len, NULL, &err);
}



/*==============================================================================
 * Compiler Hint End
 *============================================================================*/

#if defined(__clang__)
#   pragma clang diagnostic pop
#elif defined(__GNUC__)
#   if (__GNUC__ > 4) || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6)
#   pragma GCC diagnostic pop
#   endif
#elif defined(_MSC_VER)
#   pragma warning(pop)
#endif /* warning suppress end */

#ifdef __cplusplus
}
#endif /* extern "C" end */

#endif /* YYJSON_H */
