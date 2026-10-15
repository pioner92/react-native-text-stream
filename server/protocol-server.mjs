// Loopback-only protocol fixtures for native SSE/NDJSON streamFetch parsing tests.
// Start with `node server/protocol-server.mjs` (PORT defaults to 9881).
import http from 'node:http';

const port = Number(process.env.PORT) || 9881;

function writeFragmented(res, body, { oneByte = true, hang = false } = {}) {
  const bytes = Buffer.from(body);
  let offset = 0;
  let timer;
  const close = () => {
    if (timer) clearTimeout(timer);
    timer = undefined;
  };
  res.on('close', close);
  const next = () => {
    if (offset === bytes.length) {
      if (!hang) res.end();
      return;
    }
    const end = Math.min(bytes.length, offset + (oneByte ? 1 : 7));
    res.write(bytes.subarray(offset, end));
    offset = end;
    timer = setTimeout(next, 1);
  };
  next();
}

function sendProtocol(res, contentType, body, options) {
  res.writeHead(200, {
    'Content-Type': contentType,
    'Cache-Control': 'no-cache',
    'X-Content-Type-Options': 'nosniff',
  });
  res.flushHeaders();
  writeFragmented(res, body, options);
}

const sse = (events, { crlf = false, bom = false } = {}) => {
  const nl = crlf ? '\r\n' : '\n';
  return (bom ? '\uFEFF' : '') + events.map(lines => lines.join(nl) + nl + nl).join('');
};

const openAIChat = sse([
  ['data: {"id":"chatcmpl_fixture","object":"chat.completion.chunk","created":1,"model":"gpt-fixture","choices":[{"index":0,"delta":{"content":"Hel"},"finish_reason":null}]}'],
  ['data: {"id":"chatcmpl_fixture","object":"chat.completion.chunk","created":1,"model":"gpt-fixture","choices":[{"index":0,"delta":{"reasoning_content":"ignore"},"finish_reason":null}]}'],
  ['data: {"id":"chatcmpl_fixture","object":"chat.completion.chunk","created":1,"model":"gpt-fixture","choices":[{"index":0,"delta":{"content":"ло\\u0000"},"finish_reason":null}]}'],
  ['data: {"id":"chatcmpl_fixture","object":"chat.completion.chunk","created":1,"model":"gpt-fixture","choices":[{"index":0,"delta":{"tool_calls":[{}]},"finish_reason":null}]}'],
  ['data: {"id":"chatcmpl_fixture","object":"chat.completion.chunk","created":1,"model":"gpt-fixture","choices":[{"index":0,"delta":{"content":" 🚀"},"finish_reason":null}]}'],
  ['data: [DONE]'],
], { crlf: true, bom: true });

const openAIResponses = sse([
  ['event: response.output_text.delta', 'data: {"type":"response.output_text.delta","delta":"Hel","item_id":"msg_fixture","output_index":0,"content_index":0}'],
  ['event: response.reasoning.delta', 'data: {"type":"response.reasoning.delta","delta":"ignore","item_id":"rs_fixture","output_index":0,"content_index":0}'],
  ['event: response.output_text.delta', 'data: {"type":"response.output_text.delta","delta":"ло\\u0000 🚀","item_id":"msg_fixture","output_index":0,"content_index":0}'],
  ['event: response.completed', 'data: {"type":"response.completed","response":{"id":"resp_fixture","status":"completed"}}'],
], { crlf: true, bom: true });

const anthropic = sse([
  ['event: message_start', 'data: {"type":"message_start","message":{"id":"msg_fixture","type":"message","role":"assistant","content":[]}}'],
  ['event: content_block_delta', 'data: {"type":"content_block_delta","index":0,"delta":{"type":"text_delta",', 'data: "text":"Helло\\u0000 🚀"}}'],
  ['event: content_block_delta', 'data: {"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":"ignore"}}'],
  ['event: message_stop', 'data: {"type":"message_stop"}'],
], { crlf: true, bom: true });

const ollama = '\uFEFF' + [
  '{"message":{"content":"Hel"},"done":false}',
  '{"message":{"thinking":"ignore"},"done":false}',
  '{"message":{"content":"ло\\u0000 🚀"},"done":false}',
  '{"message":{"tool_calls":[{}]},"done":false}',
  '{"done":true}',
].join('\r\n') + '\r\n';

const genericSSE = sse([
  [': comment ignored'],
  ['event: token', 'data: {"message":', 'data: {"content":"**bold"}}'],
  ['event: token', 'data: {"message":{"content":"** and `code`"}}'],
  ['event: done', 'data: {"done":true}'],
], { crlf: true, bom: true });

const genericNDJSON = '\uFEFF' + [
  '{"message":{"content":"Hel"}}',
  '{"noise":true}',
  '{"message":{"content":"ло\\u0000 🚀"}}',
].join('\r\n') + '\r\n';

const server = http.createServer((req, res) => {
  const url = new URL(req.url, 'http://localhost');
  if (url.pathname === '/health') {
    res.writeHead(204).end();
    return;
  }
  if (url.pathname === '/status') {
    res.writeHead(Number(url.searchParams.get('code')) || 500, { 'Content-Type': 'text/plain' }).end('protocol status');
    return;
  }
  if (url.pathname === '/sse/generic') {
    sendProtocol(res, 'text/event-stream; charset=utf-8', genericSSE, { hang: url.searchParams.has('hang') });
    return;
  }
  if (url.pathname === '/sse/unterminated') {
    sendProtocol(res, 'text/event-stream; charset=utf-8', 'data: {"message":{"content":"discard me"}}', {});
    return;
  }
  if (url.pathname === '/sse/malformed') {
    sendProtocol(res, 'text/event-stream; charset=utf-8', sse([['data: {bad json}']]), {});
    return;
  }
  if (url.pathname === '/sse/oversize') {
    sendProtocol(res, 'text/event-stream; charset=utf-8', sse([[`data: {"message":{"content":"${'x'.repeat(4096)}"}}`]]), {});
    return;
  }
  if (url.pathname === '/sse/links') {
    sendProtocol(res, 'text/event-stream; charset=utf-8', sse([
      ['data: {"message":{"content":"[web](https://example.com) [mail](mailto:hello@example.com) [bad](javascript:alert(1))"}}'],
      ['data: [DONE]'],
    ]), {});
    return;
  }
  if (url.pathname === '/provider/openai-chat') {
    const body = url.searchParams.has('no-terminal')
      ? openAIChat.replace('data: [DONE]\r\n\r\n', '')
      : url.searchParams.has('error')
        ? sse([['data: {"error":{"message":"fixture failure"}}']], { crlf: true })
        : openAIChat;
    sendProtocol(res, 'text/event-stream; charset=utf-8', body, { hang: url.searchParams.has('hang') });
    return;
  }
  if (url.pathname === '/provider/openai-responses') {
    const body = url.searchParams.has('error') ? sse([['event: error', 'data: {"type":"error","message":"fixture failure"}']]) : openAIResponses;
    sendProtocol(res, 'text/event-stream; charset=utf-8', body, { hang: url.searchParams.has('hang') });
    return;
  }
  if (url.pathname === '/provider/anthropic') {
    const body = url.searchParams.has('error') ? sse([['event: error', 'data: {"type":"error","error":{"type":"overloaded_error","message":"fixture failure"}}']]) : anthropic;
    sendProtocol(res, 'text/event-stream; charset=utf-8', body, { hang: url.searchParams.has('hang') });
    return;
  }
  if (url.pathname === '/provider/ollama') {
    const body = url.searchParams.has('error') ? '{"error":"fixture failure"}\n' : ollama;
    sendProtocol(res, 'application/x-ndjson; charset=utf-8', body, { hang: url.searchParams.has('hang') });
    return;
  }
  if (url.pathname === '/ndjson/generic') {
    sendProtocol(res, 'application/x-ndjson; charset=utf-8', genericNDJSON, { hang: url.searchParams.has('hang') });
    return;
  }
  if (url.pathname === '/ndjson/eof') {
    sendProtocol(res, 'application/x-ndjson; charset=utf-8', '{"message":{"content":"last line"}}', {});
    return;
  }
  if (url.pathname === '/ndjson/root') {
    // A JSON Pointer of "" selects the entire JSON document, which may itself be a string.
    sendProtocol(res, 'application/x-ndjson; charset=utf-8', '"root value"\n', {});
    return;
  }
  if (url.pathname === '/ndjson/partial') {
    // A valid record followed by a deliberately unterminated record. The connection remains open
    // so the client's idle timeout, rather than EOF, verifies that no partial record is flushed.
    sendProtocol(res, 'application/x-ndjson; charset=utf-8',
      '{"message":{"content":"kept"}}\n{"message":{"content":"discarded"}', { hang: true });
    return;
  }
  if (url.pathname === '/ndjson/missing') {
    sendProtocol(res, 'application/x-ndjson; charset=utf-8', '{"message":{"other":"skip"}}\n{"message":{"content":null}}\n', {});
    return;
  }
  if (url.pathname === '/ndjson/wrong-type') {
    sendProtocol(res, 'application/x-ndjson; charset=utf-8', '{"message":{"content":7}}\n', {});
    return;
  }
  if (url.pathname === '/ndjson/malformed') {
    sendProtocol(res, 'application/x-ndjson; charset=utf-8', '{"message":{"content":"ok"}}\n{bad json}\n', {});
    return;
  }
  if (url.pathname === '/ndjson/exact') {
    const content = url.searchParams.get('text') ?? 'é🚀';
    sendProtocol(res, 'application/x-ndjson; charset=utf-8', `{"message":{"content":${JSON.stringify(content)}}}\n`, {});
    return;
  }
  res.writeHead(404, { 'Content-Type': 'text/plain' }).end('not found');
});

server.listen(port, '127.0.0.1', () => console.log(`protocol fixtures ready on ${port}`));

process.on('SIGTERM', () => server.close(() => process.exit(0)));
process.on('SIGINT', () => server.close(() => process.exit(0)));
