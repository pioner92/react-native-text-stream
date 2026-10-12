// Deterministic HTTP fixtures for the native streamFetch regression suite. Loopback only.
import http from "node:http";
import { gzipSync } from "node:zlib";

export const TEXT = "\uFEFFПривет é 漢字 🚀\n";
const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, "http://localhost");
  const send = (status, body, headers = {}) => {
    res.writeHead(status, {
      "Content-Type": "text/plain; charset=utf-8",
      "X-Content-Type-Options": "nosniff",
      ...headers,
    });
    res.end(body);
  };
  if (url.pathname === "/echo") {
    const chunks = [];
    for await (const chunk of req) chunks.push(chunk);
    return send(
      200,
      JSON.stringify({
        method: req.method,
        headers: req.headers,
        hex: Buffer.concat(chunks).toString("hex"),
      }),
    );
  }
  if (url.pathname === "/redirect")
    return send(Number(url.searchParams.get("status")) || 302, "", {
      Location: "/echo",
    });
  if (url.pathname === "/empty")
    return send(Number(url.searchParams.get("status")) || 204, "");
  if (url.pathname === "/gzip")
    return send(200, gzipSync(Buffer.from(TEXT)), {
      "Content-Encoding": "gzip",
    });
  if (url.pathname === "/bad-gzip")
    return send(200, "not gzip", { "Content-Encoding": "gzip" });
  if (url.pathname === "/bad-gzip-crc") {
    const body = gzipSync(Buffer.from(TEXT));
    body[body.length - 8] ^= 255;
    return send(200, body, { "Content-Encoding": "gzip" });
  }
  if (url.pathname === "/headers-delay") {
    const timer = setTimeout(() => send(200, "late"), 2000);
    res.on("close", () => clearTimeout(timer));
    return;
  }
  res.writeHead(200, {
    "Content-Type": "text/plain; charset=utf-8",
    "X-Content-Type-Options": "nosniff",
    ...(url.pathname === "/broken" ? { "Content-Length": "1000" } : {}),
  });
  res.flushHeaders();
  if (url.pathname === "/stall" || url.pathname === "/broken") {
    res.write("prefix");
    if (url.pathname === "/broken") {
      const timer = setTimeout(() => res.destroy(), 80);
      res.on("close", () => clearTimeout(timer));
    }
    return;
  }
  const body = Buffer.from(url.searchParams.get("text") ?? TEXT);
  const chunkSize = Math.max(1, Number(url.searchParams.get("chunk")) || 1);
  let offset = 0;
  const timer = setInterval(() => {
    if (offset >= body.length) {
      clearInterval(timer);
      res.end();
      return;
    }
    res.write(body.subarray(offset, (offset += chunkSize)));
  }, 2);
  res.on("close", () => clearInterval(timer));
});
server.listen(Number(process.env.PORT) || 9878, "127.0.0.1", () =>
  console.log("streamFetch fixtures ready"),
);
