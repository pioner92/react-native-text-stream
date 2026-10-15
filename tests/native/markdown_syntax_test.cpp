#include "StreamMarkdownSyntax.h"
#include <cassert>
#include <iostream>
#include <string>

static std::vector<stmd::SyntaxToken> tokens(const std::u16string& text, stmd::Language language) {
  // Copy rather than alias char16_t through uint16_t on platforms where their types differ.
  const std::vector<uint16_t> units(text.begin(), text.end());
  auto result = stmd::syntaxTokens(units.data(), units.size(), language);
  size_t end = 0;
  for (const auto& token : result) {
    assert(token.length > 0 && token.start >= end && token.start + token.length <= units.size());
    end = token.start + token.length;
  }
  return result;
}
static void expectToken(const std::u16string& text, stmd::Language language, const std::u16string& word, uint32_t flag) {
  const auto result = tokens(text, language);
  bool found = false;
  for (const auto& token : result) {
    if (token.flags == flag && text.substr(token.start, token.length) == word) found = true;
  }
  assert(found);
}
int main() {
  using stmd::Language;
  const std::u16string ts = u"// 🚀 comment\nconst answer: string = \"hello\"; let n = 42; /* end */\n";
  expectToken(ts, Language::typescript, u"const", STMD_RUN_TOKEN_KEYWORD);
  expectToken(ts, Language::typescript, u"string", STMD_RUN_TOKEN_TYPE);
  expectToken(ts, Language::typescript, u"\"hello\"", STMD_RUN_TOKEN_STRING);
  expectToken(ts, Language::typescript, u"42", STMD_RUN_TOKEN_NUMBER);
  expectToken(ts, Language::typescript, u"// 🚀 comment", STMD_RUN_TOKEN_COMMENT);
  expectToken(ts, Language::typescript, u"/* end */", STMD_RUN_TOKEN_COMMENT);
  assert(tokens(ts, Language::none).empty());
  assert(tokens(u"constellation myconst 变量return", Language::javascript).empty());
  expectToken(u"'a\\'b' `value ${42}`", Language::javascript, u"'a\\'b'", STMD_RUN_TOKEN_STRING);
  expectToken(u"'a\\'b' `value ${42}`", Language::javascript, u"`value ${42}`", STMD_RUN_TOKEN_STRING);
  expectToken(u"\"unclosed\nconst next = 1", Language::javascript, u"const", STMD_RUN_TOKEN_KEYWORD);
  expectToken(u"/* unclosed", Language::javascript, u"/* unclosed", STMD_RUN_TOKEN_COMMENT);
  const std::u16string py = u"def greet(name):\n  '''multi\nline'''\n  return 'hi' # note\n";
  expectToken(py, Language::python, u"def", STMD_RUN_TOKEN_KEYWORD);
  expectToken(py, Language::python, u"'''multi\nline'''", STMD_RUN_TOKEN_STRING);
  expectToken(py, Language::python, u"# note", STMD_RUN_TOKEN_COMMENT);
  expectToken(u"{\"ok\": true, \"n\": -1.5e+3}", Language::json, u"true", STMD_RUN_TOKEN_KEYWORD);
  expectToken(u"{\"ok\": true, \"n\": -1.5e+3}", Language::json, u"1.5e+3", STMD_RUN_TOKEN_NUMBER);
  assert(tokens(u"// /* # const undefined", Language::json).empty());
  for (const auto language : {Language::javascript, Language::typescript, Language::json, Language::python}) {
    for (const auto& sample : {ts, py, std::u16string(u"\"\\\"🚀\" /* */ 0xff 3e-2"), std::u16string(100000, 'a')}) {
      // Every prefix must yield ordered, nonempty, in-bounds spans, including surrogate splits.
      const size_t step = sample.size() > 1000 ? 997 : 1;
      for (size_t prefix = 0; prefix <= sample.size(); prefix += step) tokens(sample.substr(0, prefix), language);
      tokens(sample, language);
    }
  }
  assert(stmd::syntaxLanguage("TS") == Language::typescript);
  assert(stmd::syntaxLanguage("python") == Language::python);
  assert(stmd::syntaxLanguage("unknown") == Language::none);
  std::cout << "Markdown syntax lexer checks passed\n";
}
