#include "StreamTextResponseParser.hpp"
extern "C" {
#include "vendor/yyjson/yyjson.h"
}
#include <new>
#include <utility>
#include <vector>
namespace facebook::react {
namespace {
bool provider(StreamTextExtraction e) {
  return e == StreamTextExtraction::OpenAIChat ||
         e == StreamTextExtraction::OpenAIResponses ||
         e == StreamTextExtraction::Anthropic ||
         e == StreamTextExtraction::Ollama;
}
bool blank(std::string_view s) {
  for (unsigned char c : s)
    if (c != ' ' && c != '\t')
      return false;
  return true;
}
styyjson_val *get(styyjson_val *v, const char *k) {
  return v && styyjson_is_obj(v) ? styyjson_obj_get(v, k) : nullptr;
}
bool str(styyjson_val *v, std::string_view &s) {
  if (!v || !styyjson_is_str(v))
    return false;
  s = {styyjson_get_str(v), styyjson_get_len(v)};
  return true;
}
} // namespace
struct StreamTextResponseParser::Impl {
  StreamTextParserOptions o;
  Sink sink;
  StreamTextParseStatus state{StreamTextParseStatus::Continue};
  std::string err, line, data, event, bom;
  std::vector<char> jsonPool;
  bool skipLF = false, began = false, completed = false, hasData = false;
  Impl(StreamTextParserOptions a, Sink b)
      : o(std::move(a)), sink(std::move(b)) {
    // Error reporting remains allocation-free even when a record allocation
    // fails.
    err.reserve(128);
    if (!o.maxEventBytes || o.maxEventBytes > 16 * 1024 * 1024)
      fail("invalid maxEventBytes");
  }
  void fail(const char *s) {
    if (state == StreamTextParseStatus::Continue) {
      state = StreamTextParseStatus::Error;
      err = s;
    }
  }
  void done() {
    if (state == StreamTextParseStatus::Continue) {
      completed = true;
      state = StreamTextParseStatus::Done;
    }
  }
  bool append(std::string &to, std::string_view from) {
    if (from.size() > o.maxEventBytes ||
        to.size() > o.maxEventBytes - from.size()) {
      fail("response record exceeds maxEventBytes");
      return false;
    }
    if (!from.empty())
      to.append(from.data(), from.size());
    return true;
  }
  bool out(std::string_view s) {
    if (state != StreamTextParseStatus::Continue)
      return false;
    if (s.empty())
      return true;
    if (!sink || !sink(s)) {
      state = StreamTextParseStatus::Overflow;
      err = "response sink capacity exceeded";
      return false;
    }
    return true;
  }
  void json(std::string_view source, std::string_view sseType) {
    const size_t required =
        styyjson_read_max_memory_usage(source.size(), YYJSON_READ_NOFLAG);
    // yyjson's conservative bound includes DOM overhead (13 * record bytes +
    // 256). Bound by the configured record limit, not a smaller arbitrary
    // multiplier.
    if (!required || source.size() > o.maxEventBytes) {
      fail("JSON response record exceeds allocator limit");
      return;
    }
    if (jsonPool.size() < required)
      jsonPool.resize(required);
    styyjson_alc allocator{};
    if (!styyjson_alc_pool_init(&allocator, jsonPool.data(), jsonPool.size())) {
      fail("JSON allocator unavailable");
      return;
    }
    styyjson_read_err re{};
    styyjson_doc *doc =
        styyjson_read_opts(const_cast<char *>(source.data()), source.size(),
                           YYJSON_READ_NOFLAG, &allocator, &re);
    if (!doc) {
      fail("invalid JSON response record");
      return;
    }
    std::unique_ptr<styyjson_doc, decltype(&styyjson_doc_free)> document(
        doc, styyjson_doc_free);
    styyjson_val *root = styyjson_doc_get_root(doc);
    std::string_view text;
    if (provider(o.extraction)) {
      auto *error = get(root, "error");
      if (error && !styyjson_is_null(error)) {
        fail("provider stream error");
        return;
      }
    }
    if (o.extraction == StreamTextExtraction::JsonPointer) {
      styyjson_val *v = o.textPointer.empty()
                            ? root
                            : styyjson_ptr_get(root, o.textPointer.c_str());
      if (v && !styyjson_is_null(v)) {
        if (!str(v, text))
          fail("JSON pointer does not resolve to a string");
        else
          out(text);
      }
    } else if (o.extraction == StreamTextExtraction::OpenAIChat) {
      styyjson_val *choices = get(root, "choices");
      auto choicesIterator = styyjson_arr_iter_with(choices);
      while (auto *choice = styyjson_arr_iter_next(&choicesIterator)) {
        auto *index = get(choice, "index");
        if (index && styyjson_is_uint(index) && styyjson_get_uint(index) == 0) {
          styyjson_val *d = get(choice, "delta");
          if (str(get(d, "content"), text))
            out(text);
          if (str(get(d, "refusal"), text))
            out(text);
          break;
        }
      }
    } else if (o.extraction == StreamTextExtraction::OpenAIResponses) {
      std::string_view t;
      str(get(root, "type"), t);
      if (t.empty())
        t = sseType;
      if (t == "response.output_text.delta" || t == "response.refusal.delta") {
        if (str(get(root, "delta"), text))
          out(text);
        else
          fail("OpenAI response delta is not a string");
      } else if (t == "response.completed")
        done();
      else if (t == "response.failed" || t == "response.incomplete" ||
               t == "error")
        fail("OpenAI response stream error");
    } else if (o.extraction == StreamTextExtraction::Anthropic) {
      std::string_view type;
      str(get(root, "type"), type);
      if (type.empty())
        type = sseType;
      if (type == "message_stop")
        done();
      else if (type == "error")
        fail("Anthropic stream error");
      else if (type == "content_block_start") {
        styyjson_val *b = get(root, "content_block");
        std::string_view t;
        if (str(get(b, "type"), t) && t == "text" && str(get(b, "text"), text))
          out(text);
      } else if (type == "content_block_delta") {
        styyjson_val *d = get(root, "delta");
        std::string_view t;
        if (str(get(d, "type"), t) && t == "text_delta") {
          if (str(get(d, "text"), text))
            out(text);
          else
            fail("Anthropic text delta is not a string");
        }
      }
    } else if (o.extraction == StreamTextExtraction::Ollama) {
      if (styyjson_val *e = get(root, "error"); e && !styyjson_is_null(e))
        fail("Ollama stream error");
      else {
        styyjson_val *m = get(root, "message");
        if (m && !styyjson_is_obj(m))
          fail("Ollama message is not an object");
        else if (str(get(m, "content"), text) ||
                 str(get(root, "response"), text))
          out(text);
        styyjson_val *d = get(root, "done");
        if (d && styyjson_is_true(d))
          done();
        else if (d && !styyjson_is_false(d))
          fail("Ollama done is not a boolean");
      }
    } else
      fail("JSON extraction requires JsonPointer or a provider");
  }
  void dispatch() {
    if (state != StreamTextParseStatus::Continue || !hasData) {
      data.clear();
      event.clear();
      hasData = false;
      return;
    }
    std::string_view s(data);
    if (o.extraction == StreamTextExtraction::OpenAIChat && s == "[DONE]") {
      done();
    } else if (o.extraction == StreamTextExtraction::Data)
      out(s);
    else
      json(s, event);
    data.clear();
    event.clear();
    hasData = false;
  }
  void sseLine(std::string_view s) {
    if (s.empty()) {
      dispatch();
      return;
    }
    if (s.front() == ':')
      return;
    size_t p = s.find(':');
    std::string_view key = s.substr(0, p), v = p == std::string_view::npos
                                                   ? std::string_view{}
                                                   : s.substr(p + 1);
    if (!v.empty() && v.front() == ' ')
      v.remove_prefix(1);
    if (key == "data") {
      if (hasData && !append(data, "\n"))
        return;
      hasData = true;
      append(data, v);
    } else if (key == "event") {
      if (v.empty())
        event.clear();
      else
        event.assign(v.data(), v.size());
      if (event.size() > o.maxEventBytes)
        fail("response record exceeds maxEventBytes");
    }
  }
  void lineDone() {
    if (o.protocol == StreamTextProtocol::SSE)
      sseLine(line);
    else if (o.protocol == StreamTextProtocol::NDJSON && !blank(line))
      json(line, {});
    line.clear();
  }
  void scan(const uint8_t *bytes, size_t size) {
    if (!size || state != StreamTextParseStatus::Continue)
      return;
    size_t offset = 0;
    if (skipLF) {
      skipLF = false;
      if (bytes[0] == '\n')
        ++offset;
    }
    while (offset < size && state == StreamTextParseStatus::Continue) {
      const size_t start = offset;
      while (offset < size && bytes[offset] != '\r' && bytes[offset] != '\n')
        ++offset;
      // Append each contiguous segment once. No accumulated prefix is
      // rescanned.
      if (offset > start &&
          !append(line, {reinterpret_cast<const char *>(bytes + start),
                         offset - start}))
        return;
      if (offset == size)
        return;
      const bool carriageReturn = bytes[offset++] == '\r';
      if (carriageReturn) {
        if (offset == size)
          skipLF = true;
        else if (bytes[offset] == '\n')
          ++offset;
      }
      lineDone();
    }
  }
  StreamTextParseStatus feed(const uint8_t *p, size_t n) {
    if (state != StreamTextParseStatus::Continue || !n)
      return state;
    if (!p && n) {
      fail("null response bytes");
      return state;
    }
    if (o.protocol == StreamTextProtocol::Text) {
      out({reinterpret_cast<const char *>(p), n});
      return state;
    }
    size_t i = 0;
    while (i < n && state == StreamTextParseStatus::Continue && !began) {
      bom.push_back(static_cast<char>(p[i++]));
      if (bom.size() == 3) {
        began = true;
        if (bom != "\xEF\xBB\xBF")
          scan(reinterpret_cast<const uint8_t *>(bom.data()), bom.size());
        bom.clear();
      }
    }
    if (i < n && state == StreamTextParseStatus::Continue)
      scan(p + i, n - i);
    return state;
  }
  StreamTextParseStatus finish() {
    if (state != StreamTextParseStatus::Continue)
      return state;
    if (!began && !bom.empty()) {
      began = true;
      scan(reinterpret_cast<const uint8_t *>(bom.data()), bom.size());
      bom.clear();
    }
    if (state != StreamTextParseStatus::Continue)
      return state;
    if (o.protocol == StreamTextProtocol::SSE) {
      line.clear();
      data.clear();
      event.clear();
    } else if (!line.empty())
      lineDone();
    if (state == StreamTextParseStatus::Continue) {
      if (provider(o.extraction) && !completed)
        fail("provider stream ended without completion marker");
      else
        state = StreamTextParseStatus::Done;
    }
    return state;
  }
};
StreamTextResponseParser::StreamTextResponseParser(StreamTextParserOptions o,
                                                   Sink s) {
  try {
    impl_ = std::make_unique<Impl>(std::move(o), std::move(s));
  } catch (const std::bad_alloc &) {
  }
}
StreamTextResponseParser::~StreamTextResponseParser() = default;
StreamTextParseStatus StreamTextResponseParser::feed(const uint8_t *p,
                                                     size_t n) {
  if (!impl_)
    return StreamTextParseStatus::Error;
  try {
    return impl_->feed(p, n);
  } catch (const std::bad_alloc &) {
    impl_->fail("out of memory");
    return impl_->state;
  } catch (...) {
    impl_->fail("response parser failure");
    return impl_->state;
  }
}
StreamTextParseStatus StreamTextResponseParser::finish() {
  if (!impl_)
    return StreamTextParseStatus::Error;
  try {
    return impl_->finish();
  } catch (const std::bad_alloc &) {
    impl_->fail("out of memory");
    return impl_->state;
  } catch (...) {
    impl_->fail("response parser failure");
    return impl_->state;
  }
}
StreamTextParseStatus StreamTextResponseParser::status() const {
  return impl_ ? impl_->state : StreamTextParseStatus::Error;
}
const std::string &StreamTextResponseParser::error() const {
  static const std::string unavailable = "parser unavailable";
  return impl_ ? impl_->err : unavailable;
}
} // namespace facebook::react
