// Public StreamText Markdown ABI. The document is borrowed and remains valid
// until the next stmd_parse() on the same parser or stmd_destroy().
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct STMarkdownParser STMarkdownParser;

enum {
  STMD_RUN_BOLD = 1u << 0,
  STMD_RUN_ITALIC = 1u << 1,
  STMD_RUN_STRIKE = 1u << 2,
  STMD_RUN_CODE = 1u << 3,
  STMD_RUN_CODE_BLOCK = 1u << 4,
  STMD_RUN_QUOTE = 1u << 5,
  STMD_RUN_LINK = 1u << 6,
  // Normalized fenced-code language metadata. These bits are set only on
  // STMD_RUN_CODE_BLOCK runs; an unknown fence language has no language bit.
  STMD_RUN_LANGUAGE_JAVASCRIPT = 1u << 8,
  STMD_RUN_LANGUAGE_TYPESCRIPT = 1u << 9,
  STMD_RUN_LANGUAGE_JSON = 1u << 10,
  STMD_RUN_LANGUAGE_PYTHON = 1u << 11,
  STMD_RUN_LANGUAGE_MASK = 15u << 8,
  STMD_RUN_TOKEN_KEYWORD = 1u << 16,
  STMD_RUN_TOKEN_STRING = 1u << 17,
  STMD_RUN_TOKEN_COMMENT = 1u << 18,
  STMD_RUN_TOKEN_NUMBER = 1u << 19,
  STMD_RUN_TOKEN_TYPE = 1u << 20,
};

enum { STMD_BLOCK_TABLE = 1, STMD_BLOCK_IMAGE = 2 };
enum { STMD_ALIGN_DEFAULT = 0, STMD_ALIGN_LEFT = 1, STMD_ALIGN_CENTER = 2, STMD_ALIGN_RIGHT = 3 };

typedef struct {
  size_t start;
  size_t length;
  uint32_t flags;
  int headingLevel;
  int indent;
  const char *url;
} STMarkdownRun;

typedef struct {
  const uint16_t *text;
  size_t textLength;
  const STMarkdownRun *runs;
  size_t runCount;
  uint32_t flags; /* STMD_BLOCK_CELL_HEADER for header cells. */
  int alignment;
} STMarkdownCell;

enum { STMD_BLOCK_CELL_HEADER = 1u << 0 };

typedef struct {
  size_t start; /* UTF-16 U+FFFC placeholder offset in document text. */
  size_t length; /* Always one for the current block kinds. */
  uint32_t kind;
  size_t rowCount;
  size_t columnCount;
  const STMarkdownCell *cells; /* Row-major; table only. */
  const char *url; /* Image source UTF-8; null for tables. */
  const uint16_t *alt;
  size_t altLength;
} STMarkdownBlock;

typedef struct {
  const uint16_t *text;
  size_t textLength;
  const STMarkdownRun *runs;
  size_t runCount;
  size_t changedStartUTF16;
  size_t sourceBytesConsumed;
  int error;
  const STMarkdownBlock *blocks;
  size_t blockCount;
} STMarkdownDocument;

STMarkdownParser *stmd_create(void);
void stmd_destroy(STMarkdownParser *parser);
const STMarkdownDocument *stmd_parse(
    STMarkdownParser *parser, const char *utf8, size_t length, bool github);
// Parses text already passed through the platform stream decoder. Unlike
// stmd_parse(), a leading U+FEFF is content and is retained.
const STMarkdownDocument *stmd_parse_decoded(
    STMarkdownParser *parser, const char *utf8, size_t length, bool github);

// Produces a progressive display projection. While final is false, a bounded
// unfinished Markdown tail (for example an open link or emphasis delimiter)
// is withheld so transport fragments are not rendered as raw syntax. Passing
// final=true restores ordinary CommonMark/GFM semantics for the exact source.
// The decoded form follows stmd_parse_decoded() BOM semantics.
const STMarkdownDocument *stmd_parse_streaming(
    STMarkdownParser *parser, const char *utf8, size_t length, bool github,
    bool final);
const STMarkdownDocument *stmd_parse_decoded_streaming(
    STMarkdownParser *parser, const char *utf8, size_t length, bool github,
    bool final);

#ifdef __cplusplus
}
#endif
