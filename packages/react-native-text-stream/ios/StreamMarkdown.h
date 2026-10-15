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
};

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
  size_t changedStartUTF16;
  size_t sourceBytesConsumed;
  int error;
} STMarkdownDocument;

STMarkdownParser *stmd_create(void);
void stmd_destroy(STMarkdownParser *parser);
const STMarkdownDocument *stmd_parse(
    STMarkdownParser *parser, const char *utf8, size_t length, bool github);
// Parses text already passed through the platform stream decoder. Unlike
// stmd_parse(), a leading U+FEFF is content and is retained.
const STMarkdownDocument *stmd_parse_decoded(
    STMarkdownParser *parser, const char *utf8, size_t length, bool github);

#ifdef __cplusplus
}
#endif
