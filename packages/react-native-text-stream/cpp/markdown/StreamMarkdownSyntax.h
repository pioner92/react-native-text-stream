#pragma once

#include "StreamMarkdown.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// A deliberately bounded lexical highlighter. It scans UTF-16 once; no regex,
// recursive grammar, platform calls, or per-token string allocation. Unknown
// languages remain plain code. Ranges are relative to the fenced code body.
namespace stmd {
struct SyntaxToken { size_t start, length; uint32_t flags; };
enum class Language { none, javascript, typescript, json, python };

inline Language syntaxLanguage(std::string language) {
  for (char& c : language) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
  if (language == "js" || language == "javascript" || language == "jsx") return Language::javascript;
  if (language == "ts" || language == "typescript" || language == "tsx") return Language::typescript;
  if (language == "json") return Language::json;
  if (language == "py" || language == "python") return Language::python;
  return Language::none;
}
inline bool syntaxWord(uint16_t c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$' || c >= 128;
}
inline bool syntaxDigit(uint16_t c) { return c >= '0' && c <= '9'; }
inline bool syntaxEquals(const uint16_t* text, size_t length, const char* word) {
  size_t i = 0;
  while (i < length && word[i] && text[i] == static_cast<unsigned char>(word[i])) ++i;
  return i == length && word[i] == 0;
}
template<size_t N>
inline bool syntaxIn(const uint16_t* text, size_t length, const char* const (&words)[N]) {
  for (const char* word : words) if (syntaxEquals(text, length, word)) return true;
  return false;
}

inline std::vector<SyntaxToken> syntaxTokens(const uint16_t* text, size_t length, Language language) {
  std::vector<SyntaxToken> result;
  if (language == Language::none || !text) return result;
  const bool python = language == Language::python;
  const bool json = language == Language::json;
  static const char* const jsKeywords[] = {"async", "await", "break", "case", "catch", "class", "const",
    "continue", "debugger", "default", "delete", "do", "else", "export", "extends", "false", "finally",
    "for", "from", "function", "if", "import", "in", "instanceof", "let", "new", "null", "of", "return",
    "static", "super", "switch", "this", "throw", "true", "try", "typeof", "undefined", "var", "void", "while", "yield"};
  static const char* const tsKeywords[] = {"abstract", "as", "asserts", "declare", "enum", "implements", "infer",
    "interface", "is", "keyof", "namespace", "never", "private", "protected", "public", "readonly", "satisfies", "type"};
  static const char* const types[] = {"any", "bigint", "boolean", "number", "object", "string", "symbol", "unknown"};
  static const char* const pyKeywords[] = {"False", "None", "True", "and", "as", "assert", "async", "await",
    "break", "class", "continue", "def", "del", "elif", "else", "except", "finally", "for", "from", "global",
    "if", "import", "in", "is", "lambda", "nonlocal", "not", "or", "pass", "raise", "return", "try", "while", "with", "yield"};
  static const char* const jsonKeywords[] = {"true", "false", "null"};
  for (size_t i = 0; i < length;) {
    const size_t start = i;
    uint32_t flags = 0;
    const uint16_t c = text[i];
    if (!json && ((python && c == '#') || (!python && c == '/' && i + 1 < length && text[i + 1] == '/'))) {
      while (i < length && text[i] != '\n') ++i;
      flags = STMD_RUN_TOKEN_COMMENT;
    } else if (!json && !python && c == '/' && i + 1 < length && text[i + 1] == '*') {
      i += 2;
      while (i < length) {
        if (text[i] == '*' && i + 1 < length && text[i + 1] == '/') { i += 2; break; }
        ++i;
      }
      flags = STMD_RUN_TOKEN_COMMENT;
    } else if (c == '"' || (!json && (c == '\'' || (!python && c == '`')))) {
      const bool triple = python && i + 2 < length && text[i + 1] == c && text[i + 2] == c;
      i += triple ? 3 : 1;
      while (i < length) {
        if (text[i] == '\\') { i += i + 1 < length ? 2 : 1; continue; }
        if (text[i] == c && (!triple || (i + 2 < length && text[i + 1] == c && text[i + 2] == c))) {
          i += triple ? 3 : 1;
          break;
        }
        // A malformed single-line string must not swallow following code.
        if (text[i] == '\n' && !triple && c != '`') break;
        ++i;
      }
      flags = STMD_RUN_TOKEN_STRING;
    } else if (syntaxDigit(c)) {
      ++i;
      // Numeric literals are lexical, not a validation of the source program.
      while (i < length) {
        const uint16_t next = text[i];
        if (syntaxDigit(next) || next == '.' || next == '_' ||
            (next >= 'a' && next <= 'f') || (next >= 'A' && next <= 'F') ||
            next == 'x' || next == 'X' || next == 'o' || next == 'O' || next == 'n') { ++i; continue; }
        if ((next == '+' || next == '-') && i > start && (text[i - 1] == 'e' || text[i - 1] == 'E')) { ++i; continue; }
        break;
      }
      flags = STMD_RUN_TOKEN_NUMBER;
    } else if (syntaxWord(c)) {
      ++i;
      while (i < length && (syntaxWord(text[i]) || syntaxDigit(text[i]))) ++i;
      const size_t count = i - start;
      const bool keyword = json ? syntaxIn(text + start, count, jsonKeywords)
        : python ? syntaxIn(text + start, count, pyKeywords)
        : syntaxIn(text + start, count, jsKeywords) ||
          (language == Language::typescript && syntaxIn(text + start, count, tsKeywords));
      if (keyword) flags = STMD_RUN_TOKEN_KEYWORD;
      else if (language == Language::typescript && syntaxIn(text + start, count, types)) flags = STMD_RUN_TOKEN_TYPE;
    } else ++i;
    if (flags) result.push_back({start, i - start, flags});
  }
  return result;
}
} // namespace stmd
